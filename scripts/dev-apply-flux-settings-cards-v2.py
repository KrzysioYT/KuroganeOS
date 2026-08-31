#!/usr/bin/env python3
import runpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
runpy.run_path(str(ROOT / "scripts/dev-apply-flux-settings-cards.py"), run_name="__main__")

path = ROOT / "tests/test_mouse_first_apps.py"
text = path.read_text(encoding="utf-8")
old = '''for control in ("RED CORE", "LOW CONTRAST RED", "VOLUME -10", "VOLUME +10", "MUTE / UNMUTE"):
    assert control in settings, f"settings: missing mouse control {control}"
assert "desktop_settings_mouse_navigation: PASS" in settings
assert "desktop_settings_keyboard_shortcuts_detached: PASS" in settings
'''
new = '''for control in ("LOW CONTRAST RED", "MUTE AUDIO", "VOLUME -10", "VOLUME +10", "RESET INTERFACE PROFILE"):
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
'''
if text.count(old) != 1:
    raise SystemExit("Settings legacy host-contract anchor drifted")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Settings toggle-card host contract migrated")
