#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


# ----- UI primitives carry transient pointer state without changing public ABI.
path = "kernel/ui/ui.hpp"
text = read(path)
text = replace_once(
    text,
    "void flux_control(const Rect& bounds, FluxControl control, bool active = false);\n",
    "void flux_control(\n"
    "    const Rect& bounds, FluxControl control, bool active = false,\n"
    "    bool hovered = false, bool pressed = false);\n",
    "flux control pointer signature")
text = replace_once(
    text,
    "void dock_item(const Rect& bounds, DockIcon icon, bool running, bool focused);\n",
    "void dock_item(\n"
    "    const Rect& bounds, DockIcon icon, bool running, bool focused,\n"
    "    bool hovered = false, bool pressed = false);\n",
    "dock item pointer signature")
text = replace_once(
    text,
    "void dock_task(const Rect& bounds, const char* title, bool focused, bool minimized);\n",
    "void dock_task(\n"
    "    const Rect& bounds, const char* title, bool focused, bool minimized,\n"
    "    bool hovered = false, bool pressed = false);\n",
    "dock task pointer signature")
text = replace_once(
    text,
    "void button(const Rect& bounds, const char* text, bool selected = false);\n",
    "void button(\n"
    "    const Rect& bounds, const char* text, bool selected = false,\n"
    "    bool hovered = false, bool pressed = false);\n",
    "button pointer signature")
text = replace_once(
    text,
    "    bool selected = false, bool pinned = false, bool running = false);\n",
    "    bool selected = false, bool pinned = false, bool running = false,\n"
    "    bool hovered = false, bool pressed = false);\n",
    "tile pointer signature")
text = replace_once(
    text,
    "    const Rect& bounds, const char* text, bool selected = false,\n    bool disabled = false);\n",
    "    const Rect& bounds, const char* text, bool selected = false,\n"
    "    bool disabled = false, bool hovered = false, bool pressed = false);\n",
    "list row pointer signature")
write(path, text)

path = "kernel/ui/ui.cpp"
text = read(path)
text = replace_once(
    text,
    "void flux_control(const Rect& bounds, FluxControl control, bool active) {\n"
    "    if (bounds.width <= 8 || bounds.height <= 8) return;\n"
    "    const graphics::Color background = active ? graphics::rgb(45, 27, 31) : kGraphite;\n"
    "    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);\n"
    "    graphics::fill_rect(bounds.x, bounds.y + bounds.height - 1,\n"
    "                        bounds.width, 1,\n"
    "                        active ? kTheme.accent : kTheme.border);\n",
    "void flux_control(\n"
    "    const Rect& bounds, FluxControl control, bool active,\n"
    "    bool hovered, bool pressed) {\n"
    "    if (bounds.width <= 8 || bounds.height <= 8) return;\n"
    "    const graphics::Color background = pressed\n"
    "        ? graphics::rgb(68, 18, 27)\n"
    "        : (active ? graphics::rgb(45, 27, 31)\n"
    "                  : (hovered ? graphics::rgb(34, 29, 33) : kGraphite));\n"
    "    const graphics::Color edge = pressed\n"
    "        ? kRedBright : (active ? kTheme.accent : (hovered ? kRedMuted : kTheme.border));\n"
    "    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);\n"
    "    graphics::fill_rect(bounds.x, bounds.y + bounds.height - 1,\n"
    "                        bounds.width, 1, edge);\n",
    "flux control pointer rendering")
text = replace_once(
    text,
    "            control_minimize(bounds, active ? kTheme.text : kTheme.text_muted);\n",
    "            control_minimize(bounds, active || hovered ? kTheme.text : kTheme.text_muted);\n",
    "minimize hover glyph")
text = replace_once(
    text,
    "            control_expand(bounds, active ? kRedBright : kSteel);\n",
    "            control_expand(bounds, active || hovered ? kRedBright : kSteel);\n",
    "expand hover glyph")
text = replace_once(
    text,
    "            control_dismiss(bounds, active ? kTheme.danger : kRedMuted);\n",
    "            control_dismiss(bounds, active || hovered ? kTheme.danger : kRedMuted);\n",
    "dismiss hover glyph")

text = replace_once(
    text,
    "void dock_item(const Rect& bounds, DockIcon icon, bool running, bool focused) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = focused\n"
    "        ? graphics::rgb(54, 21, 27)\n"
    "        : (running ? kDockRaised : kDockSurface);\n"
    "    const graphics::Color border = focused\n"
    "        ? kRedBright\n"
    "        : (running ? kSteel : graphics::rgb(38, 40, 45));\n",
    "void dock_item(\n"
    "    const Rect& bounds, DockIcon icon, bool running, bool focused,\n"
    "    bool hovered, bool pressed) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = pressed\n"
    "        ? graphics::rgb(70, 18, 27)\n"
    "        : (focused ? graphics::rgb(54, 21, 27)\n"
    "                   : (hovered ? graphics::rgb(36, 30, 35)\n"
    "                              : (running ? kDockRaised : kDockSurface)));\n"
    "    const graphics::Color border = pressed || focused\n"
    "        ? kRedBright\n"
    "        : (hovered ? kRedMuted : (running ? kSteel : graphics::rgb(38, 40, 45)));\n",
    "dock item pointer rendering")
text = replace_once(
    text,
    "    dock_icon(bounds, icon,\n              focused ? kTheme.text : kTheme.text_muted,\n              focused || running ? kRedHot : kRedMuted);\n",
    "    dock_icon(bounds, icon,\n"
    "              focused || hovered ? kTheme.text : kTheme.text_muted,\n"
    "              focused || hovered || running ? kRedHot : kRedMuted);\n",
    "dock hover glyph")

text = replace_once(
    text,
    "void dock_task(const Rect& bounds, const char* title, bool focused, bool minimized) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = focused\n"
    "        ? graphics::rgb(49, 20, 25)\n"
    "        : (minimized ? graphics::rgb(10, 11, 13) : kGraphite);\n"
    "    const graphics::Color signal = focused\n"
    "        ? kRedBright\n"
    "        : (minimized ? kInactiveSignal : kSteel);\n",
    "void dock_task(\n"
    "    const Rect& bounds, const char* title, bool focused, bool minimized,\n"
    "    bool hovered, bool pressed) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = pressed\n"
    "        ? graphics::rgb(65, 18, 25)\n"
    "        : (focused ? graphics::rgb(49, 20, 25)\n"
    "                   : (hovered ? graphics::rgb(31, 27, 31)\n"
    "                              : (minimized ? graphics::rgb(10, 11, 13) : kGraphite)));\n"
    "    const graphics::Color signal = pressed || focused\n"
    "        ? kRedBright\n"
    "        : (hovered ? kRedMuted : (minimized ? kInactiveSignal : kSteel));\n",
    "dock task pointer rendering")
text = replace_once(
    text,
    "                        focused ? kRedMuted : kTheme.border);\n",
    "                        focused || hovered ? kRedMuted : kTheme.border);\n",
    "dock task hover border")

text = replace_once(
    text,
    "void button(const Rect& bounds, const char* text, bool selected) {\n"
    "    const auto background = selected ? graphics::rgb(55, 20, 26) : kTheme.panel_alt;\n"
    "    const auto signal = selected ? kRedBright : kTheme.border;\n",
    "void button(\n"
    "    const Rect& bounds, const char* text, bool selected,\n"
    "    bool hovered, bool pressed) {\n"
    "    const auto background = pressed\n"
    "        ? graphics::rgb(72, 18, 27)\n"
    "        : (selected ? graphics::rgb(55, 20, 26)\n"
    "                    : (hovered ? graphics::rgb(34, 28, 32) : kTheme.panel_alt));\n"
    "    const auto signal = pressed || selected\n"
    "        ? kRedBright : (hovered ? kRedMuted : kTheme.border);\n",
    "button pointer rendering")

text = replace_once(
    text,
    "void app_tile(\n"
    "    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,\n"
    "    bool selected, bool pinned, bool running) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = selected\n"
    "        ? graphics::rgb(49, 20, 27)\n"
    "        : (running ? graphics::rgb(24, 22, 26) : kGraphite);\n"
    "    const graphics::Color border = selected\n"
    "        ? kRedBright : (running ? kSteel : kTheme.border);\n",
    "void app_tile(\n"
    "    const Rect& bounds, const char* title, const char* subtitle, AppIcon icon,\n"
    "    bool selected, bool pinned, bool running, bool hovered, bool pressed) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = pressed\n"
    "        ? graphics::rgb(70, 17, 27)\n"
    "        : (selected ? graphics::rgb(49, 20, 27)\n"
    "                    : (hovered ? graphics::rgb(32, 28, 33)\n"
    "                               : (running ? graphics::rgb(24, 22, 26) : kGraphite)));\n"
    "    const graphics::Color border = pressed || selected\n"
    "        ? kRedBright : (hovered ? kRedMuted : (running ? kSteel : kTheme.border));\n",
    "tile pointer rendering")
text = replace_once(
    text,
    "                        selected ? kRedBright : (running ? kRedMuted : kRedDeep));\n"
    "    if (selected) {\n",
    "                        pressed || selected ? kRedBright\n"
    "                                            : (hovered || running ? kRedMuted : kRedDeep));\n"
    "    if (selected || hovered) {\n",
    "tile hover signal")
text = replace_once(
    text,
    "    app_icon_glyph(bounds, icon, kTheme.text, selected ? kRedBright : kRedMuted);\n",
    "    app_icon_glyph(\n"
    "        bounds, icon, kTheme.text,\n"
    "        selected || hovered ? kRedBright : kRedMuted);\n",
    "tile hover icon")

text = replace_once(
    text,
    "void list_row(\n"
    "    const Rect& bounds, const char* text, bool selected, bool disabled) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = selected\n"
    "        ? graphics::rgb(43, 20, 25) : kGraphite;\n"
    "    const graphics::Color signal = selected\n"
    "        ? kRedBright : (disabled ? kInactiveSignal : kSteel);\n",
    "void list_row(\n"
    "    const Rect& bounds, const char* text, bool selected, bool disabled,\n"
    "    bool hovered, bool pressed) {\n"
    "    if (bounds.width <= 0 || bounds.height <= 0) return;\n"
    "    const graphics::Color background = pressed\n"
    "        ? graphics::rgb(61, 18, 25)\n"
    "        : (selected ? graphics::rgb(43, 20, 25)\n"
    "                    : (hovered ? graphics::rgb(30, 27, 31) : kGraphite));\n"
    "    const graphics::Color signal = pressed || selected\n"
    "        ? kRedBright : (hovered ? kRedMuted : (disabled ? kInactiveSignal : kSteel));\n",
    "list pointer rendering")
text = replace_once(
    text,
    "                        selected ? kRedMuted : kTheme.border);\n",
    "                        selected || hovered ? kRedMuted : kTheme.border);\n",
    "list hover border")
write(path, text)

# ----- Native retained-frame renderer derives transient hover/press from real pointer state.
path = "kernel/user/runtime_base.inc"
text = read(path)
text = replace_once(
    text,
    "        const ui::Rect bounds = native_bounds(content, command);\n"
    "        const bool selected = (command.flags & KU_UI_NATIVE_SELECTED) != 0U;\n"
    "        const bool disabled = (command.flags & KU_UI_NATIVE_DISABLED) != 0U;\n",
    "        const ui::Rect bounds = native_bounds(content, command);\n"
    "        const bool selected = (command.flags & KU_UI_NATIVE_SELECTED) != 0U;\n"
    "        const bool disabled = (command.flags & KU_UI_NATIVE_DISABLED) != 0U;\n"
    "        const bool hovered = ui::contains(\n"
    "            bounds, input::pointer_x(), input::pointer_y());\n"
    "        const bool pressed = hovered &&\n"
    "            (input::pointer_buttons() & UINT8_C(1)) != 0U;\n",
    "native pointer state")
text = replace_once(
    text,
    "                ui::button(bounds, command.text, selected);\n",
    "                ui::button(bounds, command.text, selected, hovered, pressed);\n",
    "native button hover")
text = replace_once(
    text,
    "                ui::list_row(bounds, command.text, selected, disabled);\n",
    "                ui::list_row(\n"
    "                    bounds, command.text, selected, disabled, hovered, pressed);\n",
    "native list hover")
text = replace_once(
    text,
    "                    (command.flags & KU_UI_NATIVE_PINNED) != 0U,\n"
    "                    (command.flags & KU_UI_NATIVE_RUNNING) != 0U);\n",
    "                    (command.flags & KU_UI_NATIVE_PINNED) != 0U,\n"
    "                    (command.flags & KU_UI_NATIVE_RUNNING) != 0U,\n"
    "                    hovered, pressed);\n",
    "native tile hover")
write(path, text)

# ----- Window Core: bounded pointer-feedback damage and idempotent focus.
path = "kernel/ui/window_manager.cpp"
text = read(path)
text = replace_once(
    text,
    "int32_t g_drag_offset_x = 0;\nint32_t g_drag_offset_y = 0;\nDirtyMode g_dirty = DirtyMode::None;\n",
    "int32_t g_drag_offset_x = 0;\n"
    "int32_t g_drag_offset_y = 0;\n"
    "int32_t g_pointer_feedback_x = 0;\n"
    "int32_t g_pointer_feedback_y = 0;\n"
    "bool g_pointer_feedback_valid = false;\n"
    "DirtyMode g_dirty = DirtyMode::None;\n",
    "pointer feedback state")

hit_anchor = "Status activate_ribbon_item(size_t position) {\n"
helper = r'''void add_pointer_feedback_damage_at(int32_t x, int32_t y) {
    if (!g_initialized) return;
    if (login_surface() == nullptr) {
        const WorkspaceGeometry workspace = calculate_workspace();
        if (rect_contains(workspace.pulse_ribbon, x, y)) {
            for (size_t index = 0U; index < DOCK_PIN_COUNT; ++index) {
                const ui::Rect pin = dock_pin_rect(index);
                if (rect_contains(pin, x, y)) {
                    add_damage_region(pin);
                    return;
                }
            }
            const size_t tasks = exposed_window_count();
            for (size_t position = 0U; position < tasks; ++position) {
                const ui::Rect task = ribbon_item_rect(position);
                if (rect_contains(task, x, y)) {
                    add_damage_region(task);
                    return;
                }
            }
        }
    }

    const WindowId target = hit_test(x, y);
    if (target != INVALID_WINDOW) {
        Slot* slot = find(target);
        if (slot != nullptr && slot->info.state != WindowState::Minimized) {
            add_damage_region(slot->info.bounds);
        }
        return;
    }

    if (login_surface() != nullptr) return;
    for (size_t app = 0U; app < DOCK_PIN_COUNT; ++app) {
        if (!g_desktop_pinned[app]) continue;
        const ui::Rect shortcut = desktop_shortcut_rect(app);
        if (rect_contains(shortcut, x, y)) {
            add_damage_region(shortcut);
            return;
        }
    }
}

void update_pointer_feedback_damage(const input::Event& event) {
    if (event.type != input::EventType::MouseMove &&
        event.type != input::EventType::MouseButtonDown &&
        event.type != input::EventType::MouseButtonUp) return;
    if (g_pointer_feedback_valid) {
        add_pointer_feedback_damage_at(g_pointer_feedback_x, g_pointer_feedback_y);
    }
    add_pointer_feedback_damage_at(event.x, event.y);
    g_pointer_feedback_x = event.x;
    g_pointer_feedback_y = event.y;
    g_pointer_feedback_valid = true;
}

'''
text = replace_once(text, hit_anchor, helper + hit_anchor, "pointer feedback helper")

text = replace_once(
    text,
    "    g_resized = INVALID_WINDOW;\n#ifndef KUROGANE_HOST_TEST\n",
    "    g_resized = INVALID_WINDOW;\n"
    "    g_pointer_feedback_x = 0;\n"
    "    g_pointer_feedback_y = 0;\n"
    "    g_pointer_feedback_valid = false;\n"
    "#ifndef KUROGANE_HOST_TEST\n",
    "pointer feedback initialize")

text = replace_once(
    text,
    "    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;\n"
    "    if (slot->info.state == WindowState::Minimized) return Status::InvalidState;\n"
    "    size_t position = 0U;\n",
    "    if (slot == nullptr || !exposed(*slot)) return Status::NotFound;\n"
    "    if (slot->info.state == WindowState::Minimized) return Status::InvalidState;\n"
    "    if (g_focused == id) return Status::Ok;\n"
    "    size_t position = 0U;\n",
    "idempotent focus")

text = replace_once(
    text,
    "Status dispatch(const input::Event& event) {\n    if (!g_initialized) return Status::NotInitialized;\n\n",
    "Status dispatch(const input::Event& event) {\n"
    "    if (!g_initialized) return Status::NotInitialized;\n"
    "    update_pointer_feedback_damage(event);\n\n",
    "dispatch pointer damage")

# Draw window chrome with transient pointer feedback.
text = replace_once(
    text,
    "    const ChromeGeometry chrome = calculate_chrome(bounds);\n"
    "    ui::flux_window(bounds, slot.info.title, slot.info.focused);\n"
    "    ui::flux_control(chrome.minimize_control, ui::FluxControl::Minimize, slot.info.focused);\n"
    "    ui::flux_control(\n"
    "        chrome.expand_control,\n"
    "        ui::FluxControl::Expand,\n"
    "        slot.info.state == WindowState::Maximized);\n"
    "    ui::flux_control(chrome.dismiss_control, ui::FluxControl::Dismiss, slot.info.focused);\n",
    "    const ChromeGeometry chrome = calculate_chrome(bounds);\n"
    "    const int32_t pointer_x = input::pointer_x();\n"
    "    const int32_t pointer_y = input::pointer_y();\n"
    "    const bool primary_down =\n"
    "        (input::pointer_buttons() & drivers::mouse::Left) != 0U;\n"
    "    const bool minimize_hovered =\n"
    "        ui::contains(chrome.minimize_control, pointer_x, pointer_y);\n"
    "    const bool expand_hovered =\n"
    "        ui::contains(chrome.expand_control, pointer_x, pointer_y);\n"
    "    const bool dismiss_hovered =\n"
    "        ui::contains(chrome.dismiss_control, pointer_x, pointer_y);\n"
    "    ui::flux_window(bounds, slot.info.title, slot.info.focused);\n"
    "    ui::flux_control(\n"
    "        chrome.minimize_control, ui::FluxControl::Minimize, slot.info.focused,\n"
    "        minimize_hovered, minimize_hovered && primary_down);\n"
    "    ui::flux_control(\n"
    "        chrome.expand_control, ui::FluxControl::Expand,\n"
    "        slot.info.state == WindowState::Maximized,\n"
    "        expand_hovered, expand_hovered && primary_down);\n"
    "    ui::flux_control(\n"
    "        chrome.dismiss_control, ui::FluxControl::Dismiss, slot.info.focused,\n"
    "        dismiss_hovered, dismiss_hovered && primary_down);\n",
    "window chrome feedback")

# Desktop/dock render gets hover/press without userspace round-trips.
text = replace_once(
    text,
    "    const ui::Theme& theme = ui::default_theme();\n",
    "    const ui::Theme& theme = ui::default_theme();\n"
    "    const int32_t pointer_x = input::pointer_x();\n"
    "    const int32_t pointer_y = input::pointer_y();\n"
    "    const bool primary_down =\n"
    "        (input::pointer_buttons() & drivers::mouse::Left) != 0U;\n",
    "render pointer snapshot")
text = replace_once(
    text,
    "        ui::dock_item(\n"
    "            shortcut_icon,\n"
    "            kDockPins[app].icon,\n"
    "            running != nullptr,\n"
    "            running != nullptr && running->info.focused);\n",
    "        const bool shortcut_hovered =\n"
    "            ui::contains(shortcut, pointer_x, pointer_y);\n"
    "        ui::dock_item(\n"
    "            shortcut_icon, kDockPins[app].icon, running != nullptr,\n"
    "            running != nullptr && running->info.focused, shortcut_hovered,\n"
    "            shortcut_hovered && primary_down);\n",
    "desktop shortcut feedback")
text = replace_once(
    text,
    "        if (index == 0U) {\n"
    "            // The session-root Home surface is also the system application\n"
    "            // menu. Present it as an explicit Start-style APPS button while\n"
    "            // keeping the HOME desktop shortcut permanently pinned.\n"
    "            ui::button(dock_pin_rect(index), \"APPS\", active);\n"
    "        } else {\n"
    "            ui::dock_item(\n"
    "                dock_pin_rect(index), kDockPins[index].icon,\n"
    "                running != nullptr, active);\n"
    "        }\n",
    "        const ui::Rect pin_bounds = dock_pin_rect(index);\n"
    "        const bool pin_hovered = ui::contains(pin_bounds, pointer_x, pointer_y);\n"
    "        if (index == 0U) {\n"
    "            // The session-root Home surface is also the system application\n"
    "            // menu. Present it as an explicit Start-style APPS button while\n"
    "            // keeping the HOME desktop shortcut permanently pinned.\n"
    "            ui::button(\n"
    "                pin_bounds, \"APPS\", active, pin_hovered,\n"
    "                pin_hovered && primary_down);\n"
    "        } else {\n"
    "            ui::dock_item(\n"
    "                pin_bounds, kDockPins[index].icon, running != nullptr, active,\n"
    "                pin_hovered, pin_hovered && primary_down);\n"
    "        }\n",
    "dock pin feedback")
text = replace_once(
    text,
    "        ui::dock_task(\n"
    "            ribbon_item_rect(position),\n"
    "            slot->info.title,\n"
    "            slot->info.focused,\n"
    "            slot->info.state == WindowState::Minimized);\n",
    "        const ui::Rect task_bounds = ribbon_item_rect(position);\n"
    "        const bool task_hovered =\n"
    "            ui::contains(task_bounds, pointer_x, pointer_y);\n"
    "        ui::dock_task(\n"
    "            task_bounds, slot->info.title, slot->info.focused,\n"
    "            slot->info.state == WindowState::Minimized, task_hovered,\n"
    "            task_hovered && primary_down);\n",
    "dock task feedback")
write(path, text)

# ----- Host regression proves hover is bounded damage and focus is not full-redrawn.
path = "tests/test_window_manager.cpp"
text = read(path)
anchor = "    if (!render_if_needed()) return 34;\n    invalidate_region({10, 10, 20, 20});\n"
test = r'''    if (!render_if_needed()) return 34;
    event = {};
    event.type = input::EventType::MouseMove;
    event.x = 100;
    event.y = 120;
    if (dispatch(event) != Status::Ok || damage_snapshot(&damage) != Status::Ok ||
        damage.full || damage.count == 0U || damage.count > 2U) return 133;
    {
        bool found_damage_window = false;
        for (size_t index = 0U; index < damage.count; ++index) {
            if (damage.regions[index].x == 80 && damage.regions[index].y == 90 &&
                damage.regions[index].width == 300 && damage.regions[index].height == 220) {
                found_damage_window = true;
            }
        }
        if (!found_damage_window) return 134;
    }
    if (!render_if_needed()) return 135;
    if (focus(damage_window) != Status::Ok ||
        damage_snapshot(&damage) != Status::Ok || damage.full || damage.count != 0U) {
        return 136;
    }
    invalidate_region({10, 10, 20, 20});
'''
text = replace_once(text, anchor, test, "pointer damage host regression")
write(path, text)

path = "tests/test_mouse_first_apps.py"
text = read(path)
anchor = 'browser = read("userspace/gui/browser/main.c")\n'
contract = (
    'pointer_runtime = read("kernel/user/runtime_base.inc")\n'
    'assert "const bool hovered = ui::contains" in pointer_runtime\n'
    'assert "hovered, pressed" in pointer_runtime\n'
    'window_core = read("kernel/ui/window_manager.cpp")\n'
    'assert "update_pointer_feedback_damage(event);" in window_core\n'
    'assert "if (g_focused == id) return Status::Ok;" in window_core\n'
    'ui_core = read("kernel/ui/ui.cpp")\n'
    'assert "bool hovered, bool pressed" in ui_core\n\n'
)
text = replace_once(text, anchor, contract + anchor, "pointer feedback source contract")
write(path, text)

print("Flux pointer feedback migration applied")
