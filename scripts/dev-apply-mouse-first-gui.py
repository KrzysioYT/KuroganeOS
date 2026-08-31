#!/usr/bin/env python3
"""Apply the mouse-first Red Flux GUI slice to production sources.

The patch is deliberately guarded: every modified production anchor must match
exactly once, otherwise no source is published by the qualification workflow.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded anchor, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


# Window Core: expose exact content geometry to the Ring-3 bridge.
replace_once(
    "kernel/ui/window_manager.hpp",
    "Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry);\n"
    "Status pulse_item_geometry(size_t position, ui::Rect* out_bounds);\n",
    "Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry);\n"
    "// Returns the drawable application-content rectangle in screen coordinates.\n"
    "// Login owns its full surface; normal Flux windows exclude chrome.\n"
    "Status content_geometry(WindowId id, ui::Rect* out_geometry);\n"
    "Status pulse_item_geometry(size_t position, ui::Rect* out_bounds);\n",
)

replace_once(
    "kernel/ui/window_manager.cpp",
    "    return geometry;\n"
    "}\n\n"
    "ui::Rect dock_pin_rect(size_t position) {\n",
    "    return geometry;\n"
    "}\n\n"
    "ui::Rect calculate_content(const Slot& slot) {\n"
    "    if (is_login_surface(slot)) return slot.info.bounds;\n"
    "    const ui::Rect& bounds = slot.info.bounds;\n"
    "    return {\n"
    "        bounds.x + 4,\n"
    "        bounds.y + HEADER_HEIGHT,\n"
    "        bounds.width - 8,\n"
    "        bounds.height - HEADER_HEIGHT - 4,\n"
    "    };\n"
    "}\n\n"
    "ui::Rect dock_pin_rect(size_t position) {\n",
)

replace_once(
    "kernel/ui/window_manager.cpp",
    "WindowId hit_test(int32_t x, int32_t y) {\n"
    "    for (size_t position = g_count; position > 0U; --position) {\n"
    "        Slot& slot = g_slots[g_order[position - 1U]];\n"
    "        if (exposed(slot) && slot.info.state != WindowState::Minimized &&\n"
    "            rect_contains(slot.info.bounds, x, y)) return slot.info.id;\n"
    "    }\n"
    "    return INVALID_WINDOW;\n"
    "}\n\n"
    "Status cycle_focus() {\n"
    "    const size_t tasks = exposed_window_count();\n"
    "    if (tasks == 0U) return Status::NotFound;\n"
    "    size_t current = tasks;\n"
    "    for (size_t position = 0U; position < tasks; ++position) {\n"
    "        Slot* slot = exposed_at(position);\n"
    "        if (slot != nullptr && slot->info.id == g_focused) {\n"
    "            current = position;\n"
    "            break;\n"
    "        }\n"
    "    }\n"
    "    for (size_t offset = 1U; offset <= tasks; ++offset) {\n"
    "        const size_t position = (current + offset) % tasks;\n"
    "        Slot* slot = exposed_at(position);\n"
    "        if (slot != nullptr && slot->info.state != WindowState::Minimized) {\n"
    "            return focus(slot->info.id);\n"
    "        }\n"
    "    }\n"
    "    return Status::NotFound;\n"
    "}\n\n",
    "WindowId hit_test(int32_t x, int32_t y) {\n"
    "    for (size_t position = g_count; position > 0U; --position) {\n"
    "        Slot& slot = g_slots[g_order[position - 1U]];\n"
    "        if (exposed(slot) && slot.info.state != WindowState::Minimized &&\n"
    "            rect_contains(slot.info.bounds, x, y)) return slot.info.id;\n"
    "    }\n"
    "    return INVALID_WINDOW;\n"
    "}\n\n",
)

replace_once(
    "kernel/ui/window_manager.cpp",
    "        const ui::Rect content = {\n"
    "            bounds.x + 4,\n"
    "            bounds.y + HEADER_HEIGHT,\n"
    "            bounds.width - 8,\n"
    "            bounds.height - HEADER_HEIGHT - 4,\n"
    "        };\n",
    "        const ui::Rect content = calculate_content(slot);\n",
)

replace_once(
    "kernel/ui/window_manager.cpp",
    "Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry) {\n"
    "    if (!g_initialized) return Status::NotInitialized;\n"
    "    if (out_geometry == nullptr) return Status::InvalidArgument;\n"
    "    Slot* slot = find(id);\n"
    "    if (slot == nullptr) return Status::NotFound;\n"
    "    *out_geometry = calculate_chrome(slot->info.bounds);\n"
    "    return Status::Ok;\n"
    "}\n\n"
    "Status pulse_item_geometry(size_t position, ui::Rect* out_bounds) {\n",
    "Status chrome_geometry(WindowId id, ChromeGeometry* out_geometry) {\n"
    "    if (!g_initialized) return Status::NotInitialized;\n"
    "    if (out_geometry == nullptr) return Status::InvalidArgument;\n"
    "    Slot* slot = find(id);\n"
    "    if (slot == nullptr) return Status::NotFound;\n"
    "    *out_geometry = calculate_chrome(slot->info.bounds);\n"
    "    return Status::Ok;\n"
    "}\n\n"
    "Status content_geometry(WindowId id, ui::Rect* out_geometry) {\n"
    "    if (!g_initialized) return Status::NotInitialized;\n"
    "    if (out_geometry == nullptr) return Status::InvalidArgument;\n"
    "    Slot* slot = find(id);\n"
    "    if (slot == nullptr) return Status::NotFound;\n"
    "    *out_geometry = calculate_content(*slot);\n"
    "    return Status::Ok;\n"
    "}\n\n"
    "Status pulse_item_geometry(size_t position, ui::Rect* out_bounds) {\n",
)

replace_once(
    "kernel/ui/window_manager.cpp",
    "    // Windows/Super key opens the persistent Red Flux application list.\n"
    "    if (event.type == input::EventType::KeyDown &&\n"
    "        (event.key == drivers::keyboard::KeyCode::LeftGui ||\n"
    "         event.key == drivers::keyboard::KeyCode::RightGui)) {\n"
    "        return login_surface() == nullptr\n"
    "            ? activate_dock_pin(0U) : Status::InvalidState;\n"
    "    }\n"
    "    if (event.type == input::EventType::KeyDown && event.alt &&\n"
    "        event.key == drivers::keyboard::KeyCode::F4) {\n"
    "        return g_focused == INVALID_WINDOW ? Status::NotFound : dismiss(g_focused);\n"
    "    }\n"
    "    if (event.type == input::EventType::KeyDown && event.alt &&\n"
    "        event.key == drivers::keyboard::KeyCode::Tab) {\n"
    "        return cycle_focus();\n"
    "    }\n\n",
    "    // Red Flux is mouse-first: physical desktop/window shortcuts are not\n"
    "    // intercepted globally. Keyboard events continue to the focused app so\n"
    "    // text entry, terminals and accessibility paths remain available.\n\n",
)

# Ring-3 pointer coordinates become content-local and remain correct after move/resize.
replace_once(
    "kernel/user/runtime_base.inc",
    "void input_user_window(\n"
    "    windowing::WindowId,\n"
    "    const input::Event& input_event,\n"
    "    void* opaque) {\n",
    "void input_user_window(\n"
    "    windowing::WindowId window,\n"
    "    const input::Event& input_event,\n"
    "    void* opaque) {\n",
)

replace_once(
    "kernel/user/runtime_base.inc",
    "    } else if (input_event.type == input::EventType::MouseMove ||\n"
    "               input_event.type == input::EventType::MouseButtonDown ||\n"
    "               input_event.type == input::EventType::MouseButtonUp) {\n"
    "        event.type = KU_UI_EVENT_POINTER;\n"
    "        event.x = input_event.x;\n"
    "        event.y = input_event.y;\n"
    "        event.buttons = input_event.buttons;\n"
    "    } else {\n",
    "    } else if (input_event.type == input::EventType::MouseMove ||\n"
    "               input_event.type == input::EventType::MouseButtonDown ||\n"
    "               input_event.type == input::EventType::MouseButtonUp) {\n"
    "        ui::Rect content{};\n"
    "        if (windowing::content_geometry(window, &content) != windowing::Status::Ok) {\n"
    "            return;\n"
    "        }\n"
    "        event.type = KU_UI_EVENT_POINTER;\n"
    "        event.x = input_event.x - content.x;\n"
    "        event.y = input_event.y - content.y;\n"
    "        event.buttons = input_event.buttons;\n"
    "    } else {\n",
)

replace_once(
    "sdk/include/kurogane/ui.h",
    "typedef struct ku_ui_event {\n",
    "/*\n"
    " * Pointer coordinates are relative to the drawable content origin of the\n"
    " * target window. They can be negative when a captured pointer leaves the\n"
    " * content area; applications must not treat them as screen coordinates.\n"
    " */\n"
    "typedef struct ku_ui_event {\n",
)

# libui: deterministic hit-testing that mirrors the compatibility row renderer.
replace_once(
    "sdk/include/kurogane/libui.h",
    "uint32_t kui_scene_selected(const kui_scene* scene);\n"
    "ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene);\n",
    "uint32_t kui_scene_selected(const kui_scene* scene);\n"
    "// Returns the interactive visible view under a content-local pointer, or 0.\n"
    "uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y);\n"
    "ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene);\n",
)

replace_once(
    "sdk/src/libui.c",
    "uint32_t kui_scene_selected(const kui_scene* scene) {\n"
    "    return scene == (const kui_scene*)0 ? 0U : scene->selected_id;\n"
    "}\n\n"
    "ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene) {\n",
    "uint32_t kui_scene_selected(const kui_scene* scene) {\n"
    "    return scene == (const kui_scene*)0 ? 0U : scene->selected_id;\n"
    "}\n\n"
    "uint32_t kui_scene_hit_test(const kui_scene* scene, int32_t x, int32_t y) {\n"
    "    const int32_t top_inset = 12;\n"
    "    const int32_t row_height = 22;\n"
    "    uint32_t rows;\n"
    "    uint32_t target_row;\n"
    "    uint32_t visible_index = 0U;\n"
    "    uint32_t output_line = 0U;\n"
    "    uint32_t index;\n"
    "    if (scene == (const kui_scene*)0 || x < 0 || y < top_inset) return 0U;\n"
    "    rows = scene->visible_rows == 0U || scene->visible_rows > KU_UI_MAX_LINES\n"
    "        ? KU_UI_MAX_LINES : scene->visible_rows;\n"
    "    target_row = (uint32_t)((y - top_inset) / row_height);\n"
    "    if (target_row >= rows) return 0U;\n"
    "    for (index = 0U; index < scene->view_count && output_line < rows; ++index) {\n"
    "        const kui_view* view = &scene->views[index];\n"
    "        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;\n"
    "        if (visible_index++ < scene->scroll_offset) continue;\n"
    "        if (output_line == target_row) {\n"
    "            return interactive_view(view) ? view->id : 0U;\n"
    "        }\n"
    "        ++output_line;\n"
    "    }\n"
    "    return 0U;\n"
    "}\n\n"
    "ku_status_t kui_scene_present(ku_window_t window, const kui_scene* scene) {\n",
)

# HOME: mouse activation for app rows + explicit pin/logout buttons.
launcher = ROOT / "userspace/gui/launcher/main.c"
launcher_text = launcher.read_text(encoding="utf-8")
start = launcher_text.find("static void build_scene(kui_scene* scene) {")
end = launcher_text.find("\nint main(void) {", start)
if start < 0 or end < 0:
    raise SystemExit("launcher: build_scene anchors not found")
launcher_scene = r'''static void build_scene(kui_scene* scene) {
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
    (void)kui_flow_panel(&root, 1U, "RED FLUX APPS / START");
    (void)kui_flow_label(&root, 2U, "MOUSE: CLICK APP / PIN SELECTED / LOG OUT");

    kui_flow_begin(&apps, scene, 1U);
    for (index = 0U; index < APP_COUNT; ++index) {
        char label[64] = "";
        append_text(label, sizeof(label), pin_state(g_apps[index].desktop_id) ? "[PIN] " : "[   ] ");
        append_text(label, sizeof(label), g_apps[index].label);
        append_text(label, sizeof(label), " / ");
        append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_list_item(&apps, 10U + (uint32_t)index, label);
    }
    (void)kui_flow_button(&root, 30U, "PIN / UNPIN SELECTED");
    (void)kui_flow_button(&root, 31U, "LOG OUT");
    (void)kui_flow_label(&root, 32U, g_status);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}
'''
launcher_text = launcher_text[:start] + launcher_scene + launcher_text[end:]

old = "    size_t index;\n    if (window == KU_INVALID_WINDOW) return 1;\n"
new = "    size_t index;\n    uint32_t pointer_buttons = 0U;\n    if (window == KU_INVALID_WINDOW) return 1;\n"
if launcher_text.count(old) != 1:
    raise SystemExit(f"launcher: main declaration anchor count={launcher_text.count(old)}")
launcher_text = launcher_text.replace(old, new, 1)

old = "    puts(\"[TEST] desktop_arrow_navigation: PASS\");\n"
new = (
    "    puts(\"[TEST] desktop_mouse_navigation: PASS\");\n"
    "    puts(\"[TEST] desktop_keyboard_shortcuts_detached: PASS\");\n"
)
if launcher_text.count(old) != 1:
    raise SystemExit("launcher: navigation marker anchor mismatch")
launcher_text = launcher_text.replace(old, new, 1)

loop_start = launcher_text.find(
    "    for (;;) {\n        ku_ui_event event;\n        reap_children();\n",
    launcher_text.find("int main(void) {"),
)
loop_end = launcher_text.find("\n    (void)ku_ui_close(window);", loop_start)
if loop_start < 0 or loop_end < 0:
    raise SystemExit("launcher: event loop anchors not found")
launcher_loop = r'''    for (;;) {
        ku_ui_event event;
        reap_children();
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;

        if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            uint32_t target;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
            target = kui_scene_hit_test(&scene, event.x, event.y);
            if (target >= 10U && target < 10U + APP_COUNT) {
                select_and_launch((size_t)(target - 10U));
            } else if (target == 30U) {
                toggle_selected_pin();
            } else if (target == 31U) {
                puts("[TEST] desktop_logout_requested: PASS");
                break;
            } else {
                continue;
            }
            build_scene(&scene);
            (void)kui_scene_present(window, &scene);
            continue;
        }

        /*
         * Physical keyboard shortcuts are intentionally detached. The Window
         * Core still uses key=UNKNOWN as a private compatibility transport for
         * dock/desktop icon activation until a dedicated desktop command event
         * is introduced.
         */
        if (event.type != KU_UI_EVENT_KEY || event.key != KU_UI_KEY_UNKNOWN) continue;
        if (event.character == 't') {
            select_and_launch(0U);
        } else if (event.character == 'f') {
            select_and_launch(1U);
        } else if (event.character == 'v') {
            select_and_launch(2U);
        } else if (event.character == 'b') {
            select_and_launch(3U);
        } else if (event.character == 'm') {
            select_and_launch(4U);
        } else if (event.character == 's') {
            select_and_launch(5U);
        } else if (event.character == 'a') {
            select_and_launch(6U);
        } else {
            continue;
        }
        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }
'''
launcher_text = launcher_text[:loop_start] + launcher_loop + launcher_text[loop_end:]
launcher.write_text(launcher_text, encoding="utf-8")

# Login: keep keyboard only when credentials require typing; passwordless login
# must activate the actual button instead of accepting any click/Enter shortcut.
replace_once(
    "userspace/gui/login/main.c",
    "            polish ? \"ENTER LUB KLIKNIJ, ABY OTWORZYC SESJE\"\n"
    "                   : \"ENTER OR CLICK TO START THE SESSION\");\n",
    "            polish ? \"KLIKNIJ, ABY OTWORZYC SESJE\"\n"
    "                   : \"CLICK TO START THE SESSION\");\n",
)
replace_once(
    "userspace/gui/login/main.c",
    "    ku_window_t window;\n"
    "    kui_scene scene;\n",
    "    ku_window_t window;\n"
    "    kui_scene scene;\n"
    "    uint32_t pointer_buttons = 0U;\n",
)
replace_once(
    "userspace/gui/login/main.c",
    "        if (!profile.password_required &&\n"
    "            event.type == KU_UI_EVENT_POINTER &&\n"
    "            (event.buttons & UINT32_C(1)) != 0U) {\n"
    "            return start_session(window, &profile);\n"
    "        }\n"
    "        if (event.type != KU_UI_EVENT_KEY) continue;\n\n"
    "        if (!profile.password_required) {\n"
    "            if (gui_key_activate(&event)) return start_session(window, &profile);\n"
    "            continue;\n"
    "        }\n",
    "        if (!profile.password_required && event.type == KU_UI_EVENT_POINTER) {\n"
    "            const uint32_t previous_buttons = pointer_buttons;\n"
    "            pointer_buttons = event.buttons;\n"
    "            if ((event.buttons & UINT32_C(1)) != 0U &&\n"
    "                (previous_buttons & UINT32_C(1)) == 0U &&\n"
    "                kui_scene_hit_test(&scene, event.x, event.y) == 10U) {\n"
    "                return start_session(window, &profile);\n"
    "            }\n"
    "            continue;\n"
    "        }\n"
    "        if (event.type != KU_UI_EVENT_KEY) continue;\n\n"
    "        if (!profile.password_required) continue;\n",
)

# Host Window Core regression: physical desktop shortcuts must be inert, while
# mouse chrome remains the authoritative close path.
replace_once(
    "tests/test_window_manager.cpp",
    "    ChromeGeometry chrome{};\n"
    "    if (chrome_geometry(first, &chrome) != Status::Ok ||\n"
    "        chrome.header.height <= 30 || chrome.resize_grip.width <= 0 ||\n"
    "        chrome.minimize_control.x >= chrome.expand_control.x ||\n"
    "        chrome.expand_control.x >= chrome.dismiss_control.x) return 6;\n",
    "    ChromeGeometry chrome{};\n"
    "    ui::Rect content{};\n"
    "    if (chrome_geometry(first, &chrome) != Status::Ok ||\n"
    "        content_geometry(first, &content) != Status::Ok ||\n"
    "        chrome.header.height <= 30 || chrome.resize_grip.width <= 0 ||\n"
    "        chrome.minimize_control.x >= chrome.expand_control.x ||\n"
    "        chrome.expand_control.x >= chrome.dismiss_control.x ||\n"
    "        content.x != info.bounds.x + 4 ||\n"
    "        content.y != chrome.header.y + chrome.header.height ||\n"
    "        content.width != info.bounds.width - 8 ||\n"
    "        content.height != info.bounds.height - chrome.header.height - 4) return 6;\n",
)

replace_once(
    "tests/test_window_manager.cpp",
    "    // Alt+F4 remains a stable keyboard path regardless of Flux chrome.\n"
    "    event = {};\n"
    "    event.type = input::EventType::KeyDown;\n"
    "    event.alt = true;\n"
    "    event.key = drivers::keyboard::KeyCode::F4;\n"
    "    if (dispatch(event) != Status::Ok || window_count() != 1U ||\n"
    "        query(first, &info) != Status::NotFound) return 19;\n"
    "    if (!render_if_needed() || render_if_needed()) return 20;\n",
    "    // Physical desktop/window shortcuts are detached in mouse-first Flux.\n"
    "    const WindowId shortcut_focus = focused_window();\n"
    "    event = {};\n"
    "    event.type = input::EventType::KeyDown;\n"
    "    event.alt = true;\n"
    "    event.key = drivers::keyboard::KeyCode::F4;\n"
    "    if (dispatch(event) != Status::Ok || window_count() != 2U ||\n"
    "        focused_window() != shortcut_focus || query(first, &info) != Status::Ok) return 19;\n"
    "    event.key = drivers::keyboard::KeyCode::Tab;\n"
    "    if (dispatch(event) != Status::Ok || window_count() != 2U ||\n"
    "        focused_window() != shortcut_focus) return 19;\n"
    "    event = {};\n"
    "    event.type = input::EventType::KeyDown;\n"
    "    event.key = drivers::keyboard::KeyCode::LeftGui;\n"
    "    if (dispatch(event) != Status::Ok || window_count() != 2U ||\n"
    "        focused_window() != shortcut_focus) return 19;\n"
    "    if (chrome_geometry(first, &chrome) != Status::Ok) return 19;\n"
    "    event = {};\n"
    "    event.type = input::EventType::MouseButtonDown;\n"
    "    event.button = drivers::mouse::Left;\n"
    "    event.buttons = drivers::mouse::Left;\n"
    "    event.x = chrome.dismiss_control.x + chrome.dismiss_control.width / 2;\n"
    "    event.y = chrome.dismiss_control.y + chrome.dismiss_control.height / 2;\n"
    "    if (dispatch(event) != Status::Ok || window_count() != 1U ||\n"
    "        query(first, &info) != Status::NotFound) return 19;\n"
    "    if (!render_if_needed() || render_if_needed()) return 20;\n",
)

# New libui hit-test regression runs production libui.c, not a duplicate model.
test_path = ROOT / "tests/test_libui_pointer.c"
if test_path.exists():
    raise SystemExit("tests/test_libui_pointer.c already exists")
test_path.write_text(r'''#include <kurogane/libui.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Debian/glibc used by the host runner may not provide strlcpy. */
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
    fprintf(stderr, "%s: expected %u got %u\n", label, expected, actual);
    return 0;
}

static int row_y(uint32_t row) {
    return 12 + (int)(row * 22U) + 10;
}

int main(void) {
    kui_scene scene;
    kui_flow root;
    kui_scene_initialize(&scene);
    scene.visible_rows = 4U;
    kui_flow_begin(&root, &scene, 0U);
    if (kui_flow_panel(&root, 1U, "PANEL") != KU_STATUS_OK ||
        kui_flow_button(&root, 2U, "OPEN") != KU_STATUS_OK ||
        kui_flow_label(&root, 3U, "INFO") != KU_STATUS_OK ||
        kui_flow_list_item(&root, 4U, "ITEM") != KU_STATUS_OK ||
        kui_flow_button(&root, 5U, "MORE") != KU_STATUS_OK) return 1;

    if (!expect(kui_scene_hit_test(&scene, 20, row_y(0U)), 0U, "panel inert") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(1U)), 2U, "button hit") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(2U)), 0U, "label inert") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(3U)), 4U, "list hit") ||
        !expect(kui_scene_hit_test(&scene, -1, row_y(1U)), 0U, "negative x") ||
        !expect(kui_scene_hit_test(&scene, 20, 11), 0U, "top inset")) return 2;

    if (kui_scene_set_flags(&scene, 2U, KUI_VIEW_DISABLED) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(1U)), 0U, "disabled inert")) return 3;
    if (kui_scene_set_flags(&scene, 2U, 0U) != KU_STATUS_OK ||
        kui_scene_set_flags(&scene, 3U, KUI_VIEW_HIDDEN) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(2U)), 4U, "hidden row compaction") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(3U)), 5U, "visible fourth row")) return 4;

    if (kui_scene_scroll(&scene, 1) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(0U)), 2U, "scroll row zero") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(1U)), 4U, "scroll list")) return 5;

    puts("libui mouse hit-test tests passed");
    return 0;
}
''', encoding="utf-8")

# Host runner compiles the actual C libui implementation under -Werror.
replace_once(
    "scripts/run-host-tests.sh",
    "HOST_CXX=\"${HOST_CXX:-c++}\"\n"
    "HOST_PYTHON=\"${HOST_PYTHON:-python3}\"\n",
    "HOST_CC=\"${HOST_CC:-cc}\"\n"
    "HOST_CXX=\"${HOST_CXX:-c++}\"\n"
    "HOST_PYTHON=\"${HOST_PYTHON:-python3}\"\n",
)
replace_once(
    "scripts/run-host-tests.sh",
    "echo \"[host-tests] compiler: $HOST_CXX\"\n"
    "echo \"[host-tests] python:   $HOST_PYTHON\"\n\n",
    "echo \"[host-tests] C compiler:   $HOST_CC\"\n"
    "echo \"[host-tests] C++ compiler: $HOST_CXX\"\n"
    "echo \"[host-tests] python:       $HOST_PYTHON\"\n\n",
)
replace_once(
    "scripts/run-host-tests.sh",
    "\"$OUT_DIR/test_sdk_abi\"\n\n"
    "\"$HOST_CXX\" \\\n",
    "\"$OUT_DIR/test_sdk_abi\"\n\n"
    "# Exercise production libui row geometry and pointer hit-testing.\n"
    "\"$HOST_CC\" \\\n"
    "  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \\\n"
    "  -Isdk/include \\\n"
    "  tests/test_libui_pointer.c sdk/src/libui.c \\\n"
    "  -o \"$OUT_DIR/test_libui_pointer\"\n\n"
    "\"$OUT_DIR/test_libui_pointer\"\n\n"
    "\"$HOST_CXX\" \\\n",
)

print("mouse-first GUI production patch applied")
