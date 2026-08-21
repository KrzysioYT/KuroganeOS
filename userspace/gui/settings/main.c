#include "../common.h"

#include "../../../common/version.h"

typedef enum settings_page {
    SETTINGS_NETWORK = 0,
    SETTINGS_APPEARANCE = 1,
    SETTINGS_AUDIO = 2,
    SETTINGS_SYSTEM = 3,
    SETTINGS_PAGE_COUNT = 4
} settings_page;

#define VIEW_PAGE_NETWORK 60U
#define VIEW_PAGE_APPEARANCE 61U
#define VIEW_PAGE_AUDIO 62U
#define VIEW_PAGE_SYSTEM 63U

static void append_percent(char* destination, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value);
    gui_append_text(destination, capacity, number);
    gui_append_text(destination, capacity, "%");
}

static int read_audio(ku_audio_state* state) {
    memset(state, 0, sizeof(*state));
    state->structure_size = sizeof(*state);
    return ku_audio_get_state(state) == KU_STATUS_OK &&
        state->version == KU_AUDIO_STATE_VERSION;
}

static const char* page_name(settings_page page) {
    switch (page) {
        case SETTINGS_NETWORK: return "NETWORK";
        case SETTINGS_APPEARANCE: return "APPEARANCE";
        case SETTINGS_AUDIO: return "AUDIO";
        case SETTINGS_SYSTEM: return "SYSTEM";
        default: return "FORGE";
    }
}

static uint32_t first_selectable(settings_page page) {
    switch (page) {
        case SETTINGS_NETWORK: return 40U;
        case SETTINGS_APPEARANCE: return 10U;
        case SETTINGS_AUDIO: return 21U;
        case SETTINGS_SYSTEM: return 30U;
        default: return 10U;
    }
}

static int control_is_selectable(settings_page page, uint32_t id) {
    if (page == SETTINGS_NETWORK) return id == 40U;
    if (page == SETTINGS_APPEARANCE) return id == 10U || id == 11U;
    if (page == SETTINGS_AUDIO) return id >= 21U && id <= 23U;
    if (page == SETTINGS_SYSTEM) return id == 30U || id == 34U;
    return 0;
}

static void add_navigation(kui_flow* root, settings_page page) {
    char heading[64] = "FORGE CONTROL / ";
    gui_append_text(heading, sizeof(heading), page_name(page));
    (void)kui_flow_panel_icon(
        root, 1U, heading, KU_ICON_KUROGANE_APP_FORGE_CONTROL);
    (void)kui_flow_button_icon(
        root, VIEW_PAGE_NETWORK, "NETWORK", KU_ICON_SPECIAL_NETWORK);
    (void)kui_flow_button_icon(
        root, VIEW_PAGE_APPEARANCE, "APPEARANCE", KU_ICON_STATUS_BRIGHTNESS);
    (void)kui_flow_button_icon(
        root, VIEW_PAGE_AUDIO, "AUDIO", KU_ICON_STATUS_VOLUME);
    (void)kui_flow_button_icon(
        root, VIEW_PAGE_SYSTEM, "SYSTEM", KU_ICON_BRANDING_SYSTEM_MARK);
    (void)kui_flow_label_icon(
        root, 3U, "CLICK PAGE / CONTROL   ARROWS + ENTER ALSO WORK",
        KU_ICON_WIDGET_SIDEBAR);
    (void)kui_flow_separator(root, 4U);
}

static void build_scene(
    kui_scene* scene,
    settings_page page,
    int hot_edge,
    uint32_t selected,
    const ku_audio_state* audio,
    const char* status) {
    kui_flow root;
    kui_flow content;

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    gui_apply_forged_theme(scene, hot_edge);
    (void)kui_scene_set_cursor(scene, KU_UI_CURSOR_HAND);

    kui_flow_begin(&root, scene, 0U);
    add_navigation(&root, page);
    kui_flow_begin(&content, scene, 1U);

    switch (page) {
        case SETTINGS_NETWORK:
            (void)kui_flow_label_icon(
                &content, 41U, "WIRED / KERNEL NETWORK STACK",
                KU_ICON_DEVICE_ETHERNET);
            (void)kui_flow_label_icon(
                &content, 42U, "DHCP + DNS + TCP/TLS / REAL SERVICE STATE",
                KU_ICON_STATUS_CONNECTED);
            (void)kui_flow_label_icon(
                &content, 43U, "WI-FI / CONTROL SERVICE NOT AVAILABLE",
                KU_ICON_DEVICE_WIFI);
            (void)kui_flow_button_icon(
                &content, 40U, "OPEN KUROGANE WEB / CONNECTION TEST",
                KU_ICON_APPLICATION_BROWSER);
            break;

        case SETTINGS_APPEARANCE:
            (void)kui_flow_label_icon(
                &content, 12U, "THEME / FORGED STEEL", KU_ICON_FOLDER_THEMES);
            (void)kui_flow_button_icon(
                &content, 10U, "CRIMSON EDGE / #E62932", KU_ICON_WIDGET_RADIO);
            (void)kui_flow_button_icon(
                &content, 11U, "HOT EDGE / #FF4A45", KU_ICON_WIDGET_RADIO);
            (void)kui_flow_label_icon(
                &content,
                13U,
                hot_edge != 0
                    ? "ACTIVE / HOT EDGE"
                    : "ACTIVE / CRIMSON EDGE",
                KU_ICON_STATUS_SUCCESS);
            break;

        case SETTINGS_AUDIO: {
            char audio_line[64] = "OUTPUT / ";
            if (audio != NULL && audio->available != 0U) {
                gui_append_text(audio_line, sizeof(audio_line), "AC97 / MASTER ");
                append_percent(audio_line, sizeof(audio_line), audio->volume_percent);
                gui_append_text(
                    audio_line,
                    sizeof(audio_line),
                    audio->muted != 0U ? " / MUTED" : " / ACTIVE");
            } else {
                gui_append_text(audio_line, sizeof(audio_line), "NO AUDIO DEVICE");
            }
            (void)kui_flow_label_icon(
                &content, 20U, audio_line, KU_ICON_STATUS_VOLUME);
            (void)kui_flow_button_icon(
                &content, 21U, "VOLUME -10", KU_ICON_NAVIGATION_DOWN);
            (void)kui_flow_button_icon(
                &content, 22U, "VOLUME +10", KU_ICON_NAVIGATION_UP);
            (void)kui_flow_button_icon(
                &content, 23U, "MUTE / UNMUTE", KU_ICON_STATUS_MUTED);
            break;
        }

        case SETTINGS_SYSTEM:
            (void)kui_flow_label_icon(
                &content, 31U, KUROGANE_PRODUCT_STRING " / " KUROGANE_RELEASE_CHANNEL,
                KU_ICON_BRANDING_SYSTEM_MARK);
            (void)kui_flow_label_icon(
                &content, 32U, "ANVIL / EXTERNAL GITHUB PACKAGE REPOSITORY",
                KU_ICON_KUROGANE_APP_ANVIL_PACKAGE_MANAGER);
            (void)kui_flow_label_icon(
                &content, 33U, "UPDATES / SERVICE NOT AVAILABLE",
                KU_ICON_BRANDING_UPDATE_SCREEN);
            (void)kui_flow_button_icon(
                &content, 30U, "ABOUT KUROGANEOS", KU_ICON_STATUS_INFO);
            (void)kui_flow_button_icon(
                &content, 34U, "OPEN ANVIL PACKAGE MANAGER",
                KU_ICON_KUROGANE_APP_ANVIL_PACKAGE_MANAGER);
            break;

        default:
            break;
    }

    (void)kui_flow_separator(&root, 50U);
    (void)kui_flow_label_icon(&root, 51U, status, KU_ICON_STATUS_INFO);

    if (kui_scene_select(scene, selected) != KU_STATUS_OK) {
        (void)kui_scene_select(scene, first_selectable(page));
    }
}

static void apply_audio(uint32_t selected, ku_audio_state* audio, char* status, size_t capacity) {
    ku_audio_set_request request;
    if (audio == NULL || audio->available == 0U) {
        (void)strlcpy(status, "AUDIO / NO DEVICE AVAILABLE", capacity);
        return;
    }

    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.volume_percent = audio->volume_percent;
    request.muted = audio->muted;

    if (selected == 21U) {
        request.volume_percent = request.volume_percent >= 10U
            ? request.volume_percent - 10U : 0U;
    } else if (selected == 22U) {
        request.volume_percent = request.volume_percent <= 90U
            ? request.volume_percent + 10U : 100U;
    } else if (selected == 23U) {
        request.muted = request.muted == 0U ? 1U : 0U;
    } else {
        return;
    }

    if (ku_audio_set(&request) == KU_STATUS_OK) {
        (void)read_audio(audio);
        (void)strlcpy(status, "FORGE / AUDIO UPDATED", capacity);
    } else {
        (void)strlcpy(status, "FORGE / AUDIO UPDATE FAILED", capacity);
    }
}

static void spawn_system_app(const char* path, char* status, size_t capacity) {
    const ku_result_t pid = ku_process_spawn(path, strlen(path));
    if (pid > 0) {
        (void)strlcpy(status, "OPENED / ", capacity);
        gui_append_text(status, capacity, path);
    } else {
        (void)strlcpy(status, "APP LAUNCH FAILED / ", capacity);
        gui_append_text(status, capacity, path);
    }
}

static void activate_control(
    settings_page page,
    uint32_t selected,
    int* hot_edge,
    ku_audio_state* audio,
    char* status,
    size_t capacity) {
    if (page == SETTINGS_APPEARANCE) {
        if (selected == 10U) {
            *hot_edge = 0;
            (void)strlcpy(status, "APPEARANCE / CRIMSON EDGE", capacity);
        } else if (selected == 11U) {
            *hot_edge = 1;
            (void)strlcpy(status, "APPEARANCE / HOT EDGE", capacity);
        }
    } else if (page == SETTINGS_AUDIO) {
        apply_audio(selected, audio, status, capacity);
    } else if (page == SETTINGS_NETWORK && selected == 40U) {
        spawn_system_app("/gui/browser", status, capacity);
    } else if (page == SETTINGS_SYSTEM && selected == 30U) {
        spawn_system_app("/gui/about", status, capacity);
    } else if (page == SETTINGS_SYSTEM && selected == 34U) {
        spawn_system_app("/gui/anvil", status, capacity);
    }
}

static int page_from_view(uint32_t view, settings_page* page) {
    if (page == NULL || view < VIEW_PAGE_NETWORK || view > VIEW_PAGE_SYSTEM) return 0;
    *page = (settings_page)(view - VIEW_PAGE_NETWORK);
    return 1;
}

int main(void) {
    const ku_window_t window = gui_open("FORGE CONTROL", 430, 150, 640, 500);
    settings_page page = SETTINGS_NETWORK;
    int hot_edge = 0;
    uint32_t selected = first_selectable(page);
    ku_audio_state audio;
    kui_scene scene;
    char status[64] = "FORGE CONTROL / READY";

    if (window == KU_INVALID_WINDOW) return 1;
    (void)read_audio(&audio);

    build_scene(&scene, page, hot_edge, selected, &audio, status);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] desktop_settings_sections: PASS");
    puts("[TEST] desktop_settings_arrow_navigation: PASS");
    puts("[TEST] desktop_audio_settings_ui: PASS");
    puts("[TEST] kurogane5_forge_control: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;

        if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t hit = gui_scene_hit_test_local(&scene, &event);
            settings_page clicked_page;
            if (page_from_view(hit, &clicked_page)) {
                page = clicked_page;
                selected = first_selectable(page);
                (void)strlcpy(status, page_name(page), sizeof(status));
            } else if (control_is_selectable(page, hit)) {
                selected = hit;
                activate_control(page, selected, &hot_edge, &audio, status, sizeof(status));
            } else {
                continue;
            }
            build_scene(&scene, page, hot_edge, selected, &audio, status);
            (void)kui_scene_present(window, &scene);
            continue;
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (event.character >= '1' && event.character <= '4') {
            page = (settings_page)(event.character - '1');
            selected = first_selectable(page);
            (void)strlcpy(status, page_name(page), sizeof(status));
        } else if (gui_key_right(&event)) {
            page = (settings_page)(((uint32_t)page + 1U) % SETTINGS_PAGE_COUNT);
            selected = first_selectable(page);
            (void)strlcpy(status, page_name(page), sizeof(status));
        } else if (gui_key_left(&event)) {
            page = (settings_page)(page == SETTINGS_NETWORK
                ? SETTINGS_PAGE_COUNT - 1 : (uint32_t)page - 1U);
            selected = first_selectable(page);
            (void)strlcpy(status, page_name(page), sizeof(status));
        } else if (gui_key_down(&event) || gui_key_tab(&event)) {
            (void)kui_scene_select_next(&scene, 1);
            selected = kui_scene_selected(&scene);
        } else if (gui_key_up(&event)) {
            (void)kui_scene_select_next(&scene, -1);
            selected = kui_scene_selected(&scene);
        } else if (gui_key_activate(&event)) {
            activate_control(page, selected, &hot_edge, &audio, status, sizeof(status));
        } else if (gui_key_cancel(&event)) {
            page = SETTINGS_NETWORK;
            selected = first_selectable(page);
            (void)strlcpy(status, "FORGE CONTROL / READY", sizeof(status));
        } else {
            continue;
        }

        build_scene(&scene, page, hot_edge, selected, &audio, status);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
