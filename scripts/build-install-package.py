#!/usr/bin/env python3
"""Build the bounded KuroganeOS installer payload."""

from __future__ import annotations

import argparse
import base64
import hashlib
import pathlib
import struct
import zlib

MAGIC = b"KURPKG1\0"
HEADER_SIZE = 64
ENTRY_SIZE = 160
MAX_FILES = 128
MAX_PACKAGE_SIZE = 16 * 1024 * 1024
UINT64_MAX = (1 << 64) - 1
DEST_ESP = 1
DEST_ROOT = 2
APP_MANIFEST_DIRECTORY = "apps/appman"
APP_MANIFEST_EXTENSION = "MNF"


def is_short_component(component: str) -> bool:
    if not component or component in {".", ".."}:
        return False
    pieces = component.split(".")
    if len(pieces) > 2 or not (1 <= len(pieces[0]) <= 8):
        return False
    if len(pieces) == 2 and not (1 <= len(pieces[1]) <= 3):
        return False
    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
    return all(character in allowed for piece in pieces for character in piece.upper())


def checked_path(path: str) -> str:
    if not path.startswith("/") or len(path.encode("ascii")) >= 128:
        raise ValueError(f"invalid package path: {path}")
    if not all(is_short_component(part) for part in path.split("/") if part):
        raise ValueError(f"path is outside the installer's FAT 8.3 contract: {path}")
    return path


def installer_relative_path(relative: str) -> str:
    """Map registry manifest source names onto transport-only FAT 8.3 names.

    Application identity remains the `id=` field inside each manifest. The
    registry enumerates the manifest directory and parses that logical ID, so
    installer media must not make the source filename part of the public app
    identity contract. A stable 40-bit digest keeps every physical manifest
    basename within eight FAT characters while leaving `.MNF` as a bounded
    transport extension.
    """
    path = pathlib.PurePosixPath(relative)
    if path.parent.as_posix().lower() != APP_MANIFEST_DIRECTORY:
        return relative
    digest = hashlib.sha256(relative.encode("utf-8")).digest()[:5]
    basename = base64.b32encode(digest).decode("ascii")
    return f"{APP_MANIFEST_DIRECTORY}/{basename}.{APP_MANIFEST_EXTENSION}"


def checked_u64_end(offset: int, size: int, path: str) -> int:
    if offset < 0 or size < 0 or offset > UINT64_MAX or size > UINT64_MAX - offset:
        raise ValueError(f"installer package file range overflows uint64: {path}")
    return offset + size


def collect_install_records(
    efi: bytes,
    kernel: bytes,
    rootfs: pathlib.Path,
    overlay: pathlib.Path,
) -> tuple[list[tuple[int, str, bytes]], int]:
    """Collect the deterministic production overlay before serialization."""
    records: list[tuple[int, str, bytes]] = [
        (DEST_ESP, "/EFI/BOOT/BOOTX64.EFI", efi),
        (DEST_ESP, "/kernel.elf", kernel),
        (DEST_ESP, "/EFI/BOOT/kernel.elf", kernel),
        (DEST_ROOT, "/boot/kernel.elf", kernel),
    ]
    root_records: dict[str, bytes] = {}
    root_origins: dict[str, str] = {}
    overlay_replacements = 0

    def collect_tree(tree: pathlib.Path, replace_existing: bool) -> None:
        nonlocal overlay_replacements
        for source in sorted(path for path in tree.rglob("*") if path.is_file()):
            relative = source.relative_to(tree).as_posix()
            # Ignore the pre-2.0 legacy LFN spelling. The native system.cfg
            # keeps installer writes within the FAT 8.3 contract.
            if relative.lower() == "etc/system.conf":
                continue
            installer_relative = installer_relative_path(relative)
            package_path = checked_path("/" + installer_relative)
            if package_path in root_records:
                existing_origin = root_origins[package_path]
                if not replace_existing or existing_origin != relative:
                    raise ValueError(
                        "installer path collision: "
                        f"{existing_origin} and {relative} -> {package_path}"
                    )
                overlay_replacements += 1
            root_records[package_path] = source.read_bytes()
            root_origins[package_path] = relative

    collect_tree(rootfs, False)
    collect_tree(overlay, True)
    records.extend(
        (DEST_ROOT, package_path, contents)
        for package_path, contents in sorted(root_records.items())
    )
    records.append(
        (DEST_ROOT, "/etc/boot.cfg", b"DEFAULT=console\nKERNEL=/kernel.elf\n")
    )
    return records, overlay_replacements


def serialize_package(
    record_items: list[tuple[int, str, bytes]],
) -> tuple[bytes, int]:
    """Validate and serialize one bounded installer package."""
    if not record_items or len(record_items) > MAX_FILES:
        raise ValueError("installer package file count exceeds its contract")

    records: dict[tuple[int, str], bytes] = {}
    for destination, path, contents in record_items:
        if destination not in {DEST_ESP, DEST_ROOT}:
            raise ValueError(f"invalid package destination: {destination}")
        key = (destination, checked_path(path))
        if key in records:
            raise ValueError(f"duplicate package path: {path}")
        if not isinstance(contents, bytes):
            raise TypeError(f"package contents must be bytes: {path}")
        records[key] = contents

    ordered = sorted(records.items(), key=lambda item: item[0])
    entries_offset = HEADER_SIZE
    data_offset = (HEADER_SIZE + len(ordered) * ENTRY_SIZE + 15) & ~15
    data = bytearray()
    entries = bytearray()
    for (destination, path), contents in ordered:
        while (data_offset + len(data)) & 15:
            data.append(0)
        file_offset = data_offset + len(data)
        file_end = checked_u64_end(file_offset, len(contents), path)
        if file_end > MAX_PACKAGE_SIZE:
            raise ValueError("installer package size exceeds its contract")
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
    if total_size > MAX_PACKAGE_SIZE:
        raise ValueError("installer package size exceeds its contract")
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
    return bytes(package), len(ordered)


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

    record_items, overlay_replacements = collect_install_records(
        efi.read_bytes(), kernel.read_bytes(), rootfs, overlay
    )
    if overlay_replacements:
        print(f"[installer] overlay replacements: {overlay_replacements}")
    package, file_count = serialize_package(record_items)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(package)
    temporary.replace(output)
    print(f"Built installer package: {output} ({len(package)} bytes, {file_count} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
