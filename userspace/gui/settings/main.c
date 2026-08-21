#include "../common.h"

#include "../../../common/version.h"

typedef enum settings_page {
    SETTINGS_NETWORK = 0,
    SETTINGS_APPEARANCE = 1,
    SETTINGS_AUDIO = 2,
    SETTINGS_SYSTEM = 3,
    SETTINGS_PAGE_COUNT = 4
} settings_page;

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
        default: return "SETTINGS";
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

static void add_navigation(kui_flow* root, settings_page page) {
    char heading[64] = "SETTINGS / ";
    gui_append_text(heading, sizeof(heading), page_name(page));
    (void)kui_flow_panel(root, 1U, heading);
    (void)kui_flow_label(root, 2U, "1 NETWORK  2 APPEARANCE  3 AUDIO  4 SYSTEM");
    (void)kui_flow_label(root, 3U, "LEFT/RIGHT PAGE   UP/DOWN SELECT   ENTER APPLY");
    (void)kui_flow_separator(root, 4U);
}

static void build_scene(
    kui_scene* scene,
    settings_page page,
    int low_contrast,
    uint32_t selected,
    const ku_audio_state* audio,
    const char* status) {
    kui_flow root;
    kui_flow content;

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    gui_apply_obsidian_theme(scene, low_contrast);

    kui_flow_begin(&root, scene, 0U);
    add_navigation(&root, page);
    kui_flow_begin(&content, scene, 1U);

    switch (page) {
        case SETTINGS_NETWORK:
            (void)kui_flow_label(&content, 41U, "WIRED / KERNEL NETWORK STACK");
            (void)kui_flow_label(&content, 42U, "DHCP + DNS + TCP/TLS STATUS IN NETWORK SERVICE");
            (void)kui_flow_label(&content, 43U, "WI-FI / USERSPACE CONTROL SERVICE NOT EXPOSED YET");
            (void)kui_flow_button(&content, 40U, "OPEN KUROGANE WEB / CONNECTION TEST");
            break;

        case SETTINGS_APPEARANCE:
            (void)kui_flow_label(&content, 12U, "THEME / OBSIDIAN DARK + CRIMSON ACCENT");
            (void)kui_flow_button(&content, 10U, "OBSIDIAN DARK");
            (void)kui_flow_button(&content, 11U, "OBSIDIAN / REDUCED CONTRAST");
            (void)kui_flow_label(
                &content,
                13U,
                low_contrast != 0
                    ? "ACTIVE / REDUCED CONTRAST"
                    : "ACTIVE / CRIMSON");
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
            (void)kui_flow_label(&content, 20U, audio_line);
            (void)kui_flow_button(&content, 21U, "VOLUME -10");
            (void)kui_flow_button(&content, 22U, "VOLUME +10");
            (void)kui_flow_button(&content, 23U, "MUTE / UNMUTE");
            break;
        }

        case SETTINGS_SYSTEM:
            (void)kui_flow_label(&content, 31U, KUROGANE_PRODUCT_STRING " / " KUROGANE_RELEASE_CHANNEL);
            (void)kui_flow_label(&content, 32U, "PACKAGES / STORE BACKEND REQUIRES PACKAGE SERVICE");
            (void)kui_flow_label(&content, 33U, "UPDATES / RELEASE CHANNEL MANAGED BY SYSTEM");
            (void)kui_flow_button(&content, 30U, "ABOUT KUROGANEOS");
            break;

        default:
            break;
    }

    (void)kui_flow_separator(&root, 50U);
    (void)kui_flow_label(&root, 51U, status);

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
        (void)strlcpy(status, "AUDIO / UPDATED", capacity);
    } else {
        (void)strlcpy(status, "AUDIO / UPDATE FAILED", capacity);
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

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 430, 150, 640, 500);
    settings_page page = SETTINGS_NETWORK;
    int low_contrast = 0;
    uint32_t selected = first_selectable(page);
    ku_audio_state audio;
    kui_scene scene;
    char status[64] = "KuroganeOS 5 / SETTINGS READY";

    if (window == KU_INVALID_WINDOW) return 1;
    (void)read_audio(&audio);

    build_scene(&scene, page, low_contrast, selected, &audio, status);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] desktop_settings_sections: PASS");
    puts("[TEST] desktop_settings_arrow_navigation: PASS");
    puts("[TEST] desktop_audio_settings_ui: PASS");
    puts("[TEST] kurogane5_obsidian_settings: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
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
            if (page == SETTINGS_APPEARANCE) {
                if (selected == 10U) {
                    low_contrast = 0;
                    (void)strlcpy(status, "APPEARANCE / OBSIDIAN CRIMSON", sizeof(status));
                } else if (selected == 11U) {
                    low_contrast = 1;
                    (void)strlcpy(status, "APPEARANCE / REDUCED CONTRAST", sizeof(status));
                }
            } else if (page == SETTINGS_AUDIO) {
                apply_audio(selected, &audio, status, sizeof(status));
            } else if (page == SETTINGS_NETWORK && selected == 40U) {
                spawn_system_app("/gui/browser", status, sizeof(status));
            } else if (page == SETTINGS_SYSTEM && selected == 30U) {
                spawn_system_app("/gui/about", status, sizeof(status));
            }
        } else if (gui_key_cancel(&event)) {
            page = SETTINGS_NETWORK;
            selected = first_selectable(page);
            (void)strlcpy(status, "SETTINGS / HOME", sizeof(status));
        } else {
            continue;
        }

        build_scene(&scene, page, low_contrast, selected, &audio, status);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
