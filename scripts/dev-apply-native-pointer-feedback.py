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

# Window Core owns topmost pointer hover and exposes it through the existing
# internal interaction snapshot. This keeps hover generation-safe and lets the
# renderer reject geometry hidden behind another window.
path = "kernel/ui/window_manager.hpp"
text = read(path)
text = replace_once(
    text,
    "struct InteractionSnapshot {\n    WindowId focused;\n    WindowId dragged;\n    WindowId resized;\n};\n",
    "struct InteractionSnapshot {\n"
    "    WindowId focused;\n"
    "    WindowId dragged;\n"
    "    WindowId resized;\n"
    "    WindowId hovered;\n"
    "};\n",
    "interaction hover snapshot")
write(path, text)

path = "kernel/ui/window_manager.cpp"
text = read(path)
text = replace_once(
    text,
    "WindowId g_focused = INVALID_WINDOW;\nWindowId g_dragged = INVALID_WINDOW;\nWindowId g_resized = INVALID_WINDOW;\n",
    "WindowId g_focused = INVALID_WINDOW;\n"
    "WindowId g_dragged = INVALID_WINDOW;\n"
    "WindowId g_resized = INVALID_WINDOW;\n"
    "WindowId g_hovered = INVALID_WINDOW;\n",
    "hover global")
text = replace_once(
    text,
    "    g_focused = INVALID_WINDOW;\n    g_dragged = INVALID_WINDOW;\n    g_resized = INVALID_WINDOW;\n    update_z_order();\n",
    "    g_focused = INVALID_WINDOW;\n"
    "    g_dragged = INVALID_WINDOW;\n"
    "    g_resized = INVALID_WINDOW;\n"
    "    g_hovered = INVALID_WINDOW;\n"
    "    update_z_order();\n",
    "session purge hover reset")
text = replace_once(
    text,
    "void cancel_capture(WindowId id) {\n    if (g_dragged == id) g_dragged = INVALID_WINDOW;\n    if (g_resized == id) g_resized = INVALID_WINDOW;\n}\n",
    "void cancel_capture(WindowId id) {\n"
    "    if (g_dragged == id) g_dragged = INVALID_WINDOW;\n"
    "    if (g_resized == id) g_resized = INVALID_WINDOW;\n"
    "}\n\n"
    "void clear_hover(WindowId id) {\n"
    "    if (g_hovered == id) g_hovered = INVALID_WINDOW;\n"
    "}\n\n"
    "void update_pointer_feedback(const input::Event& event) {\n"
    "    if (event.type != input::EventType::MouseMove &&\n"
    "        event.type != input::EventType::MouseButtonDown &&\n"
    "        event.type != input::EventType::MouseButtonUp) return;\n"
    "    const WindowId previous = g_hovered;\n"
    "    const WindowId next = hit_test(event.x, event.y);\n"
    "    if (previous != INVALID_WINDOW && previous != next) {\n"
    "        invalidate_window(previous);\n"
    "    }\n"
    "    if (next != INVALID_WINDOW) {\n"
    "        // Repaint even within one window: the pointer may have crossed\n"
    "        // native widget geometry while the owning window stayed the same.\n"
    "        invalidate_window(next);\n"
    "    }\n"
    "    g_hovered = next;\n"
    "}\n",
    "pointer feedback helper")
text = replace_once(
    text,
    "    g_focused = INVALID_WINDOW;\n    g_dragged = INVALID_WINDOW;\n    g_resized = INVALID_WINDOW;\n#ifndef KUROGANE_HOST_TEST\n",
    "    g_focused = INVALID_WINDOW;\n"
    "    g_dragged = INVALID_WINDOW;\n"
    "    g_resized = INVALID_WINDOW;\n"
    "    g_hovered = INVALID_WINDOW;\n"
    "#ifndef KUROGANE_HOST_TEST\n",
    "initialize hover reset")
text = replace_once(
    text,
    "    --g_count;\n    release_slot(*slot);\n    cancel_capture(id);\n",
    "    --g_count;\n"
    "    clear_hover(id);\n"
    "    release_slot(*slot);\n"
    "    cancel_capture(id);\n",
    "close hover cleanup")
text = replace_once(
    text,
    "    cancel_capture(id);\n    slot->info.state = WindowState::Minimized;\n",
    "    cancel_capture(id);\n"
    "    clear_hover(id);\n"
    "    slot->info.state = WindowState::Minimized;\n",
    "minimize hover cleanup")
# Pointer feedback must run after chrome/capture decisions but before the event
# is queued to Ring-3. Native content clicks have no early return here; dock and
# chrome controls are kernel-owned and intentionally do not use libui hover.
text = replace_once(
    text,
    "#ifndef KUROGANE_HOST_TEST\n    if (event.type == input::EventType::MouseMove) move_cursor(event.x, event.y);\n#endif\n\n    Slot* focused = find(g_focused);\n",
    "    update_pointer_feedback(event);\n\n"
    "#ifndef KUROGANE_HOST_TEST\n"
    "    if (event.type == input::EventType::MouseMove) move_cursor(event.x, event.y);\n"
    "#endif\n\n"
    "    Slot* focused = find(g_focused);\n",
    "dispatch pointer feedback")
text = replace_once(
    text,
    "    out_snapshot->focused = g_focused;\n    out_snapshot->dragged = g_dragged;\n    out_snapshot->resized = g_resized;\n",
    "    out_snapshot->focused = g_focused;\n"
    "    out_snapshot->dragged = g_dragged;\n"
    "    out_snapshot->resized = g_resized;\n"
    "    out_snapshot->hovered = g_hovered;\n",
    "interaction snapshot hover")
write(path, text)

# Visual primitives get transient pointer states without changing old callers.
path = "kernel/ui/ui.hpp"
text = read(path)
text = replace_once(
    text,
    "void button(const Rect& bounds, const char* text, bool selected = false);\n"
    "void input_field(const Rect& bounds, const char* text, bool focused = false);\n"
    "void app_tile(\n"
    "    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,\n"
    "    bool selected = false, bool pinned = false, bool running = false);\n"
    "void list_row(\n"
    "    const Rect& bounds, const char* text, bool selected = false,\n"
    "    bool disabled = false);\n",
    "void button(\n"
    "    const Rect& bounds, const char* text, bool selected = false,\n"
    "    bool hovered = false, bool pressed = false);\n"
    "void input_field(\n"
    "    const Rect& bounds, const char* text, bool focused = false,\n"
    "    bool hovered = false, bool pressed = false);\n"
    "void app_tile(\n"
    "    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,\n"
    "    bool selected = false, bool pinned = false, bool running = false,\n"
    "    bool hovered = false, bool pressed = false);\n"
    "void list_row(\n"
    "    const Rect& bounds, const char* text, bool selected = false,\n"
    "    bool disabled = false, bool hovered = false, bool pressed = false);\n",
    "pointer-aware widget declarations")
write(path, text)

path = "kernel/ui/ui.cpp"
text = read(path)
old_button = '''void button(const Rect& bounds, const char* text, bool selected) {
    const auto background = selected ? graphics::rgb(55, 20, 26) : kTheme.panel_alt;
    const auto signal = selected ? kRedBright : kTheme.border;
'''
new_button = '''void button(
    const Rect& bounds, const char* text, bool selected, bool hovered, bool pressed) {
    const auto background = pressed
        ? graphics::rgb(66, 18, 28)
        : (selected ? graphics::rgb(55, 20, 26)
                    : (hovered ? graphics::rgb(34, 30, 35) : kTheme.panel_alt));
    const auto signal = pressed ? kTheme.danger
        : (selected ? kRedBright : (hovered ? kRedMuted : kTheme.border));
'''
text = replace_once(text, old_button, new_button, "button pointer style")
text = replace_once(
    text,
    "    graphics::fill_rect(bounds.x + 3, bounds.y, bounds.width - 3, 1, signal);\n\n"
    "    const char* rendered = text;\n",
    "    graphics::fill_rect(bounds.x + 3, bounds.y, bounds.width - 3, 1, signal);\n"
    "    if (pressed && bounds.width > 20) {\n"
    "        graphics::fill_rect(bounds.x + 8, bounds.y + bounds.height - 3,\n"
    "                            bounds.width - 16, 2, kRedBright);\n"
    "    }\n\n"
    "    const char* rendered = text;\n",
    "button pressed feedback")
old_input = '''void input_field(const Rect& bounds, const char* text, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = focused
        ? graphics::rgb(24, 18, 22) : kTheme.panel_alt;
    const graphics::Color border = focused ? kRedBright : kTheme.border;
'''
new_input = '''void input_field(
    const Rect& bounds, const char* text, bool focused, bool hovered, bool pressed) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = pressed
        ? graphics::rgb(31, 16, 21)
        : (focused ? graphics::rgb(24, 18, 22)
                   : (hovered ? graphics::rgb(24, 24, 28) : kTheme.panel_alt));
    const graphics::Color border = focused || pressed
        ? kRedBright : (hovered ? kRedMuted : kTheme.border);
'''
text = replace_once(text, old_input, new_input, "input pointer style")
text = replace_once(
    text,
    "    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,\n"
    "                        focused ? kRedBright : kRedDeep);\n",
    "    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,\n"
    "                        focused || pressed ? kRedBright\n"
    "                                           : (hovered ? kRedMuted : kRedDeep));\n",
    "input pointer signal")
old_tile = '''void app_tile(
    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,
    bool selected, bool pinned, bool running) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = selected
        ? graphics::rgb(49, 20, 27)
        : (running ? graphics::rgb(24, 22, 26) : kGraphite);
    const graphics::Color border = selected
        ? kRedBright : (running ? kSteel : kTheme.border);
'''
new_tile = '''void app_tile(
    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,
    bool selected, bool pinned, bool running, bool hovered, bool pressed) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = pressed
        ? graphics::rgb(63, 17, 27)
        : (selected ? graphics::rgb(49, 20, 27)
                    : (hovered ? graphics::rgb(36, 27, 32)
                               : (running ? graphics::rgb(24, 22, 26) : kGraphite)));
    const graphics::Color border = pressed || selected
        ? kRedBright : (hovered ? kRedMuted : (running ? kSteel : kTheme.border));
'''
text = replace_once(text, old_tile, new_tile, "tile pointer style")
text = replace_once(
    text,
    "    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,\n"
    "                        selected ? kRedBright : (running ? kRedMuted : kRedDeep));\n"
    "    if (selected) {\n",
    "    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,\n"
    "                        pressed || selected ? kRedBright\n"
    "                                            : (hovered || running ? kRedMuted : kRedDeep));\n"
    "    if (selected || hovered) {\n",
    "tile hover signal")
text = replace_once(
    text,
    "    app_icon_glyph(bounds, icon, kTheme.text, selected ? kRedBright : kRedMuted);\n",
    "    app_icon_glyph(bounds, icon, kTheme.text,\n"
    "                   pressed || selected || hovered ? kRedBright : kRedMuted);\n",
    "tile icon feedback")
text = replace_once(
    text,
    "    if (running) {\n        graphics::fill_rect(bounds.x + 12, bounds.y + bounds.height - 7, 28, 2,\n"
    "                            selected ? kRedBright : kRedMuted);\n    }\n}\n\nvoid list_row(\n"
    "    const Rect& bounds, const char* text, bool selected, bool disabled) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = selected\n"
    "        ? graphics::rgb(43, 20, 25) : kGraphite;\n"
    "    const graphics::Color signal = selected\n"
    "        ? kRedBright : (disabled ? kInactiveSignal : kSteel);\n",
    "    if (running) {\n"
    "        graphics::fill_rect(bounds.x + 12, bounds.y + bounds.height - 7, 28, 2,\n"
    "                            selected || hovered ? kRedBright : kRedMuted);\n"
    "    }\n"
    "    if (pressed && bounds.width > 28) {\n"
    "        graphics::fill_rect(bounds.x + 52, bounds.y + bounds.height - 5,\n"
    "                            bounds.width - 64, 2, kRedBright);\n"
    "    }\n"
    "}\n\n"
    "void list_row(\n"
    "    const Rect& bounds, const char* text, bool selected, bool disabled,\n"
    "    bool hovered, bool pressed) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = pressed\n"
    "        ? graphics::rgb(55, 18, 27)\n"
    "        : (selected ? graphics::rgb(43, 20, 25)\n"
    "                    : (hovered ? graphics::rgb(31, 29, 33) : kGraphite));\n"
    "    const graphics::Color signal = pressed || selected\n"
    "        ? kRedBright : (disabled ? kInactiveSignal : (hovered ? kRedMuted : kSteel));\n",
    "list pointer style")
text = replace_once(
    text,
    "    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,\n"
    "                        selected ? kRedMuted : kTheme.border);\n",
    "    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,\n"
    "                        pressed || selected ? kRedMuted\n"
    "                                            : (hovered ? kSteel : kTheme.border));\n",
    "list pointer border")
text = replace_once(
    text,
    "    if (selected && bounds.width > 48) {\n",
    "    if ((selected || hovered) && bounds.width > 48) {\n",
    "list hover cap")
write(path, text)

# Native renderer computes transient hover/press from the real global pointer
# and only when Window Core says this is the topmost hovered window.
path = "kernel/user/runtime_base.inc"
text = read(path)
text = replace_once(
    text,
    "void draw_native_frame(\n"
    "    const ku_ui_native_frame& frame,\n"
    "    const ui::Rect& content,\n"
    "    bool focused) {\n",
    "void draw_native_frame(\n"
    "    windowing::WindowId window,\n"
    "    const ku_ui_native_frame& frame,\n"
    "    const ui::Rect& content,\n"
    "    bool focused) {\n"
    "    windowing::InteractionSnapshot interaction{};\n"
    "    const bool pointer_owner =\n"
    "        windowing::interaction_snapshot(&interaction) == windowing::Status::Ok &&\n"
    "        interaction.hovered == window;\n"
    "    const int32_t pointer_x = input::pointer_x();\n"
    "    const int32_t pointer_y = input::pointer_y();\n"
    "    const bool primary_down =\n"
    "        (input::pointer_buttons() & drivers::mouse::Left) != 0U;\n",
    "native renderer pointer context")
text = replace_once(
    text,
    "        const bool selected = (command.flags & KU_UI_NATIVE_SELECTED) != 0U;\n"
    "        const bool disabled = (command.flags & KU_UI_NATIVE_DISABLED) != 0U;\n"
    "        switch (command.type) {\n",
    "        const bool selected = (command.flags & KU_UI_NATIVE_SELECTED) != 0U;\n"
    "        const bool disabled = (command.flags & KU_UI_NATIVE_DISABLED) != 0U;\n"
    "        const bool interactive = !disabled &&\n"
    "            (command.type == KU_UI_NATIVE_BUTTON ||\n"
    "             command.type == KU_UI_NATIVE_INPUT ||\n"
    "             command.type == KU_UI_NATIVE_LIST_ITEM ||\n"
    "             command.type == KU_UI_NATIVE_TILE);\n"
    "        const bool hovered = pointer_owner && interactive &&\n"
    "            ui::contains(bounds, pointer_x, pointer_y);\n"
    "        const bool pressed = hovered && primary_down;\n"
    "        switch (command.type) {\n",
    "native hover calculation")
text = replace_once(
    text,
    "            case KU_UI_NATIVE_BUTTON:\n                ui::button(bounds, command.text, selected);\n",
    "            case KU_UI_NATIVE_BUTTON:\n"
    "                ui::button(bounds, command.text, selected, hovered, pressed);\n",
    "native button feedback")
text = replace_once(
    text,
    "            case KU_UI_NATIVE_INPUT:\n                ui::input_field(bounds, command.text, selected);\n",
    "            case KU_UI_NATIVE_INPUT:\n"
    "                ui::input_field(bounds, command.text, selected, hovered, pressed);\n",
    "native input feedback")
text = replace_once(
    text,
    "            case KU_UI_NATIVE_LIST_ITEM:\n                ui::list_row(bounds, command.text, selected, disabled);\n",
    "            case KU_UI_NATIVE_LIST_ITEM:\n"
    "                ui::list_row(bounds, command.text, selected, disabled, hovered, pressed);\n",
    "native list feedback")
text = replace_once(
    text,
    "                    selected,\n"
    "                    (command.flags & KU_UI_NATIVE_PINNED) != 0U,\n"
    "                    (command.flags & KU_UI_NATIVE_RUNNING) != 0U);\n",
    "                    selected,\n"
    "                    (command.flags & KU_UI_NATIVE_PINNED) != 0U,\n"
    "                    (command.flags & KU_UI_NATIVE_RUNNING) != 0U,\n"
    "                    hovered, pressed);\n",
    "native tile feedback")
text = replace_once(
    text,
    "            draw_native_frame(native, content, focused);\n",
    "            draw_native_frame(context->ui.window, native, content, focused);\n",
    "native draw window identity")
write(path, text)

# Production Window Core regression: hover changes must produce bounded region
# damage, clear when leaving the window, and never survive lifecycle close.
path = "tests/test_window_manager.cpp"
text = read(path)
anchor = "    if (resource_snapshot(&owner_after) != Status::Ok ||\n        owner_after.windows != owner_baseline.windows ||\n        owner_after.retained_surfaces != owner_baseline.retained_surfaces ||\n        owner_after.retained_bytes != owner_baseline.retained_bytes) return 91;\n    return 0;\n"
replacement = """    if (resource_snapshot(&owner_after) != Status::Ok ||
        owner_after.windows != owner_baseline.windows ||
        owner_after.retained_surfaces != owner_baseline.retained_surfaces ||
        owner_after.retained_bytes != owner_baseline.retained_bytes) return 91;

    // Native pointer feedback is owned by the topmost live WindowId. Pointer
    // movement and button transitions invalidate only window regions so hover
    // and pressed visuals never require a full compositor repaint.
    WindowId hover_window = INVALID_WINDOW;
    if (create_window("Hover", UINT64_C(5151), {140, 150, 300, 220},
                      draw, receive, nullptr, &hover_window) != Status::Ok ||
        !render_if_needed()) return 92;
    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 180;
    event.y = 210;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != hover_window) return 93;
    if (damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U ||
        damage.regions[0].x != 140 || damage.regions[0].y != 150 ||
        damage.regions[0].width != 300 || damage.regions[0].height != 220) return 94;
    if (!render_if_needed()) return 95;

    event.type = input::EventType::MouseButtonDown;
    event.button = drivers::mouse::Left;
    event.buttons = drivers::mouse::Left;
    if (dispatch(event) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||
        damage.full || damage.count != 1U) return 96;
    if (!render_if_needed()) return 97;
    event.type = input::EventType::MouseButtonUp;
    event.buttons = 0U;
    if (dispatch(event) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||
        damage.full || damage.count != 1U) return 98;
    if (!render_if_needed()) return 99;

    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 790;
    event.y = 590;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != INVALID_WINDOW ||
        damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 1U) return 100;
    if (!render_if_needed()) return 101;

    event.x = 180;
    event.y = 210;
    if (dispatch(event) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != hover_window) return 102;
    if (close(hover_window) != Status::Ok ||
        interaction_snapshot(&interaction) != Status::Ok ||
        interaction.hovered != INVALID_WINDOW) return 103;
    return 0;
"""
text = replace_once(text, anchor, replacement, "pointer feedback regression")
write(path, text)

print("native pointer feedback migration applied")
