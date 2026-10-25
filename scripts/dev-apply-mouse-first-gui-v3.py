#!/usr/bin/env python3
"""Apply mouse-first GUI v2 and keep host diagnostics within Kurogane libc."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
result = subprocess.run(
    [sys.executable, str(ROOT / "scripts/dev-apply-mouse-first-gui-v2.py")],
    cwd=ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit("mouse-first GUI v2 patch failed")

test = ROOT / "tests/test_libui_pointer.c"
text = test.read_text(encoding="utf-8")
old = '    fprintf(stderr, "%s: expected %u got %u\\n", label, expected, actual);\n'
new = '    printf("%s: expected %u got %u\\n", label, expected, actual);\n'
if text.count(old) != 1:
    raise SystemExit(f"libui pointer test: expected one stderr diagnostic, found {text.count(old)}")
test.write_text(text.replace(old, new, 1), encoding="utf-8")
print("mouse-first GUI v3 patch applied")
