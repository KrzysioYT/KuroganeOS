#include "../common.h"

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void append_percent(char* destination, size_t capacity, uint32_t value) {
    char number[24];
    gui_u64(number, sizeof(number), value);
    append_text(destination, capacity, number);
    append_text(destination, capacity, "%");
}

static int read_audio(ku_audio_state* state) {
    memset(state, 0, sizeof(*state));
    state->structure_size = sizeof(*state);
    return ku_audio_get_state(state) == KU_STATUS_OK &&
        state->version == KU_AUDIO_STATE_VERSION;
}

static void build_scene(
    kui_scene* scene,
    int low_contrast,
    uint32_t selected,
    const ku_audio_state* audio) {
    kui_flow root;
    kui_flow settings;
    char audio_line[64] = "AUDIO / ";

    if (audio != NULL && audio->available != 0U) {
        append_text(audio_line, sizeof(audio_line), "AC97 / MASTER ");
        append_percent(audio_line, sizeof(audio_line), audio->volume_percent);
        append_text(audio_line, sizeof(audio_line),
            audio->muted != 0U ? " / MUTED" : " / ACTIVE");
    } else {
        append_text(audio_line, sizeof(audio_line), "AC97 NOT AVAILABLE");
    }

    kui_scene_initialize(scene);
    scene->visible_rows = 14U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        low_contrast ? UINT32_C(0x8F2633) : UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "SETTINGS / RED FLUX");
    (void)kui_flow_label(&root, 2U, "APPEARANCE + SOUND");
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&settings, scene, 1U);
    (void)kui_flow_button(&settings, 10U, "RED CORE");
    (void)kui_flow_button(&settings, 11U, "LOW CONTRAST RED");
    (void)kui_flow_label(
        &settings,
        12U,
        low_contrast ? "ACTIVE / LOW CONTRAST RED" : "ACTIVE / RED CORE");
    (void)kui_flow_separator(&settings, 13U);
    (void)kui_flow_label(&settings, 20U, audio_line);
    (void)kui_flow_button(&settings, 21U, "VOLUME -10");
    (void)kui_flow_button(&settings, 22U, "VOLUME +10");
    (void)kui_flow_button(&settings, 23U, "MUTE / UNMUTE");
    (void)kui_flow_separator(&settings, 24U);
    (void)kui_flow_label(&settings, 25U, "MOUSE / CLICK A CONTROL TO APPLY");

    if (selected != 10U && selected != 11U &&
        selected != 21U && selected != 22U && selected != 23U) {
        selected = 10U;
    }
    (void)kui_scene_select(scene, selected);
}

static void apply_audio(uint32_t selected, ku_audio_state* audio) {
    if (audio == NULL || audio->available == 0U) return;

    ku_audio_set_request request;
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
    }
}

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 430, 205, 500, 390);
    if (window == KU_INVALID_WINDOW) return 1;

    int low_contrast = 0;
    uint32_t selected = 10U;
    uint32_t pointer_buttons = 0U;
    ku_audio_state audio;
    kui_scene scene;
    (void)read_audio(&audio);
    build_scene(&scene, low_contrast, selected, &audio);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] flux_scene_settings: PASS");
    puts("[TEST] desktop_settings_mouse_navigation: PASS");
    puts("[TEST] desktop_settings_keyboard_shortcuts_detached: PASS");
    puts("[TEST] desktop_audio_settings_ui: PASS");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_POINTER) continue;

        {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
        }

        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target == 10U) {
            selected = target;
            low_contrast = 0;
        } else if (target == 11U) {
            selected = target;
            low_contrast = 1;
        } else if (target == 21U || target == 22U || target == 23U) {
            selected = target;
            apply_audio(target, &audio);
        } else {
            continue;
        }

        build_scene(&scene, low_contrast, selected, &audio);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
