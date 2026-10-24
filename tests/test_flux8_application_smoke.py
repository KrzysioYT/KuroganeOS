#!/usr/bin/env python3
"""Keep Flux 8 release smoke exercising a real *post-login app launch*.

A startup PASS marker says nothing about a working desktop. Regression
2026-10-24: Home was absent but background and Dock still rendered.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/qualify-8.0-flux-desktop-foundation.yml").read_text()
wm = (root / "kernel/ui/window_manager.cpp").read_text()
launcher = (root / "userspace/gui/launcher/main.c").read_text()
login = (root / "userspace/gui/login/main.c").read_text()

required = [
    "--click-after-marker '[TEST] red_flux_login_to_desktop: PASS'",
    "--require-marker '[SESSION] flux_home_ui_create: PASS'",
    "--require-marker '[SESSION] flux_home_ui_present: PASS'",
    "--require-marker '[SESSION] flux_home_first_frame: PASS'",
    "--require-marker '[INPUT] flux_dock_shortcut_received: PASS'",
    "--require-marker '[INPUT] flux_launcher_action_received: PASS'",
    "--require-marker '[INPUT] flux_app_spawn: PASS'",
    "--require-marker '[TEST] flux_files_directory_grid: PASS'",
]
for text in required:
    assert text in workflow, f"Flux release gate missing: {text}"

# Fixed-size 1280x800 framebuffer in this scoped QEMU smoke:
# dock width = 12*2 + (78+7*42+7*8) + 16 = 468, x = (1280-468)/2.
# button index 2 -> 406+12+78+8+42+8 = 554, center=575.
# dock top=800-58-12=730, center=759.
assert '573 754' in workflow, "QEMU smoke not pointing inside Files Dock button"
assert 'constexpr int32_t DOCK_PIN_SIZE = 42' in wm
assert 'constexpr int32_t DOCK_HOME_WIDTH = 78' in wm
assert 'constexpr int32_t DOCK_HEIGHT = 58' in wm
assert 'constexpr int32_t DOCK_BOTTOM = 12' in wm
assert 'constexpr int32_t DOCK_PADDING = 12' in wm
assert 'constexpr int32_t DOCK_PIN_GAP = 8' in wm
assert '"FILES", \'f\'' in wm
assert '"[INPUT] flux_dock_shortcut_received: PASS"' in launcher
assert '"[INPUT] flux_app_spawn: PASS"' in launcher
assert '"[SESSION] flux_home_ui_present: PASS"' in launcher
assert '"[SESSION] flux_home_first_frame: PASS"' in login
print("Flux 8 application-launch smoke contract: PASS")
