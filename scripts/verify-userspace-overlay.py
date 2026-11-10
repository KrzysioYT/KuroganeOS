#!/usr/bin/env python3
"""Fail closed if an IMG/ISO overlay lacks installed Ring-3 runtime programs.

The Linux build inventory is the canonical checked-in program list until a
shared manifest replaces the platform-specific build frontends.
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GUI_NAMES = (
    "login", "launcher", "terminal", "files", "sysmon",
    "perf", "browser", "about", "settings", "notify",
)
EXTERNAL = "apps/external"


def inventory_paths(source=None):
    if source is None:
        source = (ROOT / "scripts/build-linux.sh").read_text(encoding="utf-8")
    match = re.search(r"^applications=\(\n(.*?)^\)\s*$", source, re.M | re.S)
    if not match:
        raise ValueError("missing canonical Linux userspace inventory")
    paths = []
    for value in re.findall(r'"([^"\n]+)"', match.group(1)):
        cells = value.split("|")
        if len(cells) != 4:
            raise ValueError("bad userspace inventory row")
        paths.append(cells[2])
    if len(paths) != len(set(paths)) or len(paths) != 33:
        raise ValueError("unexpected canonical inventory length or duplicates")
    return paths


def check_elf(path):
    raw = path.read_bytes()
    if len(raw) < 64 or raw[:4] != b"\x7fELF":
        return "not an ELF file"
    if raw[4] != 2 or raw[5] != 1:
        return "not a little-endian ELF64"
    if struct.unpack_from("<H", raw, 16)[0] != 2:
        return "not ET_EXEC"
    if struct.unpack_from("<H", raw, 18)[0] != 62:
        return "not x86-64"
    return None


def validate(root, expected=None):
    if expected is None:
        expected = inventory_paths() + ["gui/" + name for name in GUI_NAMES] + [EXTERNAL]
    failures = []
    for relative in expected:
        path = root / relative
        if not path.is_file():
            failures.append(relative + ": MISSING")
            continue
        fault = check_elf(path)
        if fault is not None:
            failures.append(relative + ": " + fault)
    return failures, len(expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overlay", type=pathlib.Path, required=True)
    args = parser.parse_args()
    problems, count = validate(args.overlay)
    if problems:
        print(f"[userspace-parity] FAIL ({len(problems)}/{count})", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1
    print(f"[userspace-parity] PASS: {count} required ELF64 programs present in {args.overlay}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
