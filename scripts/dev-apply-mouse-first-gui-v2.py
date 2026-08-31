#!/usr/bin/env python3
"""Apply mouse-first GUI patch with freestanding libui host qualification."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
result = subprocess.run(
    [sys.executable, str(ROOT / "scripts/dev-apply-mouse-first-gui.py")],
    cwd=ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit("base mouse-first GUI patch failed")

runner = ROOT / "scripts/run-host-tests.sh"
text = runner.read_text(encoding="utf-8")
old = "  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \\\n  -Isdk/include \\\n  tests/test_libui_pointer.c sdk/src/libui.c \\\n"
new = "  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -ffreestanding \\\n  -Isdk/include \\\n  tests/test_libui_pointer.c sdk/src/libui.c \\\n"
if text.count(old) != 1:
    raise SystemExit(
        f"run-host-tests.sh: expected one libui host compile anchor, found {text.count(old)}")
runner.write_text(text.replace(old, new, 1), encoding="utf-8")
print("mouse-first GUI v2 patch applied")
