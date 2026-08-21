#!/usr/bin/env python3
"""Generate the KuroganeOS GUI icon registry from the checked-in v2 pack.

The runtime is freestanding and intentionally has no PNG/zlib dependency.
This tool performs PNG decoding at build/review time and emits a deterministic
24 px ARGB registry consumed directly by the software compositor.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import sys
import zipfile
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_PACK = ROOT / "KuroganeOS_5.0_Icon_Pack_v2.zip"
DEFAULT_HEADER = ROOT / "sdk/include/kurogane/icons.generated.h"
DEFAULT_DATA = ROOT / "kernel/ui/generated/icon_registry_data.inc"
RUNTIME_SIZE = 24

CATEGORY_NAMES = {
    "01_apps": "APPLICATION",
    "02_folders": "FOLDER",
    "03_filetypes": "FILE_TYPE",
    "04_devices": "DEVICE",
    "05_status": "STATUS",
    "06_actions": "ACTION",
    "07_navigation": "NAVIGATION",
    "08_ui": "WIDGET",
    "09_cursors": "CURSOR",
    "10_system": "SPECIAL",
    "11_branding": "BRANDING",
    "12_micro": "MICRO",
    "13_kurogane_apps": "KUROGANE_APP",
}


def macro_name(value: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "_", value.upper()).strip("_")


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    diagonal_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= diagonal_distance:
        return left
    if above_distance <= diagonal_distance:
        return above
    return upper_left


def decode_png(data: bytes, source: str) -> tuple[int, int, list[int]]:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"{source}: invalid PNG signature")

    position = 8
    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        chunk_type = data[position + 4 : position + 8]
        payload_start = position + 8
        payload_end = payload_start + length
        if payload_end + 4 > len(data):
            raise ValueError(f"{source}: truncated PNG chunk")
        payload = data[payload_start:payload_end]
        expected_crc = struct.unpack_from(">I", data, payload_end)[0]
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(payload, actual_crc) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            raise ValueError(f"{source}: PNG CRC mismatch")
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", payload)
            )
            if compression != 0 or filtering != 0:
                raise ValueError(f"{source}: unsupported PNG compression/filter method")
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            break
        position = payload_end + 4

    if width is None or height is None or bit_depth != 8 or interlace != 0:
        raise ValueError(f"{source}: expected non-interlaced 8-bit PNG")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise ValueError(f"{source}: unsupported PNG color type {color_type}")

    scanlines = zlib.decompress(bytes(compressed))
    stride = width * channels
    expected_size = height * (stride + 1)
    if len(scanlines) != expected_size:
        raise ValueError(
            f"{source}: decoded size {len(scanlines)} != expected {expected_size}"
        )

    rows: list[bytearray] = []
    offset = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = scanlines[offset]
        offset += 1
        encoded = scanlines[offset : offset + stride]
        offset += stride
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = value + left
            elif filter_type == 2:
                decoded = value + above
            elif filter_type == 3:
                decoded = value + ((left + above) // 2)
            elif filter_type == 4:
                decoded = value + paeth(left, above, upper_left)
            else:
                raise ValueError(f"{source}: unsupported PNG filter {filter_type}")
            row[index] = decoded & 0xFF
        rows.append(row)
        previous = row

    pixels: list[int] = []
    for row in rows:
        for x in range(width):
            base = x * channels
            if color_type == 6:
                red, green, blue, alpha = row[base : base + 4]
            elif color_type == 2:
                red, green, blue = row[base : base + 3]
                alpha = 255
            elif color_type == 4:
                red = green = blue = row[base]
                alpha = row[base + 1]
            else:
                red = green = blue = row[base]
                alpha = 255
            pixels.append((alpha << 24) | (red << 16) | (green << 8) | blue)
    return width, height, pixels


def load_assets(pack_path: pathlib.Path) -> tuple[dict, list[dict]]:
    with zipfile.ZipFile(pack_path) as archive:
        manifest_bytes = archive.read("manifest.json")
        manifest = json.loads(manifest_bytes.decode("utf-8"))
        names = set(archive.namelist())
        assets: list[dict] = []
        category_ordinals: dict[str, int] = {}
        seen_ids: set[int] = set()
        seen_names: set[tuple[str, str]] = set()
        for icon in manifest.get("icons", []):
            category = icon["category"]
            name = icon["name"]
            if category not in CATEGORY_NAMES:
                raise ValueError(f"unknown icon category: {category}")
            key = (category, name)
            if key in seen_names:
                raise ValueError(f"duplicate icon: {category}/{name}")
            seen_names.add(key)
            category_number = int(category[:2])
            ordinal = category_ordinals.get(category, 0) + 1
            category_ordinals[category] = ordinal
            if ordinal > 0xFF:
                raise ValueError(f"too many icons in {category}")
            icon_id = (category_number << 8) | ordinal
            if icon_id in seen_ids:
                raise ValueError(f"duplicate icon id: 0x{icon_id:04x}")
            seen_ids.add(icon_id)
            png_path = f"icons/{category}/{RUNTIME_SIZE}x{RUNTIME_SIZE}/{name}.png"
            if png_path not in names:
                raise ValueError(f"missing runtime icon: {png_path}")
            width, height, pixels = decode_png(archive.read(png_path), png_path)
            if width != RUNTIME_SIZE or height != RUNTIME_SIZE:
                raise ValueError(
                    f"{png_path}: expected {RUNTIME_SIZE}x{RUNTIME_SIZE}, got {width}x{height}"
                )
            assets.append(
                {
                    "id": icon_id,
                    "category": category_number,
                    "category_name": CATEGORY_NAMES[category],
                    "name": name,
                    "path": png_path,
                    "width": width,
                    "height": height,
                    "pixels": pixels,
                }
            )

    expected = int(manifest.get("icon_count", -1))
    if len(assets) != expected:
        raise ValueError(f"manifest icon_count={expected}, decoded={len(assets)}")
    manifest["sha256"] = hashlib.sha256(manifest_bytes).hexdigest()
    return manifest, assets


def render_header(manifest: dict, assets: list[dict]) -> str:
    lines = [
        "/* Generated by scripts/generate-gui-assets.py; do not edit. */",
        f"/* Pack: {manifest['pack']} / {len(assets)} icons / {RUNTIME_SIZE}px runtime set. */",
        "",
    ]
    for asset in assets:
        symbol = f"KU_ICON_{asset['category_name']}_{macro_name(asset['name'])}"
        lines.append(
            f"#define {symbol} ((ku_icon_id_t)UINT16_C(0x{asset['id']:04X}))"
        )
    lines.append("")
    return "\n".join(lines)


def render_data(manifest: dict, assets: list[dict]) -> str:
    lines = [
        "// Generated by scripts/generate-gui-assets.py; do not edit.",
        f'constexpr char kGeneratedPackName[] = "{manifest["pack"]}";',
        f'constexpr char kGeneratedManifestSha256[] = "{manifest["sha256"]}";',
        "",
    ]
    for asset in assets:
        array_name = f"kIconPixels{asset['id']:04X}"
        lines.append(f"constexpr uint32_t {array_name}[] = {{")
        pixels = asset["pixels"]
        for start in range(0, len(pixels), 8):
            values = ", ".join(
                f"UINT32_C(0x{pixel:08X})" for pixel in pixels[start : start + 8]
            )
            lines.append(f"    {values},")
        lines.append("};")
    lines.extend(["", "constexpr IconAsset kGeneratedIcons[] = {"])
    for asset in assets:
        lines.append(
            "    {UINT16_C(0x%04X), static_cast<Category>(%d), %dU, %dU, "
            '"%s", "%s", kIconPixels%04X},'
            % (
                asset["id"],
                asset["category"],
                asset["width"],
                asset["height"],
                asset["name"],
                asset["path"],
                asset["id"],
            )
        )
    lines.extend(
        [
            "};",
            "constexpr size_t kGeneratedIconCount =",
            "    sizeof(kGeneratedIcons) / sizeof(kGeneratedIcons[0]);",
            "",
        ]
    )
    return "\n".join(lines)


def update(path: pathlib.Path, content: str, check: bool) -> bool:
    encoded = content.encode("utf-8")
    existing = path.read_bytes() if path.exists() else None
    if existing == encoded:
        return True
    if check:
        print(f"out of date: {path.relative_to(ROOT)}", file=sys.stderr)
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)
    print(f"generated: {path.relative_to(ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", type=pathlib.Path, default=DEFAULT_PACK)
    parser.add_argument("--header", type=pathlib.Path, default=DEFAULT_HEADER)
    parser.add_argument("--data", type=pathlib.Path, default=DEFAULT_DATA)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    try:
        manifest, assets = load_assets(arguments.pack.resolve())
        header_ok = update(arguments.header.resolve(), render_header(manifest, assets), arguments.check)
        data_ok = update(arguments.data.resolve(), render_data(manifest, assets), arguments.check)
    except (KeyError, OSError, ValueError, zipfile.BadZipFile, zlib.error) as error:
        print(f"GUI asset generation failed: {error}", file=sys.stderr)
        return 1
    if not header_ok or not data_ok:
        return 2
    print(
        f"GUI assets: {len(assets)} icons, {len(CATEGORY_NAMES)} categories, "
        f"manifest {manifest['sha256'][:12]}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
