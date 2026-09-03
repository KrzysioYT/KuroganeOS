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
for control in ("PARENT FOLDER", "OPEN / ENTER", "NEXT PAGE"):
    assert control in files, f"files: missing explorer control {control}"
assert "ku_file_readdir" in files and "KU_FILE_OPEN_DIRECTORY" in files
assert "kui_flow_tile" in files, "files: directory grid is not tile-based"
assert "KU_UI_NATIVE_ICON_FOLDER" in files and "KU_UI_NATIVE_ICON_DOCUMENT" in files
assert "quick_entry" not in files and "g_entries" not in files
assert "flux_files_readdir: PASS" in files
assert "flux_files_directory_grid: PASS" in files
assert "flux_files_breadcrumb: PASS" in files
assert "desktop_files_mouse_navigation: PASS" in files
assert "desktop_files_keyboard_shortcuts_detached: PASS" in files

settings = read("userspace/gui/settings/main.c")
assert "kui_flow_toggle" in settings, "settings: native toggle cards missing"
assert "ku_settings_connect" in settings, "settings: persistent settingsd integration missing"
assert "UI_LOW_CONTRAST_KEY" in settings
assert "desktop_settings_profile_persist: PASS" in settings
assert "desktop_settings_profile_restore: PASS" in settings
for control in ("LOW CONTRAST RED", "MUTE AUDIO", "VOLUME -10", "VOLUME +10", "RESET INTERFACE PROFILE"):
    assert control in settings, f"settings: missing modern control {control}"
assert "RED CORE" not in settings, "settings: legacy theme-button UI returned"
assert "MUTE / UNMUTE" not in settings, "settings: legacy audio button returned"
assert "kui_flow_toggle" in settings, "settings: native toggle cards missing"
assert "ku_settings_connect" in settings, "settings: persistent settingsd integration missing"
assert "write_bool_setting" in settings and "read_bool_setting" in settings
assert "desktop_settings_profile_persist: PASS" in settings
assert "desktop_settings_profile_restore: PASS" in settings
assert "desktop_settings_mouse_navigation: PASS" in settings
assert "desktop_settings_keyboard_shortcuts_detached: PASS" in settings

terminal = read("userspace/gui/terminal/main.c")
assert "KU_UI_EVENT_KEY" in terminal, "terminal: required keyboard input was removed"

login = read("userspace/gui/login/main.c")
assert "KU_UI_EVENT_KEY" in login, "login: password keyboard input was removed"
assert "KU_UI_EVENT_POINTER" in login, "login: mouse activation missing"
assert "if (!profile.password_required)" in login
assert "gui_key_activate(&event)" in login, "login: Enter activation missing for live profile"

window_manager = read("kernel/ui/window_manager.cpp")
assert "slot.info.state = owner_pid == 0U" in window_manager, "desktop: HOME is still minimized on session entry"
assert "owner_pid != 0U && !home" not in window_manager, "desktop: HOME does not receive initial focus"
launcher_session = read("userspace/gui/launcher/main.c")
assert "desktop_performance_autostart" not in launcher_session, "launcher: diagnostic Performance autostart returned"
assert "FLUX DECK / READY" in launcher_session

launcher = read("userspace/gui/launcher/main.c")
assert "kui_flow_tile" in launcher, "launcher: Flux Deck tiles missing"
assert "kui_flow_list_item" not in launcher, "launcher: text-list menu returned"
assert "[PIN]" not in launcher, "launcher: ASCII pin decoration returned"
assert "red_flux_tile_launcher: PASS" in launcher
assert "scene->visible_rows = 16U" in launcher, "launcher: LOG OUT action is clipped"
assert "kui_flow_metric" in launcher, "launcher: System Pulse metric cards missing"
assert "ku_system_get_snapshot" in launcher, "launcher: live CPU/RAM/disk source missing"
assert "ku_network_get_status" in launcher, "launcher: live network source missing"
assert "ku_audio_get_state" in launcher, "launcher: live audio source missing"
assert "flux_home_system_pulse: PASS" in launcher
assert "flux_home_public_notification: PASS" in launcher
assert "/gui/notify" in launcher and "KU_UI_NATIVE_ICON_NOTIFICATION" in launcher
assert "SYSTEM APP / NOT PINNABLE" in launcher
notifications = read("userspace/gui/notifications/main.c")
assert "KU_NOTIFICATION_LIST_PUBLIC" in notifications
assert "kui_flow_notice" in notifications
assert "flux_notification_center_connected: PASS" in notifications
assert "flux_notification_center_public_record: PASS" in notifications
assert "CLICK A CARD TO OPEN" not in launcher, "launcher: instruction-banner UI returned"

control_center = read("userspace/gui/performance/main.c")
assert 'gui_open("FORGE CONTROL"' in control_center
assert '"FORGE CONTROL / LIVE SYSTEM"' in control_center
assert "ku_system_get_snapshot" in control_center
assert "ku_network_get_status" in control_center
assert "ku_audio_get_state" in control_center and "ku_audio_set" in control_center
assert "KU_UI_EVENT_POINTER" in control_center and "kui_scene_hit_test" in control_center
assert "KU_UI_EVENT_KEY" not in control_center
assert "kui_flow_metric" in control_center and "kui_flow_tile" in control_center
assert "kui_flow_progress" in control_center and "kui_flow_toggle" in control_center
assert "FORGE_SECTION_PERFORMANCE" in control_center
assert "FORGE_SECTION_NETWORK" in control_center
assert "FORGE_SECTION_AUDIO" in control_center
assert "flux_control_center_live: PASS" in control_center
assert "forge_control_audio_surface: PASS" in control_center
assert "flux_control_center_audio_action: PASS" in control_center
assert '"FORGE CONTROL"' in launcher
assert '"system pulse / network / audio"' in launcher
assert "\"FORGE CONTROL\", 'v'" in window_manager
assert 'text_equals(title, "CONTROL CENTER")' in window_manager

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
