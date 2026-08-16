#!/usr/bin/env python3
"""Convert the flat standalone Kurogane UEFI loader to a PE32+ EFI app.

This is the portable counterpart of scripts/elf-to-efi.ps1.  It intentionally
uses the same two-section layout so Windows and macOS builds produce equivalent
BOOTX64.EFI structure.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def align(value: int, alignment: int) -> int:
    if alignment <= 0 or alignment & (alignment - 1):
        raise ValueError("alignment must be a non-zero power of two")
    return (value + alignment - 1) & ~(alignment - 1)


def put16(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buf, offset, value)


def put32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buf, offset, value)


def put64(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buf, offset, value)


def put_name(buf: bytearray, offset: int, name: str) -> None:
    encoded = name.encode("ascii")
    if len(encoded) > 8:
        raise ValueError(f"PE section name is too long: {name}")
    buf[offset : offset + len(encoded)] = encoded


def convert(source: Path, output: Path) -> None:
    image = source.read_bytes()
    if not image:
        raise ValueError(f"flat loader image is empty: {source}")

    file_alignment = 0x200
    section_alignment = 0x1000
    headers_size = 0x200
    image_rva = 0x1000
    image_raw_offset = headers_size
    image_raw_size = align(len(image), file_alignment)
    reloc_rva = align(image_rva + len(image), section_alignment)
    reloc_raw_offset = image_raw_offset + image_raw_size
    reloc_data_size = 12
    reloc_raw_size = file_alignment
    size_of_image = align(reloc_rva + reloc_data_size, section_alignment)
    file_size = reloc_raw_offset + reloc_raw_size

    if max(len(image), image_raw_size, reloc_rva, reloc_raw_offset,
           size_of_image, file_size) > 0xFFFFFFFF:
        raise ValueError("loader is too large for the PE32+ layout")

    pe = bytearray(file_size)
    pe[0:2] = b"MZ"
    put32(pe, 0x3C, 0x80)

    pe_offset = 0x80
    pe[pe_offset : pe_offset + 4] = b"PE\0\0"
    coff = pe_offset + 4
    put16(pe, coff, 0x8664)
    put16(pe, coff + 2, 2)
    put32(pe, coff + 4, 0)
    put32(pe, coff + 8, 0)
    put32(pe, coff + 12, 0)
    put16(pe, coff + 16, 240)
    put16(pe, coff + 18, 0x0022)

    optional = coff + 20
    put16(pe, optional, 0x020B)
    pe[optional + 2] = 1
    pe[optional + 3] = 0
    put32(pe, optional + 4, image_raw_size)
    put32(pe, optional + 8, reloc_raw_size)
    put32(pe, optional + 12, 0)
    put32(pe, optional + 16, image_rva)
    put32(pe, optional + 20, image_rva)
    put64(pe, optional + 24, 0)
    put32(pe, optional + 32, section_alignment)
    put32(pe, optional + 36, file_alignment)
    put16(pe, optional + 40, 0)
    put16(pe, optional + 42, 0)
    put16(pe, optional + 44, 1)
    put16(pe, optional + 46, 0)
    put16(pe, optional + 48, 2)
    put16(pe, optional + 50, 0)
    put32(pe, optional + 52, 0)
    put32(pe, optional + 56, size_of_image)
    put32(pe, optional + 60, headers_size)
    put32(pe, optional + 64, 0)
    put16(pe, optional + 68, 10)  # IMAGE_SUBSYSTEM_EFI_APPLICATION
    put16(pe, optional + 70, 0x0140)
    put64(pe, optional + 72, 0x100000)
    put64(pe, optional + 80, 0x1000)
    put64(pe, optional + 88, 0x100000)
    put64(pe, optional + 96, 0x1000)
    put32(pe, optional + 104, 0)
    put32(pe, optional + 108, 16)

    reloc_directory = optional + 112 + (5 * 8)
    put32(pe, reloc_directory, reloc_rva)
    put32(pe, reloc_directory + 4, reloc_data_size)

    section_table = optional + 240
    put_name(pe, section_table, ".image")
    put32(pe, section_table + 8, len(image))
    put32(pe, section_table + 12, image_rva)
    put32(pe, section_table + 16, image_raw_size)
    put32(pe, section_table + 20, image_raw_offset)
    put32(pe, section_table + 36, 0x60000060)

    reloc_section = section_table + 40
    put_name(pe, reloc_section, ".reloc")
    put32(pe, reloc_section + 8, reloc_data_size)
    put32(pe, reloc_section + 12, reloc_rva)
    put32(pe, reloc_section + 16, reloc_raw_size)
    put32(pe, reloc_section + 20, reloc_raw_offset)
    put32(pe, reloc_section + 36, 0x42000040)

    pe[image_raw_offset : image_raw_offset + len(image)] = image
    put32(pe, reloc_raw_offset, 0)
    put32(pe, reloc_raw_offset + 4, 12)
    put16(pe, reloc_raw_offset + 8, 0)
    put16(pe, reloc_raw_offset + 10, 0)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(pe)
    print(
        f"[efi] {output} ({len(pe)} bytes, entry RVA 0x{image_rva:X}, "
        f"image size 0x{size_of_image:X})"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    convert(args.input.resolve(), args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
