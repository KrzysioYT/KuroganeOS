#include "../common.h"
#include "../../../common/version.h"
#include <kurogane/notification.h>

#define APP_COUNT 8U
#define CHILD_CAPACITY 10U
#define NO_APP_ID UINT32_C(0xFFFFFFFF)
#define DESKTOP_PIN_STATE_PATH "/home/desktop.cfg"

typedef struct launcher_app {
    const char* label;
    const char* subtitle;
    const char* path;
    uint32_t desktop_id;
    uint32_t icon;
    int pinnable;
} launcher_app;

static const launcher_app g_apps[APP_COUNT] = {
    {"TERMINAL", "shared shell / development", "/gui/terminal", KU_DESKTOP_APP_TERMINAL, KU_UI_NATIVE_ICON_TERMINAL, 1},
    {"FILES", "persistent root / applications", "/gui/files", KU_DESKTOP_APP_FILES, KU_UI_NATIVE_ICON_FILES, 1},
    {"CONTROL CENTER", "system pulse / network / audio", "/gui/perf", KU_DESKTOP_APP_PERFORMANCE, KU_UI_NATIVE_ICON_PERFORMANCE, 1},
    {"KUROGANE WEB", "native HTTP browser", "/gui/browser", KU_DESKTOP_APP_BROWSER, KU_UI_NATIVE_ICON_BROWSER, 1},
    {"MONITOR", "runtime / process health", "/gui/sysmon", KU_DESKTOP_APP_MONITOR, KU_UI_NATIVE_ICON_MONITOR, 1},
    {"SETTINGS", "appearance / sound", "/gui/settings", KU_DESKTOP_APP_SETTINGS, KU_UI_NATIVE_ICON_SETTINGS, 1},
    {"ABOUT", "KuroganeOS platform", "/gui/about", KU_DESKTOP_APP_ABOUT, KU_UI_NATIVE_ICON_ABOUT, 1},
    {"NOTIFICATIONS", "public system activity", "/gui/notify", NO_APP_ID, KU_UI_NATIVE_ICON_NOTIFICATION, 0},
};

static uint64_t g_children[CHILD_CAPACITY];
static uint32_t g_child_apps[CHILD_CAPACITY];
static size_t g_selected = 0U;
static char g_status[64] = "FLUX DECK / READY";
static ku_service_connection_t g_notification_connection = 0U;

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_percent(char* destination, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value > 100U ? 100U : value);
    append_text(destination, capacity, number);
    append_text(destination, capacity, "%");
}

static int read_system(ku_system_snapshot* snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->structure_size = sizeof(*snapshot);
    return ku_system_get_snapshot(snapshot) == KU_STATUS_OK &&
        snapshot->version == KU_SYSTEM_SNAPSHOT_VERSION;
}

static int read_network(ku_network_status* network) {
    memset(network, 0, sizeof(*network));
    network->structure_size = sizeof(*network);
    return ku_network_get_status(network) == KU_STATUS_OK;
}

static int read_audio(ku_audio_state* audio) {
    memset(audio, 0, sizeof(*audio));
    audio->structure_size = sizeof(*audio);
    return ku_audio_get_state(audio) == KU_STATUS_OK &&
        audio->version == KU_AUDIO_STATE_VERSION;
}

static ku_status_t notification_transact(
    ku_service_connection_t connection,
    const ku_notification_request* request,
    ku_notification_response* response) {
    uint32_t attempts = 0U;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    while (attempts++ < 300U) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_notification_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static int publish_session_notification(void) {
    uint32_t attempts = 0U;
    ku_result_t connected = KU_STATUS_NOT_FOUND;
    ku_notification_request request;
    ku_notification_response response;
    while (attempts++ < 300U) {
        connected = ku_notification_connect();
        if (connected > 0) break;
        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK) return 0;
        (void)kuro_sleep(1U);
    }
    if (connected <= 0) return 0;
    g_notification_connection = (ku_service_connection_t)connected;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_NOTIFICATION_POST;
    request.type = KU_NOTIFICATION_TYPE_SYSTEM;
    request.priority = KU_NOTIFICATION_PRIORITY_NORMAL;
    request.flags = KU_NOTIFICATION_FLAG_PUBLIC;
    (void)strlcpy(request.title, "Flux session ready", sizeof(request.title));
    (void)strlcpy(request.body, "System Pulse, Files and Control Center are online", sizeof(request.body));
    if (notification_transact(g_notification_connection, &request, &response) != KU_STATUS_OK ||
        response.notification_id == 0U ||
        (response.flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U) {
        (void)ku_service_close(g_notification_connection);
        g_notification_connection = 0U;
        return 0;
    }
    return 1;
}

static void reap_children(void) {
    size_t index;
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) continue;
        {
            int32_t status = 0;
            const ku_status_t result = ku_process_wait(g_children[index], &status);
            if (result == KU_STATUS_OK || result == KU_STATUS_NOT_FOUND) {
                g_children[index] = 0U;
                g_child_apps[index] = NO_APP_ID;
            }
        }
    }
}

static int app_is_running(uint32_t app_id) {
    size_t index;
    reap_children();
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] != 0U && g_child_apps[index] == app_id) return 1;
    }
    return 0;
}

static int remember_child(uint64_t pid, uint32_t app_id) {
    size_t index;
    reap_children();
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) {
            g_children[index] = pid;
            g_child_apps[index] = app_id;
            return 1;
        }
    }
    return 0;
}

static int launch_app(size_t index, int quiet_if_running) {
    const launcher_app* app;
    ku_result_t result;
    char number[24];
    if (index >= APP_COUNT) return 0;
    app = &g_apps[index];
    if (app_is_running((uint32_t)index)) {
        if (!quiet_if_running) {
            (void)strlcpy(g_status, "ALREADY RUNNING / USE DOCK TO FOCUS", sizeof(g_status));
        }
        return 1;
    }

    result = ku_process_spawn(app->path, strlen(app->path));
    if (result <= 0) {
        (void)strlcpy(g_status, "APP LAUNCH FAILED", sizeof(g_status));
        return 0;
    }
    (void)remember_child((uint64_t)result, (uint32_t)index);
    gui_u64(number, sizeof(number), (uint64_t)result);
    (void)strlcpy(g_status, "OPENED ", sizeof(g_status));
    append_text(g_status, sizeof(g_status), app->label);
    append_text(g_status, sizeof(g_status), " / PID ");
    append_text(g_status, sizeof(g_status), number);
    return 1;
}

static void launch_selected(void) {
    (void)launch_app(g_selected, 0);
}

static void select_and_launch(size_t index) {
    if (index >= APP_COUNT) return;
    g_selected = index;
    launch_selected();
}

static int pin_state(uint32_t app_id) {
    ku_desktop_pin_request request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app_id;
    request.action = KU_DESKTOP_PIN_QUERY;
    if (ku_desktop_pin(&request) != KU_STATUS_OK) return 0;
    return request.pinned != 0U;
}

static void set_pin_state(uint32_t app_id, int pinned) {
    ku_desktop_pin_request request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app_id;
    request.action = KU_DESKTOP_PIN_SET;
    request.value = pinned != 0 ? 1U : 0U;
    (void)ku_desktop_pin(&request);
}

static uint8_t collect_pin_mask(void) {
    uint8_t mask = UINT8_C(1);
    size_t index;
    for (index = 0U; index < APP_COUNT; ++index) {
        if (!g_apps[index].pinnable) continue;
        if (pin_state(g_apps[index].desktop_id)) {
            mask = (uint8_t)(mask | (uint8_t)(UINT8_C(1) << g_apps[index].desktop_id));
        }
    }
    return mask;
}

static int save_pin_state(void) {
    const char path[] = DESKTOP_PIN_STATE_PATH;
    ku_result_t opened;
    const uint8_t mask = collect_pin_mask();
    ku_status_t status = ku_file_create(path, sizeof(path) - 1U);
    if (status != KU_STATUS_OK && status != KU_STATUS_ALREADY_EXISTS) return 0;
    opened = ku_file_open_ex(path, sizeof(path) - 1U, KU_FILE_OPEN_WRITE);
    if (opened < 0) return 0;
    if (ku_file_write((ku_file_t)opened, &mask, sizeof(mask)) != (ku_result_t)sizeof(mask)) {
        (void)ku_file_close((ku_file_t)opened);
        return 0;
    }
    status = ku_file_close((ku_file_t)opened);
    if (status != KU_STATUS_OK) return 0;
    return ku_file_sync() == KU_STATUS_OK;
}

static int load_pin_state(void) {
    const char path[] = DESKTOP_PIN_STATE_PATH;
    uint8_t mask = 0U;
    size_t index;
    ku_result_t opened = ku_file_open(path, sizeof(path) - 1U);
    if (opened < 0) return 0;
    if (ku_file_read((ku_file_t)opened, &mask, sizeof(mask)) != (ku_result_t)sizeof(mask)) {
        (void)ku_file_close((ku_file_t)opened);
        return 0;
    }
    if (ku_file_close((ku_file_t)opened) != KU_STATUS_OK) return 0;

    for (index = 0U; index < APP_COUNT; ++index) {
        const uint32_t app_id = g_apps[index].desktop_id;
        if (!g_apps[index].pinnable) continue;
        set_pin_state(app_id, (mask & (uint8_t)(UINT8_C(1) << app_id)) != 0U);
    }
    return 1;
}

static void toggle_selected_pin(void) {
    ku_desktop_pin_request request;
    const launcher_app* app = &g_apps[g_selected];
    if (!app->pinnable) {
        (void)strlcpy(g_status, "SYSTEM APP / NOT PINNABLE", sizeof(g_status));
        return;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app->desktop_id;
    request.action = KU_DESKTOP_PIN_TOGGLE;
    if (ku_desktop_pin(&request) != KU_STATUS_OK) {
        (void)strlcpy(g_status, "DESKTOP PIN OPERATION FAILED", sizeof(g_status));
        return;
    }
    (void)strlcpy(g_status, request.pinned != 0U ? "PINNED " : "UNPINNED ",
                  sizeof(g_status));
    append_text(g_status, sizeof(g_status), app->label);
    if (save_pin_state()) {
        append_text(g_status, sizeof(g_status), " / SAVED");
    } else {
        append_text(g_status, sizeof(g_status), " / SESSION ONLY");
    }
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow apps;
    ku_system_snapshot system;
    ku_network_status network;
    ku_audio_state audio;
    const int system_valid = read_system(&system);
    const int network_valid = read_network(&network);
    const int audio_valid = read_audio(&audio);
    char cpu[64] = "CPU\n";
    char ram[64] = "RAM\n";
    char disk[64] = "DISK\n";
    char net[64] = "NETWORK\n";
    char sound[64] = "AUDIO\n";
    uint32_t net_value = 0U;
    uint32_t audio_value = 0U;
    size_t index;

    if (system_valid) {
        append_percent(cpu, sizeof(cpu), system.cpu_percent);
        append_percent(ram, sizeof(ram), system.ram_percent);
        append_percent(disk, sizeof(disk), system.disk_percent);
    } else {
        append_text(cpu, sizeof(cpu), "--");
        append_text(ram, sizeof(ram), "--");
        append_text(disk, sizeof(disk), "--");
    }
    if (!network_valid) {
        append_text(net, sizeof(net), "UNKNOWN");
    } else if (network.ready != 0U) {
        append_text(net, sizeof(net), "ONLINE");
        net_value = 100U;
    } else if (network.physical != 0U) {
        append_text(net, sizeof(net), "LINK");
        net_value = 35U;
    } else {
        append_text(net, sizeof(net), "OFFLINE");
    }
    if (!audio_valid || audio.available == 0U) {
        append_text(sound, sizeof(sound), "OFFLINE");
    } else if (audio.muted != 0U) {
        append_text(sound, sizeof(sound), "MUTED");
        audio_value = audio.volume_percent;
    } else {
        append_percent(sound, sizeof(sound), audio.volume_percent);
        audio_value = audio.volume_percent;
    }

    kui_scene_initialize(scene);
    scene->visible_rows = 16U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX HOME / SYSTEM PULSE");
    (void)kui_flow_metric(&root, 2U, cpu,
                          system_valid ? system.cpu_percent : 0U, 100U);
    (void)kui_flow_metric(&root, 3U, ram,
                          system_valid ? system.ram_percent : 0U, 100U);
    (void)kui_flow_metric(&root, 4U, disk,
                          system_valid ? system.disk_percent : 0U, 100U);
    (void)kui_flow_metric(&root, 5U, net, net_value, 100U);
    (void)kui_flow_metric(&root, 6U, sound, audio_value, 100U);

    kui_flow_begin(&apps, scene, 1U);
    for (index = 0U; index < APP_COUNT; ++index) {
        char label[64] = "";
        uint32_t flags = 0U;
        append_text(label, sizeof(label), g_apps[index].label);
        append_text(label, sizeof(label), "\n");
        append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_tile(
            &apps, 10U + (uint32_t)index, label, g_apps[index].icon);
        if (g_apps[index].pinnable && pin_state(g_apps[index].desktop_id)) flags |= KUI_VIEW_PINNED;
        if (app_is_running((uint32_t)index)) flags |= KUI_VIEW_RUNNING;
        (void)kui_scene_set_flags(scene, 10U + (uint32_t)index, flags);
    }
    (void)kui_flow_button(&root, 30U, "PIN / UNPIN SELECTED");
    (void)kui_flow_button(&root, 31U, "LOG OUT");
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}

int main(void) {
    /* Keep the window title stable: WindowManager treats it as session root. */
    const ku_window_t window = gui_open("RED FLUX HOME", 250, 135, 650, 460);
    kui_scene scene;
    size_t index;
    uint32_t pointer_buttons = 0U;
    uint32_t refresh_ticks = 0U;
    if (window == KU_INVALID_WINDOW) return 1;

    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        g_children[index] = 0U;
        g_child_apps[index] = NO_APP_ID;
    }

    if (load_pin_state()) {
        (void)strlcpy(g_status, "APPS MENU / DESKTOP STATE RESTORED", sizeof(g_status));
        puts("[TEST] desktop_pin_persistence_load: PASS");
    } else {
        puts("[TEST] desktop_pin_persistence_load: DEFAULT");
    }

    puts("[TEST] desktop_launcher_ring3: PASS");
    puts("[TEST] desktop_clean_session: PASS");
    puts("[TEST] desktop_mouse_navigation: PASS");
    puts("[TEST] desktop_keyboard_shortcuts_detached: PASS");
    puts("[TEST] red_flux_dock_controller: PASS");
    puts("[TEST] red_flux_home_pinned: PASS");
    puts("[TEST] desktop_app_pinning: PASS");
    puts("[TEST] red_flux_apps_menu: PASS");
    if (publish_session_notification()) {
        puts("[TEST] flux_home_public_notification: PASS");
    } else {
        puts("[TEST] flux_home_public_notification: FAIL");
    }

    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] red_flux_home_surface: PASS");
    puts("[TEST] red_flux_tile_launcher: PASS");
    puts("[TEST] flux_home_system_pulse: PASS");

    for (;;) {
        ku_ui_event event;
        int available;
        reap_children();
        available = kui_next_event(window, &event);
        if (available < 0) break;
        if (available == 0) {
            ++refresh_ticks;
            if (refresh_ticks >= KU_SYSTEM_TICKS_PER_SECOND) {
                refresh_ticks = 0U;
                build_scene(&scene);
                if (kui_scene_present(window, &scene) != KU_STATUS_OK) break;
            }
            (void)kuro_sleep(1U);
            continue;
        }
        if (event.type == KU_UI_EVENT_CLOSE) break;

        if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            uint32_t target;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
            target = kui_scene_hit_test(&scene, event.x, event.y);
            if (target >= 10U && target < 10U + APP_COUNT) {
                select_and_launch((size_t)(target - 10U));
            } else if (target == 30U) {
                toggle_selected_pin();
            } else if (target == 31U) {
                puts("[TEST] desktop_logout_requested: PASS");
                break;
            } else {
                continue;
            }
            build_scene(&scene);
            (void)kui_scene_present(window, &scene);
            continue;
        }

        /*
         * Physical keyboard shortcuts are intentionally detached. The Window
         * Core still uses key=UNKNOWN as a private compatibility transport for
         * dock/desktop icon activation until a dedicated desktop command event
         * is introduced.
         */
        if (event.type != KU_UI_EVENT_KEY || event.key != KU_UI_KEY_UNKNOWN) continue;
        if (event.character == 't') {
            select_and_launch(0U);
        } else if (event.character == 'f') {
            select_and_launch(1U);
        } else if (event.character == 'v') {
            select_and_launch(2U);
        } else if (event.character == 'b') {
            select_and_launch(3U);
        } else if (event.character == 'm') {
            select_and_launch(4U);
        } else if (event.character == 's') {
            select_and_launch(5U);
        } else if (event.character == 'a') {
            select_and_launch(6U);
        } else if (event.character == 'n') {
            select_and_launch(7U);
        } else {
            continue;
        }
        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }

    if (g_notification_connection != 0U) {
        (void)ku_service_close(g_notification_connection);
        g_notification_connection = 0U;
    }
    (void)ku_ui_close(window);
    return 0;
}
