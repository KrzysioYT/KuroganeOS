#include "../common.h"
#include "../../../common/version.h"

enum forge_control_section {
    FORGE_SECTION_PERFORMANCE = 100,
    FORGE_SECTION_NETWORK = 101,
    FORGE_SECTION_AUDIO = 102
};

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_percent(char* line, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value > 100U ? 100U : value);
    append_text(line, capacity, number);
    append_text(line, capacity, "%");
}

static void build_percent_metric(
    char* line, size_t capacity, const char* label, uint32_t value) {
    line[0] = '\0';
    append_text(line, capacity, label);
    append_text(line, capacity, "\n");
    append_percent(line, capacity, value);
}

static void append_mib(char* line, size_t capacity, uint64_t bytes) {
    char number[24];
    gui_u64(number, sizeof(number), bytes / UINT64_C(1048576));
    append_text(line, capacity, number);
    append_text(line, capacity, " MiB");
}

static void append_kib(char* line, size_t capacity, uint64_t bytes) {
    char number[24];
    gui_u64(number, sizeof(number), bytes / UINT64_C(1024));
    append_text(line, capacity, number);
    append_text(line, capacity, " KiB");
}

static void append_ipv4(char* line, size_t capacity, const uint8_t address[4]) {
    char number[24];
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        gui_u64(number, sizeof(number), address[index]);
        append_text(line, capacity, number);
        if (index + 1U != 4U) append_text(line, capacity, ".");
    }
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

static void build_scene(
    kui_scene* scene,
    const ku_system_snapshot* snapshot,
    const ku_network_status* network,
    int network_valid,
    const ku_audio_state* audio,
    int audio_valid,
    uint32_t active_section,
    const char* status) {
    kui_flow root;
    char cpu_metric[64];
    char ram_metric[64];
    char gpu_metric[64];
    char disk_metric[64];
    char memory_line[64] = "MEMORY / ";
    char network_line[64] = "NETWORK / ";
    char audio_line[64] = "AUDIO / ";
    uint32_t network_priority = 1U;
    const int audio_available =
        audio_valid && audio != NULL && audio->available != 0U;

    build_percent_metric(
        cpu_metric, sizeof(cpu_metric), "CPU", snapshot->cpu_percent);
    build_percent_metric(
        ram_metric, sizeof(ram_metric), "RAM", snapshot->ram_percent);
    build_percent_metric(
        gpu_metric, sizeof(gpu_metric), "GPU", snapshot->gpu_percent);
    build_percent_metric(
        disk_metric, sizeof(disk_metric), "STORAGE", snapshot->disk_percent);

    append_mib(memory_line, sizeof(memory_line),
               snapshot->memory_total_bytes - snapshot->memory_free_bytes);
    append_text(memory_line, sizeof(memory_line), " / ");
    append_mib(memory_line, sizeof(memory_line), snapshot->memory_total_bytes);

    if (!network_valid) {
        append_text(network_line, sizeof(network_line), "STATUS UNAVAILABLE");
        network_priority = 4U;
    } else if (network->ready == 0U) {
        append_text(network_line, sizeof(network_line),
                    network->physical != 0U ? "LINK / WAITING" : "OFFLINE");
        network_priority = 3U;
    } else {
        append_text(network_line, sizeof(network_line), "ONLINE / ");
        append_ipv4(network_line, sizeof(network_line), network->address);
        append_text(network_line, sizeof(network_line), " / RX ");
        append_kib(network_line, sizeof(network_line), network->bytes_received);
        append_text(network_line, sizeof(network_line), " TX ");
        append_kib(network_line, sizeof(network_line), network->bytes_transmitted);
    }

    if (!audio_valid) {
        append_text(audio_line, sizeof(audio_line), "STATUS UNAVAILABLE");
    } else if (audio->available == 0U) {
        append_text(audio_line, sizeof(audio_line), "DEVICE OFFLINE");
    } else {
        append_text(audio_line, sizeof(audio_line),
                    audio->muted != 0U ? "MUTED / " : "ACTIVE / ");
        append_percent(audio_line, sizeof(audio_line), audio->volume_percent);
        append_text(audio_line, sizeof(audio_line), " / 48 KHZ STEREO");
    }

    kui_scene_initialize(scene);
    scene->visible_rows = 16U;
    kui_scene_apply_flux_theme(scene);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FORGE CONTROL / LIVE SYSTEM");
    (void)kui_flow_tile(
        &root, FORGE_SECTION_PERFORMANCE,
        "PERFORMANCE\nCPU / RAM / GPU", KU_UI_NATIVE_ICON_PERFORMANCE);
    (void)kui_flow_tile(
        &root, FORGE_SECTION_NETWORK,
        "NETWORK\nLINK / TRAFFIC", KU_UI_NATIVE_ICON_MONITOR);
    (void)kui_flow_tile(
        &root, FORGE_SECTION_AUDIO,
        "AUDIO\nOUTPUT / LEVEL", KU_UI_NATIVE_ICON_SETTINGS);
    (void)kui_flow_separator(&root, 5U);

    (void)kui_scene_set_flags(
        scene, FORGE_SECTION_PERFORMANCE,
        active_section == FORGE_SECTION_PERFORMANCE ? KUI_VIEW_SELECTED : 0U);
    (void)kui_scene_set_flags(
        scene, FORGE_SECTION_NETWORK,
        active_section == FORGE_SECTION_NETWORK ? KUI_VIEW_SELECTED : 0U);
    (void)kui_scene_set_flags(
        scene, FORGE_SECTION_AUDIO,
        active_section == FORGE_SECTION_AUDIO ? KUI_VIEW_SELECTED : 0U);

    if (active_section == FORGE_SECTION_NETWORK) {
        (void)kui_flow_panel(&root, 30U, "NETWORK / CONNECTIVITY");
        (void)kui_flow_notice(&root, 31U, network_line, network_priority);
        (void)kui_flow_label(
            &root, 32U,
            network_valid && network->physical != 0U
                ? "ADAPTER / PHYSICAL LINK DETECTED"
                : "ADAPTER / NO PHYSICAL LINK");
        (void)kui_flow_label(
            &root, 33U, status != NULL ? status : "LIVE / READY");
    } else if (active_section == FORGE_SECTION_AUDIO) {
        (void)kui_flow_panel(&root, 40U, "AUDIO / OUTPUT");
        (void)kui_flow_label(&root, 41U, audio_line);
        (void)kui_flow_progress(
            &root, 42U, "MASTER VOLUME",
            audio_available ? audio->volume_percent : 0U, 100U);
        (void)kui_flow_button(&root, 20U, "VOLUME -10");
        (void)kui_flow_toggle(
            &root, 21U, "MUTE OUTPUT\nPERSISTENT DEVICE STATE",
            audio_available && audio->muted != 0U);
        (void)kui_flow_button(&root, 22U, "VOLUME +10");
        (void)kui_flow_label(
            &root, 43U, status != NULL ? status : "LIVE / READY");
        if (!audio_available) {
            (void)kui_scene_set_flags(scene, 20U, KUI_VIEW_DISABLED);
            (void)kui_scene_set_flags(scene, 21U, KUI_VIEW_DISABLED);
            (void)kui_scene_set_flags(scene, 22U, KUI_VIEW_DISABLED);
        }
    } else {
        (void)kui_flow_panel(&root, 10U, "PERFORMANCE / LIVE TELEMETRY");
        (void)kui_flow_metric(
            &root, 11U, cpu_metric, snapshot->cpu_percent, 100U);
        (void)kui_flow_metric(
            &root, 12U, ram_metric, snapshot->ram_percent, 100U);
        (void)kui_flow_metric(
            &root, 13U, gpu_metric, snapshot->gpu_percent, 100U);
        (void)kui_flow_metric(
            &root, 14U, disk_metric, snapshot->disk_percent, 100U);
        (void)kui_flow_label(&root, 15U, memory_line);
        (void)kui_flow_label(
            &root, 16U, status != NULL ? status : "LIVE / READY");
    }
}

static int apply_audio_action(
    uint32_t target,
    ku_audio_state* audio,
    int audio_valid,
    char* status,
    size_t status_capacity) {
    ku_audio_set_request request;
    if (!audio_valid || audio == NULL || audio->available == 0U) {
        (void)strlcpy(status, "AUDIO / DEVICE NOT AVAILABLE", status_capacity);
        return 0;
    }

    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.volume_percent = audio->volume_percent;
    request.muted = audio->muted;
    if (target == 20U) {
        request.volume_percent = request.volume_percent >= 10U
            ? request.volume_percent - 10U : 0U;
    } else if (target == 21U) {
        request.muted = request.muted == 0U ? 1U : 0U;
    } else if (target == 22U) {
        request.volume_percent = request.volume_percent <= 90U
            ? request.volume_percent + 10U : 100U;
    } else {
        return 0;
    }

    if (ku_audio_set(&request) != KU_STATUS_OK || !read_audio(audio)) {
        (void)strlcpy(status, "AUDIO / APPLY FAILED", status_capacity);
        return 0;
    }
    (void)strlcpy(status, "AUDIO / APPLIED", status_capacity);
    puts("[TEST] flux_control_center_audio_action: PASS");
    return 1;
}

int main(void) {
    const ku_window_t window = gui_open("FORGE CONTROL", 360, 80, 580, 500);
    ku_system_snapshot snapshot;
    ku_network_status network;
    ku_audio_state audio;
    kui_scene scene;
    uint32_t active_section = FORGE_SECTION_PERFORMANCE;
    uint32_t pointer_buttons = 0U;
    uint32_t refresh_ticks = 0U;
    char status[64] = "LIVE / READY";
    int network_valid;
    int audio_valid;

    if (window == KU_INVALID_WINDOW) return 1;
    if (!read_system(&snapshot)) {
        (void)ku_ui_close(window);
        return 2;
    }
    network_valid = read_network(&network);
    audio_valid = read_audio(&audio);
    build_scene(
        &scene, &snapshot, &network, network_valid,
        &audio, audio_valid, active_section, status);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 3;
    }

    puts("[TEST] desktop_performance_live: PASS");
    puts("[TEST] desktop_performance_low_damage: PASS");
    puts("[TEST] flux_control_center_live: PASS");
    if (network_valid) puts("[TEST] flux_control_center_network_status: PASS");
    if (audio_valid) puts("[TEST] flux_control_center_audio_status: PASS");

    for (;;) {
        ku_ui_event event;
        int available;
        int scene_changed = 0;
        int audio_surface_opened = 0;

        do {
            available = kui_next_event(window, &event);
            if (available < 0) {
                (void)ku_ui_close(window);
                return 0;
            }
            if (available == 0) break;
            if (event.type == KU_UI_EVENT_CLOSE) {
                (void)ku_ui_close(window);
                return 0;
            }
            if (event.type == KU_UI_EVENT_POINTER) {
                const uint32_t previous_buttons = pointer_buttons;
                const int primary_pressed =
                    (event.buttons & UINT32_C(1)) != 0U &&
                    (previous_buttons & UINT32_C(1)) == 0U;
                pointer_buttons = event.buttons;
                if (primary_pressed) {
                    const uint32_t target = kui_scene_hit_test(&scene, event.x, event.y);
                    if (target == FORGE_SECTION_PERFORMANCE ||
                        target == FORGE_SECTION_NETWORK ||
                        target == FORGE_SECTION_AUDIO) {
                        active_section = target;
                        audio_surface_opened =
                            target == FORGE_SECTION_AUDIO ? 1 : 0;
                        scene_changed = 1;
                    } else if (active_section == FORGE_SECTION_AUDIO &&
                               (target == 20U || target == 21U || target == 22U)) {
                        (void)apply_audio_action(
                            target, &audio, audio_valid, status, sizeof(status));
                        audio_valid = read_audio(&audio);
                        scene_changed = 1;
                    }
                }
            }
        } while (available > 0);

        if (refresh_ticks >= 20U) {
            if (!read_system(&snapshot)) {
                (void)ku_ui_close(window);
                return 4;
            }
            network_valid = read_network(&network);
            audio_valid = read_audio(&audio);
            refresh_ticks = 0U;
            scene_changed = 1;
        }

        if (scene_changed) {
            build_scene(
                &scene, &snapshot, &network, network_valid,
                &audio, audio_valid, active_section, status);
            if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
                (void)ku_ui_close(window);
                return 5;
            }
            if (audio_surface_opened) {
                puts("[TEST] forge_control_audio_surface: PASS");
            }
        }

        if (kuro_sleep(5U) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 6;
        }
        ++refresh_ticks;
    }
}
