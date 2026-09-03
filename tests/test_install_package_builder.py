#!/usr/bin/env python3
"""Boundary and overlay regressions for the installer package builder."""

from __future__ import annotations

import importlib.util
import pathlib
import re
import shutil
import struct
import tempfile


def load_builder(root: pathlib.Path):
    module_path = root / "scripts" / "build-install-package.py"
    spec = importlib.util.spec_from_file_location("kurogane_package_builder", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load installer builder: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_value_error(operation, message: str) -> None:
    try:
        operation()
    except ValueError:
        return
    raise AssertionError(message)


def numbered_records(builder, count: int) -> list[tuple[int, str, bytes]]:
    return [
        (builder.DEST_ROOT, f"/FILES/F{index:07d}.BIN", b"")
        for index in range(count)
    ]


def production_outputs(root: pathlib.Path) -> list[str]:
    build = (root / "scripts" / "build-linux.sh").read_text(encoding="utf-8")
    start = build.find("applications=(")
    end = build.find("\n)\n", start)
    if start < 0 or end < 0:
        raise AssertionError("cannot locate production applications table")
    pattern = re.compile(
        r'^\s*"[^|]+\|[^|]+\|([^|]+)\|(?:asm|c)"\s*$', re.MULTILINE
    )
    outputs = [match.group(1) for match in pattern.finditer(build[start:end])]
    sdk = (root / "scripts" / "build-sdk.sh").read_text(encoding="utf-8")
    sdk_start = sdk.find("declare -a gui_specs=(")
    sdk_end = sdk.find("\n)\n", sdk_start)
    if sdk_start < 0 or sdk_end < 0:
        raise AssertionError("cannot locate SDK GUI applications table")
    gui_pattern = re.compile(
        r"^\s*[a-z0-9_-]+:([a-z0-9_-]+)\s*$", re.MULTILINE
    )
    outputs.append("apps/external")
    outputs.extend(
        "gui/" + match.group(1)
        for match in gui_pattern.finditer(sdk[sdk_start:sdk_end])
    )
    if not outputs:
        raise AssertionError("production applications table is empty")
    return outputs


def verify_serialized_layout(builder, package: bytes, expected_count: int) -> None:
    fields = struct.unpack_from("<8sIIQIIQQIIQ", package, 0)
    (
        magic,
        version,
        header_size,
        total_size,
        file_count,
        entry_size,
        entries_offset,
        data_offset,
        _entries_crc,
        reserved0,
        reserved1,
    ) = fields
    assert magic == builder.MAGIC
    assert version == 1 and header_size == builder.HEADER_SIZE
    assert total_size == len(package) <= builder.MAX_PACKAGE_SIZE
    assert file_count == expected_count
    assert entry_size == builder.ENTRY_SIZE
    assert entries_offset == builder.HEADER_SIZE
    assert data_offset >= entries_offset + file_count * entry_size
    assert data_offset % 16 == 0 and reserved0 == 0 and reserved1 == 0

    previous_end = data_offset
    for index in range(file_count):
        entry = entries_offset + index * entry_size
        file_offset, file_size = struct.unpack_from("<QQ", package, entry + 128)
        assert file_offset % 16 == 0
        assert file_offset >= previous_end
        assert file_size <= len(package) - file_offset
        previous_end = file_offset + file_size


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    builder = load_builder(root)

    header = (root / "kernel" / "install" / "package.hpp").read_text(
        encoding="utf-8"
    )
    match = re.search(r"MAXIMUM_FILES\s*=\s*(\d+)U", header)
    assert match is not None
    assert int(match.group(1)) == builder.MAX_FILES

    expect_value_error(
        lambda: builder.serialize_package([]), "zero-file package was accepted"
    )
    maximum = numbered_records(builder, builder.MAX_FILES)
    package, count = builder.serialize_package(maximum)
    assert count == builder.MAX_FILES
    verify_serialized_layout(builder, package, builder.MAX_FILES)
    expect_value_error(
        lambda: builder.serialize_package(
            numbered_records(builder, builder.MAX_FILES + 1)
        ),
        "MAX_FILES + 1 package was accepted",
    )
    expect_value_error(
        lambda: builder.serialize_package(
            [
                (builder.DEST_ROOT, "/DUP/FILE.BIN", b"first"),
                (builder.DEST_ROOT, "/DUP/FILE.BIN", b"second"),
            ]
        ),
        "duplicate package path was accepted",
    )
    expect_value_error(
        lambda: builder.serialize_package([(99, "/BAD/DEST.BIN", b"")]),
        "malformed destination was accepted",
    )
    expect_value_error(
        lambda: builder.checked_path("/" + "A" * 128),
        "oversized path was accepted",
    )
    expect_value_error(
        lambda: builder.checked_u64_end(builder.UINT64_MAX - 3, 4, "/OVER.BIN"),
        "overflowing file range was accepted",
    )
    expect_value_error(
        lambda: builder.serialize_package(
            [(builder.DEST_ROOT, "/BIG/FILE.BIN", b"X" * builder.MAX_PACKAGE_SIZE)]
        ),
        "oversized package was accepted",
    )

    with tempfile.TemporaryDirectory(prefix="kurogane-package-") as temporary:
        temporary_root = pathlib.Path(temporary)
        overlay = temporary_root / "production-overlay"
        for output in production_outputs(root):
            target = overlay / output
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"ELF")
        records, replacements = builder.collect_install_records(
            b"EFI", b"KERNEL", root / "rootfs", overlay
        )
        assert replacements == 0
        production, production_count = builder.serialize_package(records)
        assert 0 < production_count <= builder.MAX_FILES
        verify_serialized_layout(builder, production, production_count)

        qualification_root = temporary_root / "qualification-root"
        shutil.copytree(root / "rootfs", qualification_root)
        for name in (
            "probe.mnf",
            "dup1.mnf",
            "dup2.mnf",
            "bad.mnf",
            "miss.mnf",
            "long.mnf",
        ):
            (qualification_root / "apps" / "appman" / name).write_bytes(b"fixture")
        for name in ("audprb", "audxcli", "audownx", "apprgprb"):
            (overlay / "system" / name).write_bytes(b"ELF")
        records, replacements = builder.collect_install_records(
            b"EFI", b"KERNEL", qualification_root, overlay
        )
        assert replacements == 0
        qualification, qualification_count = builder.serialize_package(records)
        assert qualification_count == production_count + 10
        verify_serialized_layout(builder, qualification, qualification_count)

        base = temporary_root / "base"
        replacement = temporary_root / "replacement"
        (base / "etc").mkdir(parents=True)
        (replacement / "etc").mkdir(parents=True)
        (base / "etc" / "system.cfg").write_bytes(b"old")
        (replacement / "etc" / "system.cfg").write_bytes(b"new")
        records, replacements = builder.collect_install_records(
            b"EFI", b"KERNEL", base, replacement
        )
        assert replacements == 1
        selected = [
            data
            for destination, path, data in records
            if destination == builder.DEST_ROOT and path == "/etc/system.cfg"
        ]
        assert selected == [b"new"]

    print(
        "installer package builder tests passed "
        f"(production={production_count}, qualification={qualification_count}, "
        f"max={builder.MAX_FILES})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
