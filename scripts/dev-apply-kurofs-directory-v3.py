#!/usr/bin/env python3
"""Apply revision-aware KuroFS directory records with canonical NUL parsing."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
result = subprocess.run(
    [sys.executable, str(ROOT / "scripts/dev-apply-kurofs-directory-v2.py")],
    cwd=ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit("revision-aware directory patch failed")

cpp = ROOT / "kernel/fs/kurofs.cpp"
text = cpp.read_text(encoding="utf-8")
bad_nul = r"'\\0'"
good_nul = r"'\0'"
bad_count = text.count(bad_nul)
good_count = text.count(good_nul)
if bad_count == 4 and good_count == 0:
    text = text.replace(bad_nul, good_nul)
elif bad_count == 0 and good_count == 4:
    pass
else:
    raise SystemExit(
        f"kurofs.cpp: unexpected NUL literal shape: bad={bad_count} good={good_count}")
cpp.write_text(text, encoding="utf-8")

test = ROOT / "tests/test_kurofs_directory.cpp"
test_text = test.read_text(encoding="utf-8")
old = '''    if (!ok(directory_lookup(&fs, &root, "entry4", &found) == Status::Ok &&\n            found.inode_id == children[4], "lookup child")) return 1;\n'''
new = '''    if (!ok(directory_lookup(&fs, &root, "entry4", &found) == Status::Ok &&\n            found.inode_id == children[4], "lookup child")) return 1;\n\n    // Directory identity is bound to stable inode generation, not mutable\n    // metadata revision. Updating the child must not invalidate its name.\n    Inode changed_child{};\n    if (!ok(read_inode(&fs, children[4], &changed_child) == Status::Ok,\n            "read child before metadata update")) return 1;\n    const uint32_t child_generation = changed_child.generation;\n    const uint32_t child_revision = changed_child.revision;\n    uint64_t child_extent = 0U;\n    if (!ok(allocate_blocks(&fs, 1U, &child_extent) == Status::Ok,\n            "allocate child extent")) return 1;\n    changed_child.extent_start = child_extent;\n    changed_child.extent_blocks = 1U;\n    changed_child.size = 0U;\n    if (!ok(update_inode(&fs, &changed_child) == Status::Ok &&\n            changed_child.generation == child_generation &&\n            changed_child.revision == child_revision + 1U,\n            "child revision update preserves incarnation")) return 1;\n    DirectoryEntry after_child_update{};\n    if (!ok(directory_lookup(&fs, &root, "entry4", &after_child_update) == Status::Ok &&\n            after_child_update.inode_id == children[4] &&\n            after_child_update.inode_generation == child_generation,\n            "child metadata update preserves directory identity")) return 1;\n'''
if test_text.count(old) != 1:
    raise SystemExit(f"directory test: expected one child lookup anchor, found {test_text.count(old)}")
test.write_text(test_text.replace(old, new, 1), encoding="utf-8")
