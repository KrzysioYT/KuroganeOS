#!/usr/bin/env python3
"""Run the KuroFS data patch and repair its intentionally strict host-test anchor."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = "scripts/run-host-tests.sh: expected one guarded match, found 2"

result = subprocess.run(
    [sys.executable, str(ROOT / "scripts/dev-apply-kurofs-data.py")],
    cwd=ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if result.returncode == 0:
    raise SystemExit("base patch unexpectedly succeeded; retire the v2 wrapper")
combined = result.stdout + result.stderr
if EXPECTED not in combined:
    sys.stderr.write(combined)
    raise SystemExit("base patch failed for an unexpected reason")

required = [
    ROOT / "kernel/fs/kurofs.hpp",
    ROOT / "kernel/fs/kurofs.cpp",
    ROOT / "tests/test_kurofs_data.cpp",
]
if not all(path.exists() for path in required):
    raise SystemExit("base patch did not produce the expected production/test files")
if "Status update_inode(FileSystem* filesystem, Inode* inode);" not in required[0].read_text(encoding="utf-8"):
    raise SystemExit("KuroFS data API was not applied before the expected anchor failure")

runner = ROOT / "scripts/run-host-tests.sh"
text = runner.read_text(encoding="utf-8")
anchor = '"$OUT_DIR/test_kurofs_allocator"\n\n"$HOST_CXX" \\\n'
if text.count(anchor) != 1:
    raise SystemExit(f"run-host-tests: expected one precise allocator execution anchor, found {text.count(anchor)}")
insert = (
    '"$OUT_DIR/test_kurofs_allocator"\n\n'
    '# Exercise durable KuroFS extent contents and generation-safe inode publication.\n'
    '"$HOST_CXX" \\\n'
    '  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \\\n'
    '  tests/test_kurofs_data.cpp \\\n'
    '  kernel/fs/kurofs.cpp \\\n'
    '  -o "$OUT_DIR/test_kurofs_data"\n\n'
    '"$OUT_DIR/test_kurofs_data"\n\n'
    '"$HOST_CXX" \\\n'
)
runner.write_text(text.replace(anchor, insert, 1), encoding="utf-8")
