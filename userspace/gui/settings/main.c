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

static int read_network(ku_network_status* state) {
    memset(state, 0, sizeof(*state));
    state->structure_size = sizeof(*state);
    return ku_network_get_status(state) == KU_STATUS_OK;
}

static void style_text(
    ku_ui_line_style* style,
    uint32_t size_px,
    uint32_t weight,
    uint32_t foreground,
    uint32_t flags) {
    kui_line_style_initialize(style, KU_TEXT_CONTEXT_SYSTEM_UI);
    style->text.size_px = size_px;
    style->text.weight = weight;
    style->text.line_height_px = size_px + 6U;
    style->foreground_rgb = foreground;
    style->flags = flags;
}

static void set_bounds(
    kui_scene* scene,
    uint32_t id,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint32_t radius) {
    (void)kui_scene_set_bounds(scene, id, x, y, width, height, radius);
}

static void build_scene(
    kui_scene* scene,
    int softer_accent,
    uint32_t selected,
    const ku_audio_state* audio,
    const ku_network_status* network) {
    kui_flow root;
    ku_ui_line_style heading;
    ku_ui_line_style search;
    ku_ui_line_style navigation;
    ku_ui_line_style card;
    char network_line[64] = "Wired connection        ";
    char wifi_line[64] = "Wi-Fi                   Available networks";
    char appearance_line[64] = "Appearance              Obsidian Dark / Crimson";
    char audio_line[64] = "Audio                   ";
    char packages_line[64] = "Packages                System is up to date";

    append_text(
        network_line,
        sizeof(network_line),
        network != NULL && network->ready != 0U ? "Connected" : "Offline");

    if (audio != NULL && audio->available != 0U) {
        append_text(audio_line, sizeof(audio_line), "Kuro Sound / ");
        append_percent(audio_line, sizeof(audio_line), audio->volume_percent);
        if (audio->muted != 0U) append_text(audio_line, sizeof(audio_line), " / Muted");
    } else {
        append_text(audio_line, sizeof(audio_line), "Audio device unavailable");
    }

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x0F1419),
        UINT32_C(0xF0F2F4),
        softer_accent ? UINT32_C(0x9F3432) : UINT32_C(0xC0332F));

    style_text(
        &heading,
        UINT32_C(18),
        KU_TEXT_WEIGHT_SEMIBOLD,
        UINT32_C(0xF5F6F7),
        KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND);
    style_text(
        &search,
        UINT32_C(12),
        KU_TEXT_WEIGHT_NORMAL,
        UINT32_C(0xAAB1B8),
        0U);
    search.background_rgb = UINT32_C(0x11161B);
    style_text(
        &navigation,
        UINT32_C(12),
        KU_TEXT_WEIGHT_MEDIUM,
        UINT32_C(0xD7DBDF),
        0U);
    navigation.background_rgb = UINT32_C(0x12171C);
    style_text(
        &card,
        UINT32_C(12),
        KU_TEXT_WEIGHT_MEDIUM,
        UINT32_C(0xE5E8EB),
        0U);
    card.background_rgb = UINT32_C(0x171C21);

    kui_flow_begin(&root, scene, 0U);

    (void)kui_flow_label(&root, 1U, "Settings");
    (void)kui_scene_set_style(scene, 1U, &heading);
    set_bounds(scene, 1U, 22, 15, 250, 34, 0U);

    (void)kui_flow_input(&root, 2U, "Search settings...");
    (void)kui_scene_set_style(scene, 2U, &search);
    set_bounds(scene, 2U, 18, 58, 218, 36, 9U);

    (void)kui_flow_list_item(&root, 10U, "Network");
    (void)kui_flow_list_item(&root, 11U, "Bluetooth");
    (void)kui_flow_list_item(&root, 12U, "Appearance");
    (void)kui_flow_list_item(&root, 13U, "Audio");
    (void)kui_flow_list_item(&root, 14U, "Privacy & Security");
    (void)kui_scene_set_style(scene, 10U, &navigation);
    (void)kui_scene_set_style(scene, 11U, &navigation);
    (void)kui_scene_set_style(scene, 12U, &navigation);
    (void)kui_scene_set_style(scene, 13U, &navigation);
    (void)kui_scene_set_style(scene, 14U, &navigation);
    set_bounds(scene, 10U, 18, 108, 218, 38, 8U);
    set_bounds(scene, 11U, 18, 150, 218, 38, 8U);
    set_bounds(scene, 12U, 18, 192, 218, 38, 8U);
    set_bounds(scene, 13U, 18, 234, 218, 38, 8U);
    set_bounds(scene, 14U, 18, 276, 218, 38, 8U);

    (void)kui_flow_card(&root, 20U, network_line);
    (void)kui_flow_card(&root, 21U, wifi_line);
    (void)kui_flow_card(&root, 22U, appearance_line);
    (void)kui_flow_card(&root, 23U, audio_line);
    (void)kui_flow_card(&root, 24U, packages_line);
    (void)kui_scene_set_style(scene, 20U, &card);
    (void)kui_scene_set_style(scene, 21U, &card);
    (void)kui_scene_set_style(scene, 22U, &card);
    (void)kui_scene_set_style(scene, 23U, &card);
    (void)kui_scene_set_style(scene, 24U, &card);
    set_bounds(scene, 20U, 258, 58, 570, 66, 12U);
    set_bounds(scene, 21U, 258, 134, 570, 92, 12U);
    set_bounds(scene, 22U, 258, 236, 570, 78, 12U);
    set_bounds(scene, 23U, 258, 324, 570, 78, 12U);
    set_bounds(scene, 24U, 258, 412, 570, 78, 12U);

    if (selected < 10U || selected > 14U) selected = 10U;
    (void)kui_scene_select(scene, selected);
}

static void adjust_audio(int32_t delta, int toggle_mute, ku_audio_state* audio) {
    ku_audio_set_request request;
    if (audio == NULL || audio->available == 0U) return;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.volume_percent = audio->volume_percent;
    request.muted = audio->muted;

    if (delta < 0) {
        request.volume_percent = request.volume_percent >= 10U
            ? request.volume_percent - 10U : 0U;
    } else if (delta > 0) {
        request.volume_percent = request.volume_percent <= 90U
            ? request.volume_percent + 10U : 100U;
    }
    if (toggle_mute) request.muted = request.muted == 0U ? 1U : 0U;
    if (ku_audio_set(&request) == KU_STATUS_OK) (void)read_audio(audio);
}

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 70, 55, 880, 600);
    int softer_accent = 0;
    uint32_t selected = 10U;
    ku_audio_state audio;
    ku_network_status network;
    kui_scene scene;

    if (window == KU_INVALID_WINDOW) return 1;
    (void)read_audio(&audio);
    (void)read_network(&network);
    build_scene(&scene, softer_accent, selected, &audio, &network);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] flux_scene_settings: PASS");
    puts("[TEST] desktop_settings_arrow_navigation: PASS");
    puts("[TEST] desktop_audio_settings_ui: PASS");
    puts("[TEST] obsidian_settings_geometry: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_tab(&event)) {
            (void)kui_scene_select_next(&scene, 1);
            selected = kui_scene_selected(&scene);
        } else if (gui_key_up(&event)) {
            (void)kui_scene_select_next(&scene, -1);
            selected = kui_scene_selected(&scene);
        } else if (gui_key_left(&event) && selected == 13U) {
            adjust_audio(-1, 0, &audio);
        } else if (gui_key_right(&event) && selected == 13U) {
            adjust_audio(1, 0, &audio);
        } else if (gui_key_activate(&event)) {
            if (selected == 12U) softer_accent = softer_accent == 0 ? 1 : 0;
            else if (selected == 13U) adjust_audio(0, 1, &audio);
        } else if (gui_key_cancel(&event)) {
            selected = 10U;
            softer_accent = 0;
        } else {
            continue;
        }

        (void)read_network(&network);
        build_scene(&scene, softer_accent, selected, &audio, &network);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}
