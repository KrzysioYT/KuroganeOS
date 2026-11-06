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
    "--require-marker '[TEST] desktop_terminal_ring3: PASS'",
    "--require-marker-count '[INPUT] flux_app_spawn: PASS' 2",
    "--require-marker-count '[INPUT] flux_app_surface_ready: PASS' 2",
    "--click-after-marker '[TEST] flux_files_usb_wheel_input: PASS' 367 726",
    "--require-marker '[TEST] flux_files_usb_wheel_input: PASS'",
]
for text in required:
    assert text in workflow, f"Flux release gate missing: {text}"

# The production dispatch regression covers both supported GOP modes and
# the Dock recentering after Files adds a task. Static checks alone previously
# approved a click that missed Terminal at runtime.
assert 'tests/test_flux_dock_smoke.cpp' in workflow
assert '450 726' in workflow, "QMP normalized Files click missing"
assert '367 726' in workflow, "QMP normalized Terminal click missing"
assert '573 754' not in workflow and '525 754' not in workflow, (
    "Raw GOP pixel clicks reintroduced despite QMP tablet normalization"
)
qmp_script = (root / "scripts/smoke-uefi-iso-qemu.sh").read_text()
assert "target_x * 32767 // 1023" in qmp_script
assert "target_y * 32767 // 767" in qmp_script
qemu_command = workflow.split("bash scripts/smoke-uefi-iso-qemu.sh", 1)[1]
qemu_command = qemu_command.split("\n          ! grep -Eq", 1)[0]
assert not any(line.lstrip().startswith("#") for line in qemu_command.splitlines()), (
    "Comment inside a backslash-continued QEMU command truncates its options"
)
assert 'constexpr int32_t DOCK_PIN_SIZE = 42' in wm
assert 'constexpr int32_t DOCK_HOME_WIDTH = 78' in wm
assert 'constexpr int32_t DOCK_HEIGHT = 58' in wm
assert 'constexpr int32_t DOCK_BOTTOM = 12' in wm
assert 'constexpr int32_t DOCK_PADDING = 12' in wm
assert 'constexpr int32_t DOCK_PIN_GAP = 8' in wm
assert "KU_FLUX_TITLE_FILES, KU_FLUX_EXEC_FILES, 'f'" in wm
assert 'find_dock_window(app_id)' in wm
assert '"[INPUT] flux_dock_shortcut_received: PASS"' in launcher
assert '"[INPUT] flux_app_spawn: PASS"' in launcher
assert '"[INPUT] flux_app_surface_ready: PASS"' in launcher
assert '"[SESSION] flux_home_ui_present: PASS"' in launcher
assert '"[SESSION] flux_home_first_frame: PASS"' in login
print("Flux 8 application-launch smoke contract: PASS")
