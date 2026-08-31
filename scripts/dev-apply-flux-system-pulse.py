#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


# Public Native UI transport: v3 adds METRIC while keeping the exact packet size.
replace_once(
    "sdk/include/kurogane/ui.h",
    "#define KU_UI_NATIVE_VERSION_1 UINT32_C(1)\n#define KU_UI_NATIVE_VERSION_2 UINT32_C(2)\n#define KU_UI_NATIVE_VERSION KU_UI_NATIVE_VERSION_2\n",
    "#define KU_UI_NATIVE_VERSION_1 UINT32_C(1)\n#define KU_UI_NATIVE_VERSION_2 UINT32_C(2)\n#define KU_UI_NATIVE_VERSION_3 UINT32_C(3)\n#define KU_UI_NATIVE_VERSION KU_UI_NATIVE_VERSION_3\n",
)
replace_once(
    "sdk/include/kurogane/ui.h",
    "    KU_UI_NATIVE_SEPARATOR = 7,\n    KU_UI_NATIVE_TILE = 8\n};\n",
    "    KU_UI_NATIVE_SEPARATOR = 7,\n    KU_UI_NATIVE_TILE = 8,\n    KU_UI_NATIVE_METRIC = 9\n};\n",
)

# libui source-facing view type and constructors.
replace_once(
    "sdk/include/kurogane/libui.h",
    "    KUI_VIEW_SEPARATOR = 7,\n    KUI_VIEW_TILE = 8\n};\n",
    "    KUI_VIEW_SEPARATOR = 7,\n    KUI_VIEW_TILE = 8,\n    KUI_VIEW_METRIC = 9\n};\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "ku_status_t kui_scene_add_progress(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\n",
    "ku_status_t kui_scene_add_progress(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\nku_status_t kui_scene_add_metric(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\n",
)
replace_once(
    "sdk/include/kurogane/libui.h",
    "ku_status_t kui_flow_progress(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\nku_status_t kui_flow_separator(kui_flow* flow, uint32_t id);\n",
    "ku_status_t kui_flow_progress(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\nku_status_t kui_flow_metric(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum);\nku_status_t kui_flow_separator(kui_flow* flow, uint32_t id);\n",
)

replace_once(
    "sdk/src/libui.c",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_TILE) {\n",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_METRIC) {\n",
)
replace_once(
    "sdk/src/libui.c",
    "ku_status_t kui_scene_set_text(\n",
    "ku_status_t kui_scene_add_metric(\n    kui_scene* scene,\n    uint32_t id,\n    uint32_t parent_id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum) {\n    ku_status_t status = kui_scene_add(\n        scene, id, parent_id, KUI_VIEW_METRIC, text);\n    if (status != KU_STATUS_OK) return status;\n    return kui_scene_set_value(scene, id, value, maximum);\n}\n\nku_status_t kui_scene_set_text(\n",
)
replace_once(
    "sdk/src/libui.c",
    "    if (view == (kui_view*)0 || view->type != KUI_VIEW_PROGRESS || maximum == 0U) {\n",
    "    if (view == (kui_view*)0 ||\n        (view->type != KUI_VIEW_PROGRESS && view->type != KUI_VIEW_METRIC) ||\n        maximum == 0U) {\n",
)
replace_once(
    "sdk/src/libui.c",
    "typedef struct kui_native_layout_state {\n    int32_t cursor_y;\n    uint32_t tile_column;\n} kui_native_layout_state;\n\n#define KUI_TILE_COLUMNS 3U\n#define KUI_TILE_WIDTH 184\n#define KUI_TILE_HEIGHT 68\n#define KUI_TILE_GAP_X 12\n#define KUI_TILE_GAP_Y 8\n",
    "typedef struct kui_native_layout_state {\n    int32_t cursor_y;\n    uint32_t tile_column;\n    uint32_t metric_column;\n} kui_native_layout_state;\n\n#define KUI_TILE_COLUMNS 3U\n#define KUI_TILE_WIDTH 184\n#define KUI_TILE_HEIGHT 68\n#define KUI_TILE_GAP_X 12\n#define KUI_TILE_GAP_Y 8\n#define KUI_METRIC_COLUMNS 5U\n#define KUI_METRIC_WIDTH 112\n#define KUI_METRIC_HEIGHT 58\n#define KUI_METRIC_GAP_X 8\n#define KUI_METRIC_GAP_Y 8\n",
)
replace_once(
    "sdk/src/libui.c",
    "        case KUI_VIEW_SEPARATOR: return 10;\n        case KUI_VIEW_TILE: return KUI_TILE_HEIGHT;\n",
    "        case KUI_VIEW_SEPARATOR: return 10;\n        case KUI_VIEW_TILE: return KUI_TILE_HEIGHT;\n        case KUI_VIEW_METRIC: return KUI_METRIC_HEIGHT;\n",
)
replace_once(
    "sdk/src/libui.c",
    "static void native_layout_initialize(kui_native_layout_state* state) {\n    state->cursor_y = 16;\n    state->tile_column = 0U;\n}\n\nstatic void native_flush_tiles(kui_native_layout_state* state) {\n    if (state->tile_column == 0U) return;\n    state->cursor_y += KUI_TILE_HEIGHT + KUI_TILE_GAP_Y;\n    state->tile_column = 0U;\n}\n",
    "static void native_layout_initialize(kui_native_layout_state* state) {\n    state->cursor_y = 16;\n    state->tile_column = 0U;\n    state->metric_column = 0U;\n}\n\nstatic void native_flush_tiles(kui_native_layout_state* state) {\n    if (state->tile_column == 0U) return;\n    state->cursor_y += KUI_TILE_HEIGHT + KUI_TILE_GAP_Y;\n    state->tile_column = 0U;\n}\n\nstatic void native_flush_metrics(kui_native_layout_state* state) {\n    if (state->metric_column == 0U) return;\n    state->cursor_y += KUI_METRIC_HEIGHT + KUI_METRIC_GAP_Y;\n    state->metric_column = 0U;\n}\n",
)
replace_once(
    "sdk/src/libui.c",
    "    if (view->type == KUI_VIEW_TILE) {\n        if (state->tile_column >= KUI_TILE_COLUMNS) native_flush_tiles(state);\n        output->x = 16 + indent +\n            (int32_t)state->tile_column * (KUI_TILE_WIDTH + KUI_TILE_GAP_X);\n        output->y = state->cursor_y;\n        output->width = KUI_TILE_WIDTH;\n        output->height = KUI_TILE_HEIGHT;\n        ++state->tile_column;\n        if (state->tile_column == KUI_TILE_COLUMNS) native_flush_tiles(state);\n        return;\n    }\n\n    native_flush_tiles(state);\n",
    "    if (view->type == KUI_VIEW_METRIC) {\n        native_flush_tiles(state);\n        if (state->metric_column >= KUI_METRIC_COLUMNS) native_flush_metrics(state);\n        output->x = 16 + indent +\n            (int32_t)state->metric_column * (KUI_METRIC_WIDTH + KUI_METRIC_GAP_X);\n        output->y = state->cursor_y;\n        output->width = KUI_METRIC_WIDTH;\n        output->height = KUI_METRIC_HEIGHT;\n        ++state->metric_column;\n        if (state->metric_column == KUI_METRIC_COLUMNS) native_flush_metrics(state);\n        return;\n    }\n    if (view->type == KUI_VIEW_TILE) {\n        native_flush_metrics(state);\n        if (state->tile_column >= KUI_TILE_COLUMNS) native_flush_tiles(state);\n        output->x = 16 + indent +\n            (int32_t)state->tile_column * (KUI_TILE_WIDTH + KUI_TILE_GAP_X);\n        output->y = state->cursor_y;\n        output->width = KUI_TILE_WIDTH;\n        output->height = KUI_TILE_HEIGHT;\n        ++state->tile_column;\n        if (state->tile_column == KUI_TILE_COLUMNS) native_flush_tiles(state);\n        return;\n    }\n\n    native_flush_metrics(state);\n    native_flush_tiles(state);\n",
)
replace_once(
    "sdk/src/libui.c",
    "ku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {\n",
    "ku_status_t kui_flow_metric(\n    kui_flow* flow,\n    uint32_t id,\n    const char* text,\n    uint32_t value,\n    uint32_t maximum) {\n    return flow == (kui_flow*)0\n        ? KU_STATUS_INVALID_ARGUMENT\n        : kui_scene_add_metric(\n            flow->scene, id, flow->parent_id, text, value, maximum);\n}\n\nku_status_t kui_flow_separator(kui_flow* flow, uint32_t id) {\n",
)

# Kernel validation stays backward-compatible with native v1/v2.
replace_once(
    "kernel/user/runtime_base.inc",
    "    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_2\n        ? KU_UI_NATIVE_TILE : KU_UI_NATIVE_SEPARATOR;\n",
    "    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_3\n        ? KU_UI_NATIVE_METRIC\n        : (version >= KU_UI_NATIVE_VERSION_2\n            ? KU_UI_NATIVE_TILE : KU_UI_NATIVE_SEPARATOR);\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "    if (command.type == KU_UI_NATIVE_PROGRESS && command.maximum == 0U) return false;\n",
    "    if ((command.type == KU_UI_NATIVE_PROGRESS ||\n         command.type == KU_UI_NATIVE_METRIC) && command.maximum == 0U) return false;\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "        (frame.version != KU_UI_NATIVE_VERSION_1 &&\n         frame.version != KU_UI_NATIVE_VERSION_2) ||\n",
    "        (frame.version != KU_UI_NATIVE_VERSION_1 &&\n         frame.version != KU_UI_NATIVE_VERSION_2 &&\n         frame.version != KU_UI_NATIVE_VERSION_3) ||\n",
)
replace_once(
    "kernel/user/runtime_base.inc",
    "            case KU_UI_NATIVE_SEPARATOR:\n                ui::separator(bounds.x, bounds.y + bounds.height / 2, bounds.width);\n                break;\n            case KU_UI_NATIVE_TILE: {\n",
    "            case KU_UI_NATIVE_SEPARATOR:\n                ui::separator(bounds.x, bounds.y + bounds.height / 2, bounds.width);\n                break;\n            case KU_UI_NATIVE_METRIC: {\n                char title[32];\n                char detail[32];\n                split_native_tile_text(command.text, title, detail);\n                ui::metric_card(\n                    bounds, title, detail, command.value, command.maximum);\n                break;\n            }\n            case KU_UI_NATIVE_TILE: {\n",
)

# Renderer primitive for compact system dashboard cards.
replace_once(
    "kernel/ui/ui.hpp",
    "void app_tile(\n",
    "void metric_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t value, uint32_t maximum);\nvoid app_tile(\n",
)
replace_once(
    "kernel/ui/ui.cpp",
    "void app_tile(\n",
    "void metric_card(\n    const Rect& bounds, const char* title, const char* detail,\n    uint32_t value, uint32_t maximum) {\n    if (bounds.width <= 0 || bounds.height <= 0 || maximum == 0U) return;\n    if (value > maximum) value = maximum;\n    const graphics::Color background = kGraphite;\n    const uint32_t percent = static_cast<uint32_t>(\n        (static_cast<uint64_t>(value) * 100U) / maximum);\n    const graphics::Color signal = value == 0U\n        ? kSteel : (percent >= 80U ? kRedBright : kRedMuted);\n    graphics::fill_rect(bounds.x + 3, bounds.y + 3,\n                        bounds.width, bounds.height, kSurfaceShadow);\n    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);\n    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);\n    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);\n    graphics::draw_text(bounds.x + 10, bounds.y + 9,\n                        title ? title : \"METRIC\", kTheme.text, background, 1U, true);\n    graphics::draw_text(bounds.x + 10, bounds.y + 25,\n                        detail ? detail : \"\", kTheme.text_muted, background, 1U, true);\n    if (bounds.width > 24 && bounds.height > 16) {\n        const int32_t bar_x = bounds.x + 10;\n        const int32_t bar_y = bounds.y + bounds.height - 10;\n        const int32_t bar_width = bounds.width - 20;\n        const int32_t active_width = static_cast<int32_t>(\n            (static_cast<uint64_t>(value) * static_cast<uint64_t>(bar_width)) / maximum);\n        graphics::fill_rect(bar_x, bar_y, bar_width, 3, graphics::rgb(35, 38, 44));\n        if (active_width > 0) {\n            graphics::fill_rect(bar_x, bar_y, active_width, 3, signal);\n        }\n    }\n}\n\nvoid app_tile(\n",
)

# HOME becomes a live dashboard rather than an instruction label over app cards.
replace_once(
    "userspace/gui/launcher/main.c",
    "static void reap_children(void) {\n",
    "static void append_percent(char* destination, size_t capacity, uint32_t value) {\n    char number[24];\n    gui_u64(number, sizeof(number), value > 100U ? 100U : value);\n    append_text(destination, capacity, number);\n    append_text(destination, capacity, \"%\");\n}\n\nstatic int read_system(ku_system_snapshot* snapshot) {\n    memset(snapshot, 0, sizeof(*snapshot));\n    snapshot->structure_size = sizeof(*snapshot);\n    return ku_system_get_snapshot(snapshot) == KU_STATUS_OK &&\n        snapshot->version == KU_SYSTEM_SNAPSHOT_VERSION;\n}\n\nstatic int read_network(ku_network_status* network) {\n    memset(network, 0, sizeof(*network));\n    network->structure_size = sizeof(*network);\n    return ku_network_get_status(network) == KU_STATUS_OK;\n}\n\nstatic int read_audio(ku_audio_state* audio) {\n    memset(audio, 0, sizeof(*audio));\n    audio->structure_size = sizeof(*audio);\n    return ku_audio_get_state(audio) == KU_STATUS_OK &&\n        audio->version == KU_AUDIO_STATE_VERSION;\n}\n\nstatic void reap_children(void) {\n",
)
old_build = '''static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow apps;
    size_t index;
    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FLUX DECK / APPLICATIONS");
    (void)kui_flow_label(&root, 2U, "CLICK A CARD TO OPEN / APPS BUTTON TO HIDE");

    kui_flow_begin(&apps, scene, 1U);
    for (index = 0U; index < APP_COUNT; ++index) {
        char label[64] = "";
        uint32_t flags = 0U;
        append_text(label, sizeof(label), g_apps[index].label);
        append_text(label, sizeof(label), "\\n");
        append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_tile(
            &apps, 10U + (uint32_t)index, label, g_apps[index].desktop_id);
        if (pin_state(g_apps[index].desktop_id)) flags |= KUI_VIEW_PINNED;
        if (app_is_running(g_apps[index].desktop_id)) flags |= KUI_VIEW_RUNNING;
        (void)kui_scene_set_flags(scene, 10U + (uint32_t)index, flags);
    }
    (void)kui_flow_button(&root, 30U, "PIN / UNPIN SELECTED");
    (void)kui_flow_button(&root, 31U, "LOG OUT");
    (void)kui_flow_label(&root, 32U, g_status);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}
'''
new_build = '''static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow apps;
    ku_system_snapshot system;
    ku_network_status network;
    ku_audio_state audio;
    const int system_valid = read_system(&system);
    const int network_valid = read_network(&network);
    const int audio_valid = read_audio(&audio);
    char cpu[64] = "CPU\\n";
    char ram[64] = "RAM\\n";
    char disk[64] = "DISK\\n";
    char net[64] = "NETWORK\\n";
    char sound[64] = "AUDIO\\n";
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
    scene->visible_rows = 15U;
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
        append_text(label, sizeof(label), "\\n");
        append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_tile(
            &apps, 10U + (uint32_t)index, label, g_apps[index].desktop_id);
        if (pin_state(g_apps[index].desktop_id)) flags |= KUI_VIEW_PINNED;
        if (app_is_running(g_apps[index].desktop_id)) flags |= KUI_VIEW_RUNNING;
        (void)kui_scene_set_flags(scene, 10U + (uint32_t)index, flags);
    }
    (void)kui_flow_button(&root, 30U, "PIN / UNPIN SELECTED");
    (void)kui_flow_button(&root, 31U, "LOG OUT");
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}
'''
replace_once("userspace/gui/launcher/main.c", old_build, new_build)
replace_once(
    "userspace/gui/launcher/main.c",
    "    uint32_t pointer_buttons = 0U;\n",
    "    uint32_t pointer_buttons = 0U;\n    uint32_t refresh_ticks = 0U;\n",
)
replace_once(
    "userspace/gui/launcher/main.c",
    "    puts(\"[TEST] red_flux_tile_launcher: PASS\");\n\n    for (;;) {\n        ku_ui_event event;\n        reap_children();\n        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;\n",
    "    puts(\"[TEST] red_flux_tile_launcher: PASS\");\n    puts(\"[TEST] flux_home_system_pulse: PASS\");\n\n    for (;;) {\n        ku_ui_event event;\n        int available;\n        reap_children();\n        available = kui_next_event(window, &event);\n        if (available < 0) break;\n        if (available == 0) {\n            ++refresh_ticks;\n            if (refresh_ticks >= KU_SYSTEM_TICKS_PER_SECOND) {\n                refresh_ticks = 0U;\n                build_scene(&scene);\n                if (kui_scene_present(window, &scene) != KU_STATUS_OK) break;\n            }\n            (void)kuro_sleep(1U);\n            continue;\n        }\n        if (event.type == KU_UI_EVENT_CLOSE) break;\n",
)

# ABI and libui tests explicitly qualify v3 metrics and v1/v2 size compatibility.
replace_once(
    "tests/test_sdk_abi.cpp",
    "    static_assert(KU_UI_NATIVE_VERSION == KU_UI_NATIVE_VERSION_2);\n    static_assert(KU_UI_NATIVE_TILE == 8);\n",
    "    static_assert(KU_UI_NATIVE_VERSION == KU_UI_NATIVE_VERSION_3);\n    static_assert(KU_UI_NATIVE_TILE == 8);\n    static_assert(KU_UI_NATIVE_METRIC == 9);\n",
)
replace_once(
    "tests/test_libui_pointer.c",
    "            tile_frame.version != KU_UI_NATIVE_VERSION_2 ||\n",
    "            tile_frame.version != KU_UI_NATIVE_VERSION_3 ||\n",
)
replace_once(
    "tests/test_libui_pointer.c",
    "    puts(\"libui native packet + mouse hit-test tests passed\");\n",
    "    {\n        kui_scene metrics;\n        kui_flow flow;\n        ku_ui_native_frame metric_frame;\n        kui_scene_initialize(&metrics);\n        metrics.visible_rows = 7U;\n        kui_flow_begin(&flow, &metrics, 0U);\n        if (kui_flow_panel(&flow, 40U, \"PULSE\") != KU_STATUS_OK ||\n            kui_flow_metric(&flow, 41U, \"CPU\\n23%\", 23U, 100U) != KU_STATUS_OK ||\n            kui_flow_metric(&flow, 42U, \"RAM\\n48%\", 48U, 100U) != KU_STATUS_OK ||\n            kui_flow_metric(&flow, 43U, \"DISK\\n7%\", 7U, 100U) != KU_STATUS_OK ||\n            kui_flow_metric(&flow, 44U, \"NETWORK\\nONLINE\", 100U, 100U) != KU_STATUS_OK ||\n            kui_flow_metric(&flow, 45U, \"AUDIO\\n64%\", 64U, 100U) != KU_STATUS_OK ||\n            kui_flow_tile(&flow, 46U, \"FILES\\nROOT\", KU_UI_NATIVE_ICON_FILES) != KU_STATUS_OK) return 9;\n        if (kui_scene_build_native(&metrics, &metric_frame) != KU_STATUS_OK ||\n            metric_frame.version != KU_UI_NATIVE_VERSION_3 ||\n            metric_frame.command_count != 7U ||\n            metric_frame.commands[1].type != KU_UI_NATIVE_METRIC ||\n            metric_frame.commands[5].type != KU_UI_NATIVE_METRIC ||\n            metric_frame.commands[1].value != 23U || metric_frame.commands[1].maximum != 100U ||\n            metric_frame.commands[1].y != metric_frame.commands[2].y ||\n            metric_frame.commands[2].y != metric_frame.commands[3].y ||\n            metric_frame.commands[3].y != metric_frame.commands[4].y ||\n            metric_frame.commands[4].y != metric_frame.commands[5].y ||\n            metric_frame.commands[1].x >= metric_frame.commands[2].x ||\n            metric_frame.commands[2].x >= metric_frame.commands[3].x ||\n            metric_frame.commands[3].x >= metric_frame.commands[4].x ||\n            metric_frame.commands[4].x >= metric_frame.commands[5].x ||\n            metric_frame.commands[6].y <= metric_frame.commands[1].y ||\n            !expect(kui_scene_hit_test(\n                &metrics, metric_frame.commands[3].x + 4,\n                center_y(&metric_frame, 3U)), 0U, \"metric inert\")) return 10;\n    }\n\n    puts(\"libui native packet + mouse hit-test tests passed\");\n",
)
replace_once(
    "tests/test_mouse_first_apps.py",
    "assert \"red_flux_tile_launcher: PASS\" in launcher\n",
    "assert \"red_flux_tile_launcher: PASS\" in launcher\nassert \"kui_flow_metric\" in launcher, \"launcher: System Pulse metric cards missing\"\nassert \"ku_system_get_snapshot\" in launcher, \"launcher: live CPU/RAM/disk source missing\"\nassert \"ku_network_get_status\" in launcher, \"launcher: live network source missing\"\nassert \"ku_audio_get_state\" in launcher, \"launcher: live audio source missing\"\nassert \"flux_home_system_pulse: PASS\" in launcher\nassert \"CLICK A CARD TO OPEN\" not in launcher, \"launcher: instruction-banner UI returned\"\n",
)

# Guard against accidental ABI growth and against incomplete migration.
ui_header = read("sdk/include/kurogane/ui.h")
libui = read("sdk/src/libui.c")
runtime = read("kernel/user/runtime_base.inc")
launcher = read("userspace/gui/launcher/main.c")
for needle in ("KU_UI_NATIVE_VERSION_3", "KU_UI_NATIVE_METRIC = 9"):
    if needle not in ui_header:
        raise SystemExit(f"ui.h: missing {needle}")
for needle in ("KUI_METRIC_COLUMNS 5U", "kui_flow_metric", "KUI_VIEW_METRIC"):
    if needle not in libui:
        raise SystemExit(f"libui.c: missing {needle}")
for needle in ("KU_UI_NATIVE_VERSION_1", "KU_UI_NATIVE_VERSION_2", "KU_UI_NATIVE_VERSION_3"):
    if needle not in runtime:
        raise SystemExit(f"runtime: compatibility gate missing {needle}")
for needle in ("FLUX HOME / SYSTEM PULSE", "flux_home_system_pulse: PASS", "kui_flow_metric"):
    if needle not in launcher:
        raise SystemExit(f"launcher: missing {needle}")

print("Flux System Pulse / Native UI v3 migration applied")
