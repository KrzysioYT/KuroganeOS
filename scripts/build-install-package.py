#!/usr/bin/env python3
"""Build the bounded KuroganeOS installer payload."""

from __future__ import annotations

import argparse
import pathlib
import struct
import zlib

MAGIC = b"KURPKG1\0"
HEADER_SIZE = 64
ENTRY_SIZE = 160
MAX_FILES = 64
DEST_ESP = 1
DEST_ROOT = 2


def is_short_component(component: str) -> bool:
    if not component or component in {".", ".."}:
        return False
    pieces = component.split(".")
    if len(pieces) > 2 or not (1 <= len(pieces[0]) <= 8):
        return False
    if len(pieces) == 2 and len(pieces[1]) > 3:
        return False
    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
    return all(character in allowed for piece in pieces for character in piece.upper())


def checked_path(path: str) -> str:
    if not path.startswith("/") or len(path.encode("ascii")) >= 128:
        raise ValueError(f"invalid package path: {path}")
    if not all(is_short_component(part) for part in path.split("/") if part):
        raise ValueError(f"path is outside the installer's FAT 8.3 contract: {path}")
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--efi", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--rootfs", required=True)
    parser.add_argument("--overlay", required=True)
    args = parser.parse_args()

    output = pathlib.Path(args.output)
    efi = pathlib.Path(args.efi)
    kernel = pathlib.Path(args.kernel)
    rootfs = pathlib.Path(args.rootfs)
    overlay = pathlib.Path(args.overlay)
    for required in (efi, kernel):
        if not required.is_file():
            raise FileNotFoundError(required)
    for required in (rootfs, overlay):
        if not required.is_dir():
            raise NotADirectoryError(required)

    records: dict[tuple[int, str], bytes] = {}

    def add(destination: int, path: str, data: bytes) -> None:
        key = (destination, checked_path(path))
        if key in records:
            raise ValueError(f"duplicate package path: {path}")
        records[key] = data

    add(DEST_ESP, "/EFI/BOOT/BOOTX64.EFI", efi.read_bytes())
    add(DEST_ESP, "/kernel.elf", kernel.read_bytes())
    add(DEST_ESP, "/EFI/BOOT/kernel.elf", kernel.read_bytes())
    add(DEST_ROOT, "/boot/kernel.elf", kernel.read_bytes())

    for tree in (rootfs, overlay):
        for source in sorted(path for path in tree.rglob("*") if path.is_file()):
            relative = source.relative_to(tree).as_posix()
            # Ignore the pre-2.0 legacy LFN spelling. The native system.cfg
            # keeps installer writes within the FAT 8.3 contract.
            if relative.lower() == "etc/system.conf":
                continue
            add(DEST_ROOT, "/" + relative, source.read_bytes())

    add(DEST_ROOT, "/etc/boot.cfg", b"DEFAULT=console\nKERNEL=/kernel.elf\n")
    ordered = sorted(records.items(), key=lambda item: item[0])
    if not ordered or len(ordered) > MAX_FILES:
        raise ValueError("installer package file count exceeds its contract")

    entries_offset = HEADER_SIZE
    data_offset = (HEADER_SIZE + len(ordered) * ENTRY_SIZE + 15) & ~15
    data = bytearray()
    entries = bytearray()
    for (destination, path), contents in ordered:
        while (data_offset + len(data)) & 15:
            data.append(0)
        file_offset = data_offset + len(data)
        encoded_path = path.encode("ascii")
        entry = bytearray(ENTRY_SIZE)
        entry[: len(encoded_path)] = encoded_path
        struct.pack_into(
            "<QQIIQ",
            entry,
            128,
            file_offset,
            len(contents),
            zlib.crc32(contents) & 0xFFFFFFFF,
            destination,
            0,
        )
        entries.extend(entry)
        data.extend(contents)

    total_size = data_offset + len(data)
    header = bytearray(HEADER_SIZE)
    header[:8] = MAGIC
    struct.pack_into(
        "<IIQIIQQIIQ",
        header,
        8,
        1,
        HEADER_SIZE,
        total_size,
        len(ordered),
        ENTRY_SIZE,
        entries_offset,
        data_offset,
        zlib.crc32(entries) & 0xFFFFFFFF,
        0,
        0,
    )
    package = header + entries
    package.extend(b"\0" * (data_offset - len(package)))
    package.extend(data)
    if len(package) != total_size:
        raise AssertionError("internal package size mismatch")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(package)
    temporary.replace(output)
    print(f"Built installer package: {output} ({len(package)} bytes, {len(ordered)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
