#!/usr/bin/env python3
"""Regression checks for release artifact version parsing."""

from __future__ import annotations

import pathlib
import re
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
READER = ROOT / "scripts" / "read-version.sh"


def read_version(header: pathlib.Path) -> bytes:
    return subprocess.check_output(["bash", str(READER), str(header)])


def main() -> None:
    header = ROOT / "common" / "version.h"
    match = re.search(
        rb'^#define\s+KUROGANE_VERSION_STRING\s+"([^"]+)"',
        header.read_bytes(),
        re.MULTILINE,
    )
    assert match is not None
    expected = match.group(1) + b"\n"
    actual = read_version(header)
    assert actual == expected, (actual, expected)
    assert b"\r" not in actual

    with tempfile.TemporaryDirectory() as temporary:
        fixture = pathlib.Path(temporary) / "version.h"
        fixture.write_bytes(
            b'#pragma once\r\n'
            b'#define KUROGANE_VERSION_STRING "9.8.7-rc1"\r\n'
        )
        crlf_actual = read_version(fixture)
        assert crlf_actual == b"9.8.7-rc1\n", crlf_actual
        assert b"\r" not in crlf_actual

    print("release version parsing tests passed")


if __name__ == "__main__":
    main()
