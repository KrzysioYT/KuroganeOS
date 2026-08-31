#!/usr/bin/env python3
"""Apply the KuroFS directory patch and bind parent snapshots to metadata revision."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
result = subprocess.run(
    [sys.executable, str(ROOT / "scripts/dev-apply-kurofs-directory.py")],
    cwd=ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit("base directory patch failed")

cpp = ROOT / "kernel/fs/kurofs.cpp"
text = cpp.read_text(encoding="utf-8")
old = (
    "        left.extent_blocks == right.extent_blocks && left.link_count == right.link_count &&\n"
    "        left.generation == right.generation;\n"
)
new = (
    "        left.extent_blocks == right.extent_blocks && left.link_count == right.link_count &&\n"
    "        left.generation == right.generation && left.revision == right.revision;\n"
)
if text.count(old) != 1:
    raise SystemExit(f"kurofs.cpp: expected one directory snapshot guard, found {text.count(old)}")
cpp.write_text(text.replace(old, new, 1), encoding="utf-8")
