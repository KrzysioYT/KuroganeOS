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

# Public UI ABI: keep ku_ui_frame intact and add a bounded native packet.
path = "sdk/include/kurogane/ui.h"
text = read(path)
text = replace_once(
    text,
    "#define KU_UI_LINE_CAPACITY 64U\n",
    "#define KU_UI_LINE_CAPACITY 64U\n"
    "\n"
    "/* Native bounded retained-widget transport. Legacy ku_ui_frame remains ABI-stable. */\n"
    "#define KU_UI_NATIVE_MAGIC UINT32_C(0x4B554932) /* KUI2 */\n"
    "#define KU_UI_NATIVE_VERSION UINT32_C(1)\n"
    "#define KU_UI_NATIVE_MAX_COMMANDS 32U\n"
    "#define KU_UI_NATIVE_TEXT_CAPACITY 64U\n"
    "#define KU_UI_NATIVE_COORD_LIMIT 4096\n"
    "\n"
    "enum ku_ui_native_command_type {\n"
    "    KU_UI_NATIVE_PANEL = 1,\n"
    "    KU_UI_NATIVE_LABEL = 2,\n"
    "    KU_UI_NATIVE_BUTTON = 3,\n"
    "    KU_UI_NATIVE_INPUT = 4,\n"
    "    KU_UI_NATIVE_LIST_ITEM = 5,\n"
    "    KU_UI_NATIVE_PROGRESS = 6,\n"
    "    KU_UI_NATIVE_SEPARATOR = 7\n"
    "};\n"
    "\n"
    "enum ku_ui_native_command_flags {\n"
    "    KU_UI_NATIVE_SELECTED = UINT32_C(1) << 0,\n"
    "    KU_UI_NATIVE_DISABLED = UINT32_C(1) << 1\n"
    "};\n"
    "\n"
    "typedef struct ku_ui_native_command {\n"
    "    uint32_t type;\n"
    "    uint32_t flags;\n"
    "    int32_t x;\n"
    "    int32_t y;\n"
    "    int32_t width;\n"
    "    int32_t height;\n"
    "    uint32_t foreground_rgb;\n"
    "    uint32_t background_rgb;\n"
    "    uint32_t accent_rgb;\n"
    "    uint32_t value;\n"
    "    uint32_t maximum;\n"
    "    uint32_t reserved;\n"
    "    char text[KU_UI_NATIVE_TEXT_CAPACITY];\n"
    "} ku_ui_native_command;\n"
    "\n"
    "typedef struct ku_ui_native_frame {\n"
    "    uint32_t structure_size;\n"
    "    uint32_t magic;\n"
    "    uint32_t version;\n"
    "    uint32_t command_count;\n"
    "    uint32_t background_rgb;\n"
    "    uint32_t foreground_rgb;\n"
    "    uint32_t accent_rgb;\n"
    "    uint32_t reserved;\n"
    "    ku_ui_native_command commands[KU_UI_NATIVE_MAX_COMMANDS];\n"
    "} ku_ui_native_frame;\n",
    "ui native ABI insertion")
text = replace_once(
    text,
    "static inline ku_status_t ku_ui_poll(\n",
    "static inline ku_status_t ku_ui_present_native(\n"
    "    ku_window_t window,\n"
    "    const ku_ui_native_frame* frame) {\n"
    "    return (ku_status_t)ku_syscall3(\n"
    "        KU_SYS_UI_PRESENT, window, (uint64_t)(uintptr_t)frame,\n"
    "        sizeof(ku_ui_native_frame));\n"
    "}\n\n"
    "static inline ku_status_t ku_ui_poll(\n",
    "ui native present helper")
write(path, text)

# libui public surface: expose deterministic native packet builder for testing/tools.
path = "sdk/include/kurogane/libui.h"
text = read(path)
text = replace_once(
    text,
    "uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y);\nku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene);\n",
    "uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y);\n"
    "ku_status_t kui_scene_build_native(\n"
    "    const kui_scene* scene, ku_ui_native_frame* frame);\n"
    "ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene);\n",
    "libui native builder declaration")
write(path, text)

# Production libui: replace text serialization with deterministic native geometry.
path = "sdk/src/libui.c"
text = read(path)
text = replace_between(text, "static void append_text(", "static kui_view* find_view(", "static kui_view* find_view(", "remove text append")
text = replace_between(text, "static void render_view_line(", "void kui_frame_initialize(", "void kui_frame_initialize(", "remove text row renderer")
new_scene_transport = r'''typedef struct kui_native_layout {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} kui_native_layout;

static int32_t native_view_height(uint32_t type) {
    switch (type) {
        case KUI_VIEW_PANEL: return 38;
        case KUI_VIEW_LABEL: return 22;
        case KUI_VIEW_BUTTON: return 34;
        case KUI_VIEW_INPUT: return 36;
        case KUI_VIEW_LIST_ITEM: return 36;
        case KUI_VIEW_PROGRESS: return 44;
        case KUI_VIEW_SEPARATOR: return 10;
        default: return 0;
    }
}

static void native_layout_view(
    const kui_scene* scene,
    const kui_view* view,
    int32_t y,
    kui_native_layout* output) {
    const uint32_t depth = view_depth(scene, view);
    output->x = 16 + (int32_t)(depth * 12U);
    output->y = y;
    /* width=0 is the native ABI's bounded stretch-to-content sentinel. */
    output->width = 0;
    output->height = native_view_height(view->type);
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
    int32_t cursor_y = 16;
    const uint32_t rows = scene == (const kui_scene*)0 ? 0U : native_rows(scene);
    if (scene == (const kui_scene*)0 || x < 0 || y < 0 ||
        x > KU_UI_NATIVE_COORD_LIMIT || y > KU_UI_NATIVE_COORD_LIMIT) return 0U;

    for (index = 0U; index < scene->view_count && output_index < rows; ++index) {
        const kui_view* view = &scene->views[index];
        kui_native_layout layout;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        native_layout_view(scene, view, cursor_y, &layout);
        if (layout.height <= 0) continue;
        if (interactive_view(view) && x >= layout.x &&
            y >= layout.y && y < layout.y + layout.height) {
            return view->id;
        }
        cursor_y += layout.height + 6;
        ++output_index;
    }
    return 0U;
}

ku_status_t kui_scene_build_native(
    const kui_scene* scene, ku_ui_native_frame* frame) {
    uint32_t visible_index = 0U;
    uint32_t output_index = 0U;
    uint32_t index;
    int32_t cursor_y = 16;
    uint32_t rows;
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

    for (index = 0U; index < scene->view_count && output_index < rows; ++index) {
        const kui_view* view = &scene->views[index];
        kui_native_layout layout;
        ku_ui_native_command* command;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        native_layout_view(scene, view, cursor_y, &layout);
        if (layout.height <= 0) return KU_STATUS_CORRUPT_DATA;

        command = &frame->commands[output_index++];
        command->type = view->type;
        if ((view->flags & KUI_VIEW_SELECTED) != 0U) {
            command->flags |= KU_UI_NATIVE_SELECTED;
        }
        if ((view->flags & KUI_VIEW_DISABLED) != 0U) {
            command->flags |= KU_UI_NATIVE_DISABLED;
        }
        command->x = layout.x;
        command->y = layout.y;
        command->width = layout.width;
        command->height = layout.height;
        command->foreground_rgb = scene->foreground_rgb;
        command->background_rgb = scene->background_rgb;
        command->accent_rgb = scene->accent_rgb;
        command->value = view->value;
        command->maximum = view->maximum;
        if (strlcpy(command->text, view->text, sizeof(command->text)) >=
            sizeof(command->text)) return KU_STATUS_OUT_OF_RANGE;

        cursor_y += layout.height + 6;
    }
    frame->command_count = output_index;
    return KU_STATUS_OK;
}

ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene) {
    ku_ui_native_frame frame;
    const ku_status_t status = kui_scene_build_native(scene, &frame);
    if (status != KU_STATUS_OK) return status;
    return ku_ui_present_native(window, &frame);
}

'''
text = replace_between(text, "uint32_t kui_scene_hit_test(", "void kui_flow_begin(", new_scene_transport + "void kui_flow_begin(", "replace libui scene transport")
write(path, text)

# Native visual widgets in the kernel UI theme.
path = "kernel/ui/ui.hpp"
text = read(path)
text = replace_once(
    text,
    "void button(const Rect& bounds, const char* text, bool selected = false);\nvoid progress(const Rect& bounds, uint32_t value, uint32_t maximum);\n",
    "void button(const Rect& bounds, const char* text, bool selected = false);\n"
    "void input_field(const Rect& bounds, const char* text, bool focused = false);\n"
    "void list_row(\n"
    "    const Rect& bounds, const char* text, bool selected = false,\n"
    "    bool disabled = false);\n"
    "void progress(const Rect& bounds, uint32_t value, uint32_t maximum);\n",
    "ui native widget declarations")
write(path, text)

path = "kernel/ui/ui.cpp"
text = read(path)
needle = "void progress(const Rect& bounds, uint32_t value, uint32_t maximum) {\n"
widgets = r'''void input_field(const Rect& bounds, const char* text, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = focused
        ? graphics::rgb(24, 18, 22) : kTheme.panel_alt;
    const graphics::Color border = focused ? kRedBright : kTheme.border;
    graphics::fill_rect(bounds.x + 3, bounds.y + 3,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, border);
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,
                        focused ? kRedBright : kRedDeep);
    graphics::draw_text(bounds.x + 12, bounds.y + (bounds.height - 8) / 2,
                        text ? text : "", kTheme.text, background, 1U, true);
}

void list_row(
    const Rect& bounds, const char* text, bool selected, bool disabled) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = selected
        ? graphics::rgb(43, 20, 25) : kGraphite;
    const graphics::Color signal = selected
        ? kRedBright : (disabled ? kInactiveSignal : kSteel);
    const graphics::Color foreground = disabled ? kTheme.text_muted : kTheme.text;
    graphics::fill_rect(bounds.x + 3, bounds.y + 3,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        selected ? kRedMuted : kTheme.border);
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height, signal);
    if (selected && bounds.width > 48) {
        graphics::fill_rect(bounds.x + 12, bounds.y, 34, 2, kRedBright);
    }
    graphics::draw_text(bounds.x + 14, bounds.y + (bounds.height - 8) / 2,
                        text ? text : "", foreground, background, 1U, true);
}

'''
text = replace_once(text, needle, widgets + needle, "insert native UI widgets")
write(path, text)

# Kernel Ring-3 UI renderer: validate and draw native packets; keep legacy path intact.
path = "kernel/user/runtime_base.inc"
text = read(path)
new_draw = r'''bool native_text_terminated(const char text[KU_UI_NATIVE_TEXT_CAPACITY]) {
    for (size_t index = 0U; index < KU_UI_NATIVE_TEXT_CAPACITY; ++index) {
        if (text[index] == '\0') return true;
    }
    return false;
}

bool native_command_valid(const ku_ui_native_command& command) {
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

ui::Rect native_bounds(
    const ui::Rect& content,
    const ku_ui_native_command& command) {
    int32_t width = command.width;
    if (width == 0) width = content.width - command.x - 16;
    if (width < 1) width = 1;
    const int32_t available_width = content.width - command.x;
    if (available_width > 0 && width > available_width) width = available_width;
    int32_t height = command.height;
    const int32_t available_height = content.height - command.y;
    if (available_height > 0 && height > available_height) height = available_height;
    if (height < 1) height = 1;
    return {content.x + command.x, content.y + command.y, width, height};
}

void draw_native_frame(
    const ku_ui_native_frame& frame,
    const ui::Rect& content,
    bool focused) {
    const graphics::Color background = frame.background_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color foreground = frame.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent = frame.accent_rgb & UINT32_C(0xFFFFFF);
    graphics::fill_rect(content.x, content.y, content.width, content.height, background);
    if (content.width > 48) {
        graphics::fill_rect(content.x + 16, content.y + 7,
                            content.width - 32, 1,
                            focused ? accent : graphics::rgb(45, 47, 52));
    }

    for (uint32_t index = 0U; index < frame.command_count; ++index) {
        const ku_ui_native_command& command = frame.commands[index];
        const ui::Rect bounds = native_bounds(content, command);
        const bool selected = (command.flags & KU_UI_NATIVE_SELECTED) != 0U;
        const bool disabled = (command.flags & KU_UI_NATIVE_DISABLED) != 0U;
        switch (command.type) {
            case KU_UI_NATIVE_PANEL:
                ui::panel(bounds, true);
                graphics::draw_text(bounds.x + 12, bounds.y + 11, command.text,
                                    foreground, background, 1U, true);
                break;
            case KU_UI_NATIVE_LABEL:
                graphics::draw_text(bounds.x, bounds.y + 6, command.text,
                                    disabled ? ui::default_theme().text_muted : foreground,
                                    background, 1U, true);
                break;
            case KU_UI_NATIVE_BUTTON:
                ui::button(bounds, command.text, selected);
                break;
            case KU_UI_NATIVE_INPUT:
                ui::input_field(bounds, command.text, selected);
                break;
            case KU_UI_NATIVE_LIST_ITEM:
                ui::list_row(bounds, command.text, selected, disabled);
                break;
            case KU_UI_NATIVE_PROGRESS: {
                graphics::draw_text(bounds.x, bounds.y, command.text,
                                    foreground, background, 1U, true);
                const int32_t bar_y = bounds.y + 18;
                const int32_t bar_height = bounds.height > 24 ? bounds.height - 20 : 8;
                ui::progress({bounds.x, bar_y, bounds.width, bar_height},
                             command.value, command.maximum);
                break;
            }
            case KU_UI_NATIVE_SEPARATOR:
                ui::separator(bounds.x, bounds.y + bounds.height / 2, bounds.width);
                break;
            default:
                break;
        }
    }
}

void draw_user_window(
    windowing::WindowId,
    const ui::Rect& content,
    bool focused,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    if (context == nullptr || !context->active || !context->ui.active) return;

    windowing::SurfaceView retained{};
    if (windowing::read_surface(context->ui.window, &retained) == windowing::Status::Ok &&
        retained.data != nullptr && retained.size == sizeof(ku_ui_native_frame)) {
        ku_ui_native_frame native{};
        auto* destination = reinterpret_cast<uint8_t*>(&native);
        for (size_t index = 0U; index < sizeof(native); ++index) {
            destination[index] = retained.data[index];
        }
        if (native_frame_valid(native)) {
            draw_native_frame(native, content, focused);
            return;
        }
    }

    ku_ui_frame frame = context->ui.frame;
    if (retained.data != nullptr && retained.size == sizeof(frame)) {
        auto* destination = reinterpret_cast<uint8_t*>(&frame);
        for (size_t index = 0U; index < sizeof(frame); ++index) {
            destination[index] = retained.data[index];
        }
    }
    const graphics::Color background = frame.background_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color foreground = frame.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent = frame.accent_rgb & UINT32_C(0xFFFFFF);
    graphics::fill_rect(
        content.x, content.y, content.width, content.height, background);
    int32_t y = content.y + 12;
    for (uint32_t index = 0U;
         index < frame.line_count && index < KU_UI_MAX_LINES;
         ++index) {
        graphics::draw_text(
            content.x + 12, y, frame.lines[index],
            index == 0U && focused ? accent : foreground,
            background, 2U, true);
        y += 22;
        if (y + 20 >= content.y + content.height) break;
    }
    if (frame.progress_maximum != 0U && content.height >= 70) {
        ui::progress(
            {content.x + 12, content.y + content.height - 32,
             content.width - 24, 16},
            frame.progress_value, frame.progress_maximum);
    }
}

'''
text = replace_between(text, "void draw_user_window(\n", "void queue_user_event(", new_draw + "void queue_user_event(", "replace Ring3 UI renderer")
new_present = r'''        case KU_SYS_UI_PRESENT: {
            if (!context->ui.active || frame.rdi != context->ui.window ||
                (frame.rdx != sizeof(ku_ui_frame) &&
                 frame.rdx != sizeof(ku_ui_native_frame)) ||
                frame.rdx > SIZE_MAX ||
                !validate_user_buffer(
                    *context, frame.rsi, static_cast<size_t>(frame.rdx))) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }

            if (frame.rdx == sizeof(ku_ui_native_frame)) {
                const auto* user_native = reinterpret_cast<const ku_ui_native_frame*>(
                    static_cast<uintptr_t>(frame.rsi));
                if (!native_frame_valid(*user_native)) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                    return;
                }
                const windowing::Status surface_status = windowing::present_surface(
                    context->ui.window, sizeof(ku_ui_native_frame), 1U,
                    sizeof(ku_ui_native_frame), user_native,
                    sizeof(ku_ui_native_frame));
                if (surface_status != windowing::Status::Ok) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                    return;
                }
                system_metrics::record_graphics_work(
                    UINT64_C(1) + static_cast<uint64_t>(user_native->command_count));
                frame.rax = KU_STATUS_OK;
                return;
            }

            const auto* user_frame = reinterpret_cast<const ku_ui_frame*>(
                static_cast<uintptr_t>(frame.rsi));
            if (user_frame->structure_size != sizeof(ku_ui_frame) ||
                user_frame->line_count > KU_UI_MAX_LINES || user_frame->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                return;
            }
            for (uint32_t line = 0U; line < user_frame->line_count; ++line) {
                bool terminated = false;
                for (size_t character = 0U; character < KU_UI_LINE_CAPACITY; ++character) {
                    if (user_frame->lines[line][character] == '\0') {
                        terminated = true;
                        break;
                    }
                }
                if (!terminated) {
                    frame.rax = static_cast<uint64_t>(KU_STATUS_CORRUPT_DATA);
                    return;
                }
            }
            const windowing::Status surface_status = windowing::present_surface(
                context->ui.window, sizeof(ku_ui_frame), 1U, sizeof(ku_ui_frame),
                user_frame, sizeof(ku_ui_frame));
            if (surface_status != windowing::Status::Ok) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            context->ui.frame = *user_frame;
            system_metrics::record_graphics_work(
                UINT64_C(1) + static_cast<uint64_t>(user_frame->line_count));
            frame.rax = KU_STATUS_OK;
            return;
        }
'''
text = replace_between(text, "        case KU_SYS_UI_PRESENT: {\n", "        case KU_SYS_UI_POLL: {\n", new_present + "        case KU_SYS_UI_POLL: {\n", "replace UI_PRESENT dispatch")
write(path, text)

# ABI tests: lock native packet size under the retained-surface 4 KiB cap.
path = "tests/test_sdk_abi.cpp"
text = read(path)
text = replace_once(
    text,
    "    static_assert(sizeof(ku_ui_window_options) == 20);\n    static_assert(sizeof(ku_ui_frame) == 800);\n    static_assert(sizeof(ku_ui_event) == 32);\n",
    "    static_assert(sizeof(ku_ui_window_options) == 20);\n"
    "    static_assert(sizeof(ku_ui_frame) == 800);\n"
    "    static_assert(sizeof(ku_ui_native_command) == 112);\n"
    "    static_assert(sizeof(ku_ui_native_frame) == 3616);\n"
    "    static_assert(sizeof(ku_ui_native_frame) <= 4096);\n"
    "    static_assert(KU_UI_NATIVE_MAGIC == UINT32_C(0x4B554932));\n"
    "    static_assert(KU_UI_NATIVE_MAX_COMMANDS == 32U);\n"
    "    static_assert(sizeof(ku_ui_event) == 32);\n",
    "native ABI tests")
write(path, text)

# Host libui test now verifies native geometry and hit-testing from the same scene.
write("tests/test_libui_pointer.c", r'''#include <kurogane/libui.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

size_t strlcpy(char* destination, const char* source, size_t capacity) {
    const size_t length = strlen(source);
    if (capacity != 0U) {
        const size_t copied = length < capacity - 1U ? length : capacity - 1U;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

static int expect(uint32_t actual, uint32_t expected, const char* label) {
    if (actual == expected) return 1;
    printf("%s: expected %u got %u\n", label, expected, actual);
    return 0;
}

static int center_y(const ku_ui_native_frame* frame, uint32_t command) {
    return frame->commands[command].y + frame->commands[command].height / 2;
}

int main(void) {
    kui_scene scene;
    kui_flow root;
    ku_ui_native_frame native;
    kui_scene_initialize(&scene);
    scene.visible_rows = 4U;
    kui_flow_begin(&root, &scene, 0U);
    if (kui_flow_panel(&root, 1U, "PANEL") != KU_STATUS_OK ||
        kui_flow_button(&root, 2U, "OPEN") != KU_STATUS_OK ||
        kui_flow_label(&root, 3U, "INFO") != KU_STATUS_OK ||
        kui_flow_list_item(&root, 4U, "ITEM") != KU_STATUS_OK ||
        kui_flow_button(&root, 5U, "MORE") != KU_STATUS_OK ||
        kui_flow_button(&root, 6U, "OVERFLOW") != KU_STATUS_OK) return 1;

    if (kui_scene_build_native(&scene, &native) != KU_STATUS_OK ||
        native.magic != KU_UI_NATIVE_MAGIC ||
        native.version != KU_UI_NATIVE_VERSION || native.command_count != 4U ||
        native.commands[0].type != KU_UI_NATIVE_PANEL ||
        native.commands[1].type != KU_UI_NATIVE_BUTTON ||
        native.commands[3].type != KU_UI_NATIVE_LIST_ITEM ||
        native.commands[1].width != 0 || native.commands[1].height != 34) return 2;

    if (!expect(kui_scene_hit_test(&scene, 24, center_y(&native, 0U)), 0U, "panel inert") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 1U)), 2U, "button hit") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 2U)), 0U, "label inert") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 3U)), 4U, "list hit") ||
        !expect(kui_scene_hit_test(&scene, -1, center_y(&native, 1U)), 0U, "negative x")) return 3;

    if (kui_scene_set_flags(&scene, 2U, KUI_VIEW_DISABLED) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 1U)), 0U, "disabled inert")) return 4;

    if (kui_scene_set_flags(&scene, 2U, 0U) != KU_STATUS_OK ||
        kui_scene_set_flags(&scene, 3U, KUI_VIEW_HIDDEN) != KU_STATUS_OK ||
        kui_scene_build_native(&scene, &native) != KU_STATUS_OK ||
        native.command_count != 4U ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 2U)), 4U, "hidden compaction") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 3U)), 5U, "visible fourth")) return 5;

    if (kui_scene_scroll(&scene, 1) != KU_STATUS_OK ||
        kui_scene_build_native(&scene, &native) != KU_STATUS_OK ||
        native.command_count != 4U ||
        native.commands[0].type != KU_UI_NATIVE_BUTTON ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 0U)), 2U, "scroll first") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 1U)), 4U, "scroll list")) return 6;

    puts("libui native packet + mouse hit-test tests passed");
    return 0;
}
''')

print("native UI packet migration applied")
