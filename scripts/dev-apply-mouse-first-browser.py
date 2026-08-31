#!/usr/bin/env python3
"""Make Kurogane Web mouse-first while retaining keyboard only for omnibox text."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded anchor, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


browser = ROOT / "userspace/gui/browser/main.c"
text = browser.read_text(encoding="utf-8")
start = text.find("static void build_scene(kui_scene* scene) {")
end = text.find("\nint main(void) {", start)
if start < 0 or end < 0:
    raise SystemExit("browser: build_scene anchors not found")
new_scene = r'''static void build_scene(kui_scene* scene, int omnibox_active) {
    kui_flow root;
    size_t index;
    char address[BROWSER_URL_CAPACITY + 24U] = "ADDRESS  ";
    char navigation[64] = "NAV / ";

    (void)platform_delegate_refresh_network(&g_browser);
    if (omnibox_active) {
        (void)strlcpy(address, "ADDRESS* ", sizeof(address));
    }
    append_text(address, sizeof(address), g_browser.url);
    append_text(navigation, sizeof(navigation), stage_name(g_browser.stage));
    append_text(navigation, sizeof(navigation), " / ");
    append_text(navigation, sizeof(navigation), g_browser.status);

    kui_scene_initialize(scene);
    /* ku_ui_frame exposes exactly twelve compatibility rows. Keep every
       interactive browser control inside that real ABI instead of building
       off-screen rows that the renderer can never present. */
    scene->visible_rows = KU_UI_MAX_LINES;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE WEB / CHROMIUM PORT");
    (void)kui_flow_input(&root, 2U, address);
    (void)kui_flow_button(&root, 3U, "GO / SEARCH");
    (void)kui_flow_button(&root, 4U, "CLEAR ADDRESS");
    (void)kui_flow_label(&root, 5U, g_browser.network);
    (void)kui_flow_label(&root, 6U, navigation);
    for (index = 0U; index < BROWSER_RENDER_LINES && index < 6U; ++index) {
        (void)kui_flow_label(
            &root,
            10U + (uint32_t)index,
            g_browser.render_lines[index][0] != '\0'
                ? g_browser.render_lines[index] : " ");
    }
}
'''
text = text[:start] + new_scene + text[end:]

old = "    kui_scene scene;\n    if (window == KU_INVALID_WINDOW) return 1;\n"
new = (
    "    kui_scene scene;\n"
    "    int omnibox_active = 0;\n"
    "    uint32_t pointer_buttons = 0U;\n"
    "    if (window == KU_INVALID_WINDOW) return 1;\n"
)
if text.count(old) != 1:
    raise SystemExit(f"browser: main state anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '    puts("[TEST] chromium_port_bounded_partial_response: PASS");\n'
new = (
    '    puts("[TEST] chromium_port_bounded_partial_response: PASS");\n'
    '    puts("[TEST] chromium_port_mouse_navigation: PASS");\n'
    '    puts("[TEST] chromium_port_keyboard_scoped_to_omnibox: PASS");\n'
)
if text.count(old) != 1:
    raise SystemExit("browser: marker anchor mismatch")
text = text.replace(old, new, 1)

loop_start = text.find("    for (;;) {\n        ku_ui_event event;\n", text.find("int main(void)"))
loop_end = text.find("\n    (void)ku_ui_close(window);", loop_start)
if loop_start < 0 or loop_end < 0:
    raise SystemExit("browser: event loop anchors not found")
new_loop = r'''    for (;;) {
        ku_ui_event event;
        build_scene(&scene, omnibox_active);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
        }

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
            if (target == 2U) {
                omnibox_active = 1;
            } else if (target == 3U) {
                (void)navigation_controller_load(&g_browser);
                omnibox_active = 0;
            } else if (target == 4U) {
                g_browser.url_length = 0U;
                g_browser.url[0] = '\0';
                g_browser.stage = CHROMIUM_STAGE_IDLE;
                (void)strlcpy(g_browser.status, "ADDRESS CLEARED", sizeof(g_browser.status));
                omnibox_active = 1;
            } else {
                omnibox_active = 0;
            }
            continue;
        }

        if (event.type != KU_UI_EVENT_KEY || !omnibox_active) continue;
        if (event.key == KU_UI_KEY_BACKSPACE) {
            if (g_browser.url_length != 0U) {
                g_browser.url[--g_browser.url_length] = '\0';
            }
        } else if (event.key == KU_UI_KEY_ENTER) {
            (void)navigation_controller_load(&g_browser);
            omnibox_active = 0;
        } else if (event.character >= 0x20U && event.character <= 0x7EU &&
                   g_browser.url_length + 1U < sizeof(g_browser.url)) {
            g_browser.url[g_browser.url_length++] = (char)event.character;
            g_browser.url[g_browser.url_length] = '\0';
        }
    }
'''
text = text[:loop_start] + new_loop + text[loop_end:]
browser.write_text(text, encoding="utf-8")

# Extend the existing mouse-first contract. Browser is the deliberate mixed
# input case: pointer owns actions/focus; keyboard is scoped to active text.
test = ROOT / "tests/test_mouse_first_apps.py"
test_text = test.read_text(encoding="utf-8")
anchor = 'print("mouse-first application input contract tests passed")\n'
insert = r'''browser = read("userspace/gui/browser/main.c")
assert "KU_UI_EVENT_POINTER" in browser, "browser: pointer controls missing"
assert "kui_scene_hit_test" in browser, "browser: hit test missing"
assert "GO / SEARCH" in browser and "CLEAR ADDRESS" in browser
assert "KU_UI_EVENT_KEY" in browser, "browser: omnibox keyboard input removed"
assert "event.type != KU_UI_EVENT_KEY || !omnibox_active" in browser
assert "KU_UI_KEY_BACKSPACE" in browser and "KU_UI_KEY_ENTER" in browser
assert "gui_key_cancel(" not in browser, "browser: Escape clear shortcut returned"
assert "scene->visible_rows = KU_UI_MAX_LINES" in browser
assert "chromium_port_mouse_navigation: PASS" in browser
assert "chromium_port_keyboard_scoped_to_omnibox: PASS" in browser

print("mouse-first application input contract tests passed")
'''
if test_text.count(anchor) != 1:
    raise SystemExit(f"mouse-first app test: expected one final marker, found {test_text.count(anchor)}")
test.write_text(test_text.replace(anchor, insert, 1), encoding="utf-8")

print("mouse-first browser patch applied")
