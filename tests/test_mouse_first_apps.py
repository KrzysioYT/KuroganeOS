#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


for path in ("userspace/gui/files/main.c", "userspace/gui/settings/main.c"):
    text = read(path)
    assert "KU_UI_EVENT_POINTER" in text, f"{path}: pointer path missing"
    assert "kui_scene_hit_test" in text, f"{path}: libui hit test missing"
    assert "KU_UI_EVENT_KEY" not in text, f"{path}: keyboard menu path returned"
    for helper in (
        "gui_key_down(", "gui_key_up(", "gui_key_left(", "gui_key_right(",
        "gui_key_tab(", "gui_key_activate(", "gui_key_cancel(",
    ):
        assert helper not in text, f"{path}: shortcut helper returned: {helper}"

files = read("userspace/gui/files/main.c")
assert "OPEN SELECTED" in files and "REFRESH PREVIEW" in files
assert "desktop_files_mouse_navigation: PASS" in files
assert "desktop_files_keyboard_shortcuts_detached: PASS" in files

settings = read("userspace/gui/settings/main.c")
for control in ("RED CORE", "LOW CONTRAST RED", "VOLUME -10", "VOLUME +10", "MUTE / UNMUTE"):
    assert control in settings, f"settings: missing mouse control {control}"
assert "desktop_settings_mouse_navigation: PASS" in settings
assert "desktop_settings_keyboard_shortcuts_detached: PASS" in settings

terminal = read("userspace/gui/terminal/main.c")
assert "KU_UI_EVENT_KEY" in terminal, "terminal: required keyboard input was removed"

login = read("userspace/gui/login/main.c")
assert "KU_UI_EVENT_KEY" in login, "login: password keyboard input was removed"
assert "KU_UI_EVENT_POINTER" in login, "login: mouse activation missing"

browser = read("userspace/gui/browser/main.c")
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
