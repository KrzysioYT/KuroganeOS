#!/usr/bin/env python3
"""Normalize FAT32 metadata for KuroganeOS' strict mount validation.

The kernel intentionally validates enabled FAT mirrors byte-for-byte and
requires backup boot geometry to match the primary BPB. Some host-side FAT
utilities may leave mirrored metadata divergent after populating an image.
This helper normalizes those metadata copies without touching file data.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys


def u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def normalize(path: str) -> None:
    with open(path, "r+b") as image:
        boot = bytearray(image.read(512))
        if len(boot) != 512 or boot[510:512] != b"\x55\xaa":
            raise RuntimeError("invalid FAT32 boot sector signature")

        bytes_per_sector = u16(boot, 11)
        sectors_per_cluster = boot[13]
        reserved_sectors = u16(boot, 14)
        fat_count = boot[16]
        sectors_per_fat_16 = u16(boot, 22)
        sectors_per_fat_32 = u32(boot, 36)
        fs_version = u16(boot, 42)
        backup_boot_sector = u16(boot, 50)

        if bytes_per_sector != 512:
            raise RuntimeError(f"unsupported sector size: {bytes_per_sector}")
        if sectors_per_cluster == 0 or sectors_per_cluster & (sectors_per_cluster - 1):
            raise RuntimeError("invalid sectors-per-cluster")
        if reserved_sectors == 0 or fat_count == 0:
            raise RuntimeError("invalid FAT32 reserved/FAT count")
        if sectors_per_fat_16 != 0 or sectors_per_fat_32 == 0 or fs_version != 0:
            raise RuntimeError("image is not supported FAT32 geometry")

        file_size = os.fstat(image.fileno()).st_size
        fat_bytes = sectors_per_fat_32 * bytes_per_sector
        first_fat_offset = reserved_sectors * bytes_per_sector
        if first_fat_offset + fat_bytes > file_size:
            raise RuntimeError("primary FAT is outside the image")

        image.seek(first_fat_offset)
        primary_fat = image.read(fat_bytes)
        if len(primary_fat) != fat_bytes:
            raise RuntimeError("cannot read primary FAT")

        # KuroganeOS currently requires every enabled FAT mirror to be equal.
        for copy in range(1, fat_count):
            offset = (reserved_sectors + copy * sectors_per_fat_32) * bytes_per_sector
            if offset + fat_bytes > file_size:
                raise RuntimeError("FAT mirror is outside the image")
            image.seek(offset)
            image.write(primary_fat)

        # The kernel compares BPB/extended-BPB geometry bytes 11..51 against
        # the backup boot sector. Keep boot code/OEM text independent, but make
        # the filesystem geometry deterministic and equivalent.
        if backup_boot_sector not in (0, 0xFFFF):
            backup_offset = backup_boot_sector * bytes_per_sector
            if backup_offset + bytes_per_sector > file_size:
                raise RuntimeError("backup boot sector is outside the image")
            image.seek(backup_offset)
            backup = bytearray(image.read(bytes_per_sector))
            if len(backup) != bytes_per_sector:
                raise RuntimeError("cannot read backup boot sector")
            backup[11:52] = boot[11:52]
            backup[510:512] = boot[510:512]
            image.seek(backup_offset)
            image.write(backup)

        image.flush()
        os.fsync(image.fileno())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", help="raw FAT32 volume image")
    args = parser.parse_args()
    try:
        normalize(args.image)
    except (OSError, RuntimeError, struct.error) as exc:
        print(f"normalize-fat32: {exc}", file=sys.stderr)
        return 1
    print(f"Normalized FAT32 metadata: {args.image}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
