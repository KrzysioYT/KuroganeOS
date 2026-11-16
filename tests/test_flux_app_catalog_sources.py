#!/usr/bin/env python3
"""Guard against split-brain window titles across kernel and Ring-3."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
kernel = (root / "kernel/ui/window_manager.cpp").read_text()
launcher = (root / "userspace/gui/launcher/main.c").read_text()
assert "KU_FLUX_TITLE_TERMINAL" in kernel
for key in ("HOME", "TERMINAL", "FILES", "PERFORMANCE", "BROWSER",
            "MONITOR", "SETTINGS", "ABOUT"):
    assert f"KU_FLUX_TITLE_{key}, KU_FLUX_EXEC_{key}" in kernel, (
        "Dock title must be tied to its installed executable: " + key
    )
assert "owner.executable, pin.executable" in kernel
assert "KU_FLUX_TITLE_TERMINAL" in (root / "userspace/gui/terminal/main.c").read_text()
assert '"FLUX TERMINAL"' not in kernel, "stale WM terminal title reintroduced"
assert "KU_FLUX_TITLE_HOME" in launcher
for sub, key in [
    ("files", "FILES"),
    ("performance", "PERFORMANCE"),
    ("browser", "BROWSER"),
    ("sysmon", "MONITOR"),
    ("settings", "SETTINGS"),
    ("about", "ABOUT"),
    ("notifications", "NOTIFICATIONS"),
]:
    actual = (root / "userspace/gui" / sub / "main.c").read_text()
    legacy = "gui_open(KU_FLUX_TITLE_" + key in actual
    # Files uses the common native geometry-restoring opener. Keep verifying
    # its catalog title and window lifetime rather than a fixed spelling.
    profile = (
        sub == "files"
        and "gui_window_profile_open(" in actual
        and "KU_FLUX_TITLE_" + key in actual
        and '"window.files"' in actual
        and "gui_window_profile_tick(" in actual
        and "gui_window_profile_flush(" in actual
    )
    assert legacy or profile, sub
    assert "KU_FLUX_EXEC_" + key in launcher, "launcher image path: " + sub
for candidate in ["kernel/ui/window_manager.cpp", "userspace/gui/launcher/main.c"]:
    assert "flux_app_catalog.h" in (root / candidate).read_text()
print("Flux application registry kernel/Ring-3 cross-check: PASS")
