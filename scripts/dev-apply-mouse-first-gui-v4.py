#!/usr/bin/env python3
"""Apply mouse-first GUI v3 and make the libui scroll test truly overflow."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
result = subprocess.run(
    [sys.executable, str(ROOT / "scripts/dev-apply-mouse-first-gui-v3.py")],
    cwd=ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit("mouse-first GUI v3 patch failed")

test = ROOT / "tests/test_libui_pointer.c"
text = test.read_text(encoding="utf-8")
old = (
    '        kui_flow_list_item(&root, 4U, "ITEM") != KU_STATUS_OK ||\n'
    '        kui_flow_button(&root, 5U, "MORE") != KU_STATUS_OK) return 1;\n'
)
new = (
    '        kui_flow_list_item(&root, 4U, "ITEM") != KU_STATUS_OK ||\n'
    '        kui_flow_button(&root, 5U, "MORE") != KU_STATUS_OK ||\n'
    '        kui_flow_button(&root, 6U, "OVERFLOW") != KU_STATUS_OK) return 1;\n'
)
if text.count(old) != 1:
    raise SystemExit(f"libui pointer test: expected one scene overflow anchor, found {text.count(old)}")
test.write_text(text.replace(old, new, 1), encoding="utf-8")
print("mouse-first GUI v4 patch applied")
