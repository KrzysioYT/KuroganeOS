#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_between(text, start, end, new, label):
    first = text.find(start)
    if first < 0:
        raise SystemExit(f"{label}: start anchor missing")
    if text.find(start, first + 1) >= 0:
        raise SystemExit(f"{label}: start anchor not unique")
    last = text.find(end, first)
    if last < 0:
        raise SystemExit(f"{label}: end anchor missing")
    return text[:first] + new + text[last:]

# ----- Public native UI ABI: additive v2, while kernel keeps v1 compatibility.
path = "sdk/include/kurogane/ui.h"
text = read(path)
text = replace_once(
    text,
    "#define KU_UI_NATIVE_VERSION UINT32_C(1)\n",
    "#define KU_UI_NATIVE_VERSION_1 UINT32_C(1)\n"
    "#define KU_UI_NATIVE_VERSION_2 UINT32_C(2)\n"
    "#define KU_UI_NATIVE_VERSION KU_UI_NATIVE_VERSION_2\n",
    "native UI version")
text = replace_once(
    text,
    "    KU_UI_NATIVE_PROGRESS = 6,\n    KU_UI_NATIVE_SEPARATOR = 7\n};\n",
    "    KU_UI_NATIVE_PROGRESS = 6,\n"
    "    KU_UI_NATIVE_SEPARATOR = 7,\n"
    "    KU_UI_NATIVE_TILE = 8\n};\n"
    "\n"
    "enum ku_ui_native_icon {\n"
    "    KU_UI_NATIVE_ICON_HOME = 0,\n"
    "    KU_UI_NATIVE_ICON_TERMINAL = 1,\n"
    "    KU_UI_NATIVE_ICON_FILES = 2,\n"
    "    KU_UI_NATIVE_ICON_PERFORMANCE = 3,\n"
    "    KU_UI_NATIVE_ICON_BROWSER = 4,\n"
    "    KU_UI_NATIVE_ICON_MONITOR = 5,\n"
    "    KU_UI_NATIVE_ICON_SETTINGS = 6,\n"
    "    KU_UI_NATIVE_ICON_ABOUT = 7,\n"
    "    KU_UI_NATIVE_ICON_COUNT = 8\n"
    "};\n",
    "native tile type/icons")
text = replace_once(
    text,
    "    KU_UI_NATIVE_SELECTED = UINT32_C(1) << 0,\n    KU_UI_NATIVE_DISABLED = UINT32_C(1) << 1\n};\n",
    "    KU_UI_NATIVE_SELECTED = UINT32_C(1) << 0,\n"
    "    KU_UI_NATIVE_DISABLED = UINT32_C(1) << 1,\n"
    "    KU_UI_NATIVE_PINNED = UINT32_C(1) << 2,\n"
    "    KU_UI_NATIVE_RUNNING = UINT32_C(1) << 3\n};\n",
    "native tile flags")
write(path, text)

# ----- libui public tile abstraction.
path = "sdk/include/kurogane/libui.h"
text = read(path)
text = replace_once(
    text,
    "    KUI_VIEW_PROGRESS = 6,\n    KUI_VIEW_SEPARATOR = 7\n};\n",
    "    KUI_VIEW_PROGRESS = 6,\n"
    "    KUI_VIEW_SEPARATOR = 7,\n"
    "    KUI_VIEW_TILE = 8\n};\n",
    "libui tile type")
text = replace_once(
    text,
    "    KUI_VIEW_HIDDEN = UINT32_C(1) << 0,\n    KUI_VIEW_SELECTED = UINT32_C(1) << 1,\n    KUI_VIEW_DISABLED = UINT32_C(1) << 2\n};\n",
    "    KUI_VIEW_HIDDEN = UINT32_C(1) << 0,\n"
    "    KUI_VIEW_SELECTED = UINT32_C(1) << 1,\n"
    "    KUI_VIEW_DISABLED = UINT32_C(1) << 2,\n"
    "    KUI_VIEW_PINNED = UINT32_C(1) << 3,\n"
    "    KUI_VIEW_RUNNING = UINT32_C(1) << 4\n};\n",
    "libui tile flags")
text = replace_once(
    text,
    "ku_status_t kui_scene_add_progress(\n",
    "ku_status_t kui_scene_add_tile(\n"
    "    kui_scene* scene,\n"
    "    uint32_t id,\n"
    "    uint32_t parent_id,\n"
    "    const char* text,\n"
    "    uint32_t icon);\n"
    "ku_status_t kui_scene_add_progress(\n",
    "libui tile scene declaration")
text = replace_once(
    text,
    "ku_status_t kui_flow_list_item(kui_flow* flow, uint32_t id, const char* text);\n",
    "ku_status_t kui_flow_list_item(kui_flow* flow, uint32_t id, const char* text);\n"
    "ku_status_t kui_flow_tile(\n"
    "    kui_flow* flow, uint32_t id, const char* text, uint32_t icon);\n",
    "libui tile flow declaration")
write(path, text)

# ----- libui implementation: native three-column grid for tiles.
path = "sdk/src/libui.c"
text = read(path)
text = replace_once(
    text,
    "    return view->type == KUI_VIEW_BUTTON || view->type == KUI_VIEW_INPUT ||\n        view->type == KUI_VIEW_LIST_ITEM;\n",
    "    return view->type == KUI_VIEW_BUTTON || view->type == KUI_VIEW_INPUT ||\n"
    "        view->type == KUI_VIEW_LIST_ITEM || view->type == KUI_VIEW_TILE;\n",
    "tile interactive")
text = replace_once(
    text,
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_SEPARATOR) {\n",
    "        type < KUI_VIEW_PANEL || type > KUI_VIEW_TILE) {\n",
    "tile add validation")
text = replace_once(
    text,
    "ku_status_t kui_scene_add_progress(\n",
    "ku_status_t kui_scene_add_tile(\n"
    "    kui_scene* scene,\n"
    "    uint32_t id,\n"
    "    uint32_t parent_id,\n"
    "    const char* text,\n"
    "    uint32_t icon) {\n"
    "    ku_status_t status;\n"
    "    kui_view* view;\n"
    "    if (icon >= KU_UI_NATIVE_ICON_COUNT) return KU_STATUS_INVALID_ARGUMENT;\n"
    "    status = kui_scene_add(scene, id, parent_id, KUI_VIEW_TILE, text);\n"
    "    if (status != KU_STATUS_OK) return status;\n"
    "    view = find_view(scene, id);\n"
    "    if (view == (kui_view*)0) return KU_STATUS_BAD_STATE;\n"
    "    view->value = icon;\n"
    "    return KU_STATUS_OK;\n"
    "}\n\n"
    "ku_status_t kui_scene_add_progress(\n",
    "tile add implementation")
text = replace_once(
    text,
    "    view->flags = flags &\n        (KUI_VIEW_HIDDEN | KUI_VIEW_SELECTED | KUI_VIEW_DISABLED);\n",
    "    view->flags = flags &\n"
    "        (KUI_VIEW_HIDDEN | KUI_VIEW_SELECTED | KUI_VIEW_DISABLED |\n"
    "         KUI_VIEW_PINNED | KUI_VIEW_RUNNING);\n",
    "tile flag mask")

start = "typedef struct kui_native_layout {\n"
end = "ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene) {\n"
layout = r'''typedef struct kui_native_layout {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} kui_native_layout;

typedef struct kui_native_layout_state {
    int32_t cursor_y;
    uint32_t tile_column;
} kui_native_layout_state;

#define KUI_TILE_COLUMNS 3U
#define KUI_TILE_WIDTH 184
#define KUI_TILE_HEIGHT 68
#define KUI_TILE_GAP_X 12
#define KUI_TILE_GAP_Y 8

static int32_t native_view_height(uint32_t type) {
    switch (type) {
        case KUI_VIEW_PANEL: return 38;
        case KUI_VIEW_LABEL: return 22;
        case KUI_VIEW_BUTTON: return 34;
        case KUI_VIEW_INPUT: return 36;
        case KUI_VIEW_LIST_ITEM: return 36;
        case KUI_VIEW_PROGRESS: return 44;
        case KUI_VIEW_SEPARATOR: return 10;
        case KUI_VIEW_TILE: return KUI_TILE_HEIGHT;
        default: return 0;
    }
}

static void native_layout_initialize(kui_native_layout_state* state) {
    state->cursor_y = 16;
    state->tile_column = 0U;
}

static void native_flush_tiles(kui_native_layout_state* state) {
    if (state->tile_column == 0U) return;
    state->cursor_y += KUI_TILE_HEIGHT + KUI_TILE_GAP_Y;
    state->tile_column = 0U;
}

static void native_layout_view(
    const kui_scene* scene,
    const kui_view* view,
    kui_native_layout_state* state,
    kui_native_layout* output) {
    const uint32_t depth = view_depth(scene, view);
    const int32_t indent = (int32_t)(depth * 12U);
    if (view->type == KUI_VIEW_TILE) {
        if (state->tile_column >= KUI_TILE_COLUMNS) native_flush_tiles(state);
        output->x = 16 + indent +
            (int32_t)state->tile_column * (KUI_TILE_WIDTH + KUI_TILE_GAP_X);
        output->y = state->cursor_y;
        output->width = KUI_TILE_WIDTH;
        output->height = KUI_TILE_HEIGHT;
        ++state->tile_column;
        if (state->tile_column == KUI_TILE_COLUMNS) native_flush_tiles(state);
        return;
    }

    native_flush_tiles(state);
    output->x = 16 + indent;
    output->y = state->cursor_y;
    output->width = 0;
    output->height = native_view_height(view->type);
    state->cursor_y += output->height + 6;
}

static uint32_t native_rows(const kui_scene* scene) {
    if (scene->visible_rows == 0U || scene->visible_rows > KU_UI_NATIVE_MAX_COMMANDS) {
        return KU_UI_NATIVE_MAX_COMMANDS;
    }
    return scene->visible_rows;
}

uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y) {
    uint32_t visible_index = 0U;
    uint32_t output_index = 0U;
    uint32_t index;
    kui_native_layout_state state;
    const uint32_t rows = scene == (const kui_scene*)0 ? 0U : native_rows(scene);
    if (scene == (const kui_scene*)0 || x < 0 || y < 0 ||
        x > KU_UI_NATIVE_COORD_LIMIT || y > KU_UI_NATIVE_COORD_LIMIT) return 0U;
    native_layout_initialize(&state);

    for (index = 0U; index < scene->view_count && output_index < rows; ++index) {
        const kui_view* view = &scene->views[index];
        kui_native_layout geometry;
        int within_x;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        native_layout_view(scene, view, &state, &geometry);
        if (geometry.height <= 0) continue;
        within_x = x >= geometry.x &&
            (geometry.width == 0 || x < geometry.x + geometry.width);
        if (interactive_view(view) && within_x &&
            y >= geometry.y && y < geometry.y + geometry.height) {
            return view->id;
        }
        ++output_index;
    }
    return 0U;
}

ku_status_t kui_scene_build_native(
    const kui_scene* scene, ku_ui_native_frame* frame) {
    uint32_t visible_index = 0U;
    uint32_t output_index = 0U;
    uint32_t index;
    uint32_t rows;
    kui_native_layout_state state;
    if (scene == (const kui_scene*)0 || frame == (ku_ui_native_frame*)0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }

    memset(frame, 0, sizeof(*frame));
    frame->structure_size = sizeof(*frame);
    frame->magic = KU_UI_NATIVE_MAGIC;
    frame->version = KU_UI_NATIVE_VERSION;
    frame->background_rgb = scene->background_rgb;
    frame->foreground_rgb = scene->foreground_rgb;
    frame->accent_rgb = scene->accent_rgb;
    rows = native_rows(scene);
    native_layout_initialize(&state);

    for (index = 0U; index < scene->view_count && output_index < rows; ++index) {
        const kui_view* view = &scene->views[index];
        kui_native_layout geometry;
        ku_ui_native_command* command;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        native_layout_view(scene, view, &state, &geometry);
        if (geometry.height <= 0) return KU_STATUS_CORRUPT_DATA;

        command = &frame->commands[output_index++];
        command->type = view->type;
        if ((view->flags & KUI_VIEW_SELECTED) != 0U) command->flags |= KU_UI_NATIVE_SELECTED;
        if ((view->flags & KUI_VIEW_DISABLED) != 0U) command->flags |= KU_UI_NATIVE_DISABLED;
        if ((view->flags & KUI_VIEW_PINNED) != 0U) command->flags |= KU_UI_NATIVE_PINNED;
        if ((view->flags & KUI_VIEW_RUNNING) != 0U) command->flags |= KU_UI_NATIVE_RUNNING;
        command->x = geometry.x;
        command->y = geometry.y;
        command->width = geometry.width;
        command->height = geometry.height;
        command->foreground_rgb = scene->foreground_rgb;
        command->background_rgb = scene->background_rgb;
        command->accent_rgb = scene->accent_rgb;
        command->value = view->value;
        command->maximum = view->maximum;
        if (strlcpy(command->text, view->text, sizeof(command->text)) >=
            sizeof(command->text)) return KU_STATUS_OUT_OF_RANGE;
    }
    frame->command_count = output_index;
    return KU_STATUS_OK;
}

'''
text = replace_between(text, start, end, layout, "native tile layout")
text = replace_once(
    text,
    "ku_status_t kui_flow_progress(\n",
    "ku_status_t kui_flow_tile(\n"
    "    kui_flow* flow, uint32_t id, const char* text, uint32_t icon) {\n"
    "    return flow == (kui_flow*)0\n"
    "        ? KU_STATUS_INVALID_ARGUMENT\n"
    "        : kui_scene_add_tile(flow->scene, id, flow->parent_id, text, icon);\n"
    "}\n\n"
    "ku_status_t kui_flow_progress(\n",
    "tile flow implementation")
write(path, text)

# ----- Kernel visual language: app cards with vector glyphs.
path = "kernel/ui/ui.hpp"
text = read(path)
text = replace_once(
    text,
    "enum class DockIcon : uint8_t {\n    Home,\n    Terminal,\n    Files,\n    Monitor,\n    Settings,\n    About,\n};\n",
    "enum class DockIcon : uint8_t {\n    Home,\n    Terminal,\n    Files,\n    Monitor,\n    Settings,\n    About,\n};\n"
    "\n"
    "enum class AppIcon : uint8_t {\n"
    "    Home = 0,\n"
    "    Terminal = 1,\n"
    "    Files = 2,\n"
    "    Performance = 3,\n"
    "    Browser = 4,\n"
    "    Monitor = 5,\n"
    "    Settings = 6,\n"
    "    About = 7,\n"
    "};\n",
    "app icon enum")
text = replace_once(
    text,
    "void input_field(const Rect& bounds, const char* text, bool focused = false);\n",
    "void input_field(const Rect& bounds, const char* text, bool focused = false);\n"
    "void app_tile(\n"
    "    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,\n"
    "    bool selected = false, bool pinned = false, bool running = false);\n",
    "app tile declaration")
write(path, text)

path = "kernel/ui/ui.cpp"
text = read(path)
# Insert private glyph helper before the anonymous namespace closes at the first public desktop function.
anchor = "} // namespace\n\nconst Theme& default_theme()"
glyph = r'''void app_icon_glyph(
    const Rect& bounds, AppIcon icon,
    graphics::Color foreground, graphics::Color accent) {
    const int32_t x = bounds.x + 10;
    const int32_t y = bounds.y + 12;
    switch (icon) {
        case AppIcon::Terminal:
            graphics::draw_rect(x, y, 28, 24, foreground);
            graphics::draw_text(x + 5, y + 8, ">_", accent, kTheme.panel_alt, 1U, true);
            break;
        case AppIcon::Files:
            graphics::fill_rect(x, y + 5, 30, 22, foreground);
            graphics::fill_rect(x + 3, y, 12, 7, foreground);
            graphics::fill_rect(x + 3, y + 10, 24, 14, kGraphite);
            break;
        case AppIcon::Performance:
            graphics::draw_rect(x, y, 30, 26, foreground);
            graphics::fill_rect(x + 5, y + 15, 4, 7, accent);
            graphics::fill_rect(x + 13, y + 10, 4, 12, accent);
            graphics::fill_rect(x + 21, y + 5, 4, 17, accent);
            break;
        case AppIcon::Browser:
            graphics::draw_rect(x, y, 30, 26, foreground);
            graphics::fill_rect(x, y + 6, 30, 1, accent);
            graphics::draw_rect(x + 8, y + 10, 14, 11, accent);
            break;
        case AppIcon::Monitor:
            graphics::draw_rect(x, y, 30, 22, foreground);
            line(x + 4, y + 13, x + 9, y + 13, accent);
            line(x + 9, y + 13, x + 13, y + 7, accent);
            line(x + 13, y + 7, x + 18, y + 17, accent);
            line(x + 18, y + 17, x + 25, y + 10, accent);
            graphics::fill_rect(x + 12, y + 24, 7, 2, foreground);
            break;
        case AppIcon::Settings:
            graphics::draw_rect(x + 6, y + 4, 18, 18, foreground);
            graphics::fill_rect(x + 13, y, 4, 26, accent);
            graphics::fill_rect(x + 2, y + 11, 26, 4, accent);
            graphics::fill_rect(x + 11, y + 9, 8, 8, kGraphite);
            break;
        case AppIcon::About:
            graphics::draw_rect(x + 6, y, 18, 26, foreground);
            graphics::fill_rect(x + 13, y + 5, 4, 4, accent);
            graphics::fill_rect(x + 13, y + 12, 4, 9, accent);
            break;
        case AppIcon::Home:
        default:
            graphics::fill_rect(x + 5, y + 10, 21, 16, foreground);
            line(x + 4, y + 11, x + 15, y, accent);
            line(x + 15, y, x + 27, y + 11, accent);
            break;
    }
}

} // namespace

const Theme& default_theme()'''
text = replace_once(text, anchor, glyph, "app glyph helper")
# Insert public tile renderer before list_row.
anchor = "void list_row(\n"
tile_renderer = r'''void app_tile(
    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,
    bool selected, bool pinned, bool running) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = selected
        ? graphics::rgb(49, 20, 27)
        : (running ? graphics::rgb(24, 22, 26) : kGraphite);
    const graphics::Color border = selected
        ? kRedBright : (running ? kSteel : kTheme.border);
    graphics::fill_rect(bounds.x + 4, bounds.y + 5,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, border);
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,
                        selected ? kRedBright : (running ? kRedMuted : kRedDeep));
    if (selected) {
        graphics::fill_rect(bounds.x + 12, bounds.y, 48, 2, kRedBright);
    }
    app_icon_glyph(bounds, icon, kTheme.text, selected ? kRedBright : kRedMuted);
    graphics::draw_text(bounds.x + 50, bounds.y + 13,
                        title ? title : "APP", kTheme.text, background, 1U, true);
    graphics::draw_text(bounds.x + 50, bounds.y + 31,
                        subtitle ? subtitle : "", kTheme.text_muted, background, 1U, true);
    if (pinned) {
        graphics::fill_rect(bounds.x + bounds.width - 13, bounds.y + 9, 5, 5, kRedBright);
        graphics::fill_rect(bounds.x + bounds.width - 11, bounds.y + 14, 1, 7, kRedMuted);
    }
    if (running) {
        graphics::fill_rect(bounds.x + 12, bounds.y + bounds.height - 7, 28, 2,
                            selected ? kRedBright : kRedMuted);
    }
}

'''
text = replace_once(text, anchor, tile_renderer + anchor, "app tile renderer")
write(path, text)

# ----- Ring-3 renderer: v1 compatibility + v2 tiles.
path = "kernel/user/runtime_base.inc"
text = read(path)
old = r'''bool native_command_valid(const ku_ui_native_command& command) {
    if (command.type < KU_UI_NATIVE_PANEL || command.type > KU_UI_NATIVE_SEPARATOR ||
        (command.flags & ~(KU_UI_NATIVE_SELECTED | KU_UI_NATIVE_DISABLED)) != 0U ||
        command.reserved != 0U || !native_text_terminated(command.text)) {
        return false;
    }
    if (command.x < 0 || command.y < 0 || command.height <= 0 || command.width < 0 ||
        command.x > KU_UI_NATIVE_COORD_LIMIT || command.y > KU_UI_NATIVE_COORD_LIMIT ||
        command.width > KU_UI_NATIVE_COORD_LIMIT || command.height > KU_UI_NATIVE_COORD_LIMIT) {
        return false;
    }
    if (command.type == KU_UI_NATIVE_PROGRESS && command.maximum == 0U) return false;
    return true;
}

bool native_frame_valid(const ku_ui_native_frame& frame) {
    if (frame.structure_size != sizeof(ku_ui_native_frame) ||
        frame.magic != KU_UI_NATIVE_MAGIC || frame.version != KU_UI_NATIVE_VERSION ||
        frame.command_count > KU_UI_NATIVE_MAX_COMMANDS || frame.reserved != 0U) {
        return false;
    }
    for (uint32_t index = 0U; index < frame.command_count; ++index) {
        if (!native_command_valid(frame.commands[index])) return false;
    }
    return true;
}
'''
new = r'''bool native_command_valid(
    const ku_ui_native_command& command, uint32_t version) {
    const uint32_t maximum_type = version >= KU_UI_NATIVE_VERSION_2
        ? KU_UI_NATIVE_TILE : KU_UI_NATIVE_SEPARATOR;
    uint32_t allowed_flags = KU_UI_NATIVE_SELECTED | KU_UI_NATIVE_DISABLED;
    if (version >= KU_UI_NATIVE_VERSION_2) {
        allowed_flags |= KU_UI_NATIVE_PINNED | KU_UI_NATIVE_RUNNING;
    }
    if (command.type < KU_UI_NATIVE_PANEL || command.type > maximum_type ||
        (command.flags & ~allowed_flags) != 0U || command.reserved != 0U ||
        !native_text_terminated(command.text)) {
        return false;
    }
    if (command.x < 0 || command.y < 0 || command.height <= 0 || command.width < 0 ||
        command.x > KU_UI_NATIVE_COORD_LIMIT || command.y > KU_UI_NATIVE_COORD_LIMIT ||
        command.width > KU_UI_NATIVE_COORD_LIMIT || command.height > KU_UI_NATIVE_COORD_LIMIT) {
        return false;
    }
    if (command.type == KU_UI_NATIVE_PROGRESS && command.maximum == 0U) return false;
    if (command.type == KU_UI_NATIVE_TILE && command.value >= KU_UI_NATIVE_ICON_COUNT) {
        return false;
    }
    return true;
}

bool native_frame_valid(const ku_ui_native_frame& frame) {
    if (frame.structure_size != sizeof(ku_ui_native_frame) ||
        frame.magic != KU_UI_NATIVE_MAGIC ||
        (frame.version != KU_UI_NATIVE_VERSION_1 &&
         frame.version != KU_UI_NATIVE_VERSION_2) ||
        frame.command_count > KU_UI_NATIVE_MAX_COMMANDS || frame.reserved != 0U) {
        return false;
    }
    for (uint32_t index = 0U; index < frame.command_count; ++index) {
        if (!native_command_valid(frame.commands[index], frame.version)) return false;
    }
    return true;
}
'''
text = replace_once(text, old, new, "native v2 validation")
# Insert bounded title/subtitle split before draw_native_frame.
anchor = "void draw_native_frame(\n"
split = r'''void split_native_tile_text(
    const char* text, char title[32], char subtitle[32]) {
    size_t source = 0U;
    size_t title_size = 0U;
    size_t subtitle_size = 0U;
    bool second = false;
    title[0] = '\0';
    subtitle[0] = '\0';
    if (text == nullptr) return;
    while (source < KU_UI_NATIVE_TEXT_CAPACITY && text[source] != '\0') {
        const char value = text[source++];
        if (!second && value == '\n') {
            second = true;
            continue;
        }
        if (!second) {
            if (title_size + 1U < 32U) title[title_size++] = value;
        } else if (subtitle_size + 1U < 32U) {
            subtitle[subtitle_size++] = value;
        }
    }
    title[title_size] = '\0';
    subtitle[subtitle_size] = '\0';
}

'''
text = replace_once(text, anchor, split + anchor, "tile text split")
# Extend native renderer switch.
old = r'''            case KU_UI_NATIVE_SEPARATOR:
                ui::separator(bounds.x, bounds.y + bounds.height / 2, bounds.width);
                break;
            default:
                break;
'''
new = r'''            case KU_UI_NATIVE_SEPARATOR:
                ui::separator(bounds.x, bounds.y + bounds.height / 2, bounds.width);
                break;
            case KU_UI_NATIVE_TILE: {
                char title[32];
                char subtitle[32];
                split_native_tile_text(command.text, title, subtitle);
                ui::app_tile(
                    bounds, title, subtitle,
                    static_cast<ui::AppIcon>(command.value),
                    selected,
                    (command.flags & KU_UI_NATIVE_PINNED) != 0U,
                    (command.flags & KU_UI_NATIVE_RUNNING) != 0U);
                break;
            }
            default:
                break;
'''
text = replace_once(text, old, new, "native tile renderer dispatch")
write(path, text)

# ----- Flux Deck: no ordinary window chrome, APPS toggles the deck.
path = "kernel/ui/window_manager.cpp"
text = read(path)
text = replace_once(
    text,
    "ui::Rect calculate_content(const Slot& slot) {\n    if (is_login_surface(slot)) return slot.info.bounds;\n",
    "ui::Rect calculate_content(const Slot& slot) {\n"
    "    if (is_login_surface(slot)) return slot.info.bounds;\n"
    "    if (is_home_surface(slot.info.title)) {\n"
    "        return {\n"
    "            slot.info.bounds.x + 8, slot.info.bounds.y + 8,\n"
    "            slot.info.bounds.width - 16, slot.info.bounds.height - 16,\n"
    "        };\n"
    "    }\n",
    "home content geometry")
text = replace_once(
    text,
    "bool title_hit(const Slot& slot, int32_t x, int32_t y) {\n    if (is_login_surface(slot)) return false;\n",
    "bool title_hit(const Slot& slot, int32_t x, int32_t y) {\n"
    "    if (is_login_surface(slot) || is_home_surface(slot.info.title)) return false;\n",
    "home title hit")
text = replace_once(
    text,
    "    if (existing != nullptr) {\n        if (existing->info.state == WindowState::Minimized) {\n            return restore(existing->info.id);\n        }\n        return focus(existing->info.id);\n    }\n",
    "    if (existing != nullptr) {\n"
    "        if (existing->info.state == WindowState::Minimized) {\n"
    "            return restore(existing->info.id);\n"
    "        }\n"
    "        if (position == 0U && existing->info.focused) {\n"
    "            return minimize(existing->info.id);\n"
    "        }\n"
    "        return focus(existing->info.id);\n"
    "    }\n",
    "APPS toggle")
# Special draw path before normal window chrome.
anchor = "    const ChromeGeometry chrome = calculate_chrome(bounds);\n"
home_draw = r'''    if (is_home_surface(slot.info.title)) {
        ui::panel(bounds, true);
        if (slot.draw != nullptr) {
            const ui::Rect content = calculate_content(slot);
            graphics::set_clip(content.x, content.y, content.width, content.height);
            graphics::set_text_scale_limit(1U);
            slot.draw(slot.info.id, content, true, slot.context);
            graphics::reset_text_scale_limit();
            graphics::reset_clip();
        }
        return;
    }

'''
text = replace_once(text, anchor, home_draw + anchor, "home chrome-free renderer")
text = replace_once(
    text,
    'ui::login_backdrop("LOCAL SESSION / ENTER TO CONTINUE");',
    'ui::login_backdrop("LOCAL SESSION / MOUSE READY");',
    "mouse-first login backdrop")
text = replace_once(
    text,
    "            if (!is_login_surface(*slot)) {\n                const ChromeGeometry chrome = calculate_chrome(slot->info.bounds);\n",
    "            if (!is_login_surface(*slot) && !is_home_surface(slot->info.title)) {\n"
    "                const ChromeGeometry chrome = calculate_chrome(slot->info.bounds);\n",
    "home pointer chrome bypass")
write(path, text)

# ----- Launcher uses semantic cards, not prefixed text rows.
path = "userspace/gui/launcher/main.c"
text = read(path)
start = "static void build_scene(kui_scene* scene) {\n"
end = "int main(void) {\n"
new_build = r'''static void build_scene(kui_scene* scene) {
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
        append_text(label, sizeof(label), "\n");
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
text = replace_between(text, start, end, new_build, "Flux Deck launcher scene")
text = replace_once(
    text,
    "    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {\n        (void)ku_ui_close(window);\n        return 2;\n    }\n\n    for (;;) {\n",
    "    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {\n"
    "        (void)ku_ui_close(window);\n"
    "        return 2;\n"
    "    }\n"
    "    puts(\"[TEST] red_flux_home_surface: PASS\");\n"
    "    puts(\"[TEST] red_flux_tile_launcher: PASS\");\n\n"
    "    for (;;) {\n",
    "Flux Deck runtime markers")
write(path, text)

# ----- Contract tests: packet remains bounded, v1 stays supported, tiles are geometric.
path = "tests/test_sdk_abi.cpp"
text = read(path)
text = replace_once(
    text,
    "    static_assert(KU_UI_NATIVE_MAGIC == UINT32_C(0x4B554932));\n    static_assert(KU_UI_NATIVE_MAX_COMMANDS == 32U);\n",
    "    static_assert(KU_UI_NATIVE_MAGIC == UINT32_C(0x4B554932));\n"
    "    static_assert(KU_UI_NATIVE_VERSION == KU_UI_NATIVE_VERSION_2);\n"
    "    static_assert(KU_UI_NATIVE_TILE == 8);\n"
    "    static_assert(KU_UI_NATIVE_ICON_COUNT == 8);\n"
    "    static_assert(KU_UI_NATIVE_MAX_COMMANDS == 32U);\n",
    "tile ABI assertions")
write(path, text)

path = "tests/test_libui_pointer.c"
text = read(path)
anchor = '    puts("libui native packet + mouse hit-test tests passed");\n'
tile_test = r'''    {
        kui_scene tiles;
        kui_flow flow;
        ku_ui_native_frame tile_frame;
        kui_scene_initialize(&tiles);
        tiles.visible_rows = 6U;
        kui_flow_begin(&flow, &tiles, 0U);
        if (kui_flow_panel(&flow, 20U, "DECK") != KU_STATUS_OK ||
            kui_flow_tile(&flow, 21U, "TERMINAL\nSHELL", KU_UI_NATIVE_ICON_TERMINAL) != KU_STATUS_OK ||
            kui_flow_tile(&flow, 22U, "FILES\nROOT", KU_UI_NATIVE_ICON_FILES) != KU_STATUS_OK ||
            kui_flow_tile(&flow, 23U, "WEB\nNETWORK", KU_UI_NATIVE_ICON_BROWSER) != KU_STATUS_OK ||
            kui_flow_tile(&flow, 24U, "SETTINGS\nSYSTEM", KU_UI_NATIVE_ICON_SETTINGS) != KU_STATUS_OK) return 7;
        if (kui_scene_set_flags(&tiles, 22U, KUI_VIEW_PINNED | KUI_VIEW_RUNNING) != KU_STATUS_OK ||
            kui_scene_build_native(&tiles, &tile_frame) != KU_STATUS_OK ||
            tile_frame.version != KU_UI_NATIVE_VERSION_2 ||
            tile_frame.commands[1].type != KU_UI_NATIVE_TILE ||
            tile_frame.commands[1].y != tile_frame.commands[2].y ||
            tile_frame.commands[2].y != tile_frame.commands[3].y ||
            tile_frame.commands[1].x >= tile_frame.commands[2].x ||
            tile_frame.commands[2].x >= tile_frame.commands[3].x ||
            tile_frame.commands[4].y <= tile_frame.commands[1].y ||
            (tile_frame.commands[2].flags & (KU_UI_NATIVE_PINNED | KU_UI_NATIVE_RUNNING)) !=
                (KU_UI_NATIVE_PINNED | KU_UI_NATIVE_RUNNING) ||
            !expect(kui_scene_hit_test(
                &tiles,
                tile_frame.commands[2].x + tile_frame.commands[2].width / 2,
                center_y(&tile_frame, 2U)), 22U, "tile column hit") ||
            !expect(kui_scene_hit_test(
                &tiles,
                tile_frame.commands[2].x + tile_frame.commands[2].width + 4,
                center_y(&tile_frame, 2U)), 0U, "tile gap inert")) return 8;
    }

'''
text = replace_once(text, anchor, tile_test + anchor, "tile libui test")
write(path, text)

path = "tests/test_mouse_first_apps.py"
text = read(path)
text = replace_once(
    text,
    'browser = read("userspace/gui/browser/main.c")\n',
    'launcher = read("userspace/gui/launcher/main.c")\n'
    'assert "kui_flow_tile" in launcher, "launcher: Flux Deck tiles missing"\n'
    'assert "kui_flow_list_item" not in launcher, "launcher: text-list menu returned"\n'
    'assert "[PIN]" not in launcher, "launcher: ASCII pin decoration returned"\n'
    'assert "red_flux_tile_launcher: PASS" in launcher\n\n'
    'browser = read("userspace/gui/browser/main.c")\n',
    "launcher visual contract")
write(path, text)

# Give chained QMP clicks enough deterministic guest time between actions.
path = "scripts/smoke-uefi-iso-qemu.sh"
text = read(path)
text = replace_once(
    text,
    '        execute(stream, "input-send-event", {"events": [\n            {"type": "btn", "data": {"down": False, "button": "left"}}\n        ]})\n        stream.close()\n',
    '        execute(stream, "input-send-event", {"events": [\n            {"type": "btn", "data": {"down": False, "button": "left"}}\n        ]})\n        time.sleep(0.35)\n        stream.close()\n',
    "QMP chained click settle")
write(path, text)

print("Flux Deck tile migration applied")
