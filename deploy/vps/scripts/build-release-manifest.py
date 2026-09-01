#!/usr/bin/env python3
"""Build the downloads.kuroganeos.dev release manifest from qualified media."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
from pathlib import Path


MEDIA_SUFFIXES = {".iso": "iso", ".img": "img", ".zip": "zip"}
VERSION_RE = re.compile(r"^KuroganeOS-(?P<version>.+?)-(?:x86_64|linux-qemu)\.(?:iso|img)$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_declared_sums(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    declared: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.strip().split(maxsplit=1)
        if len(parts) != 2:
            continue
        declared[Path(parts[1].lstrip("* ")).name] = parts[0].lower()
    return declared


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("media_directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    media_directory = args.media_directory.resolve()
    if not media_directory.is_dir():
        raise SystemExit(f"media directory does not exist: {media_directory}")

    media = sorted(
        (path for path in media_directory.iterdir() if path.suffix.lower() in MEDIA_SUFFIXES),
        key=lambda path: (0 if path.suffix.lower() == ".iso" else 1, path.name),
    )
    if not media:
        raise SystemExit("no ISO, IMG or ZIP files found")

    declared = parse_declared_sums(media_directory / "SHA256SUMS.txt")
    files = []
    version = "development"
    calculated_lines = []
    for path in media:
        digest = sha256(path)
        expected = declared.get(path.name)
        if expected and expected != digest:
            raise SystemExit(f"SHA-256 mismatch for {path.name}: expected {expected}, got {digest}")
        match = VERSION_RE.match(path.name)
        if match:
            version = match.group("version")
        files.append(
            {
                "kind": MEDIA_SUFFIXES[path.suffix.lower()],
                "name": path.name,
                "url": path.name,
                "size": path.stat().st_size,
                "sha256": digest,
            }
        )
        calculated_lines.append(f"{digest}  {path.name}")

    (media_directory / "SHA256SUMS.txt").write_text(
        "\n".join(calculated_lines) + "\n", encoding="utf-8"
    )
    manifest = {
        "schema": 1,
        "name": f"KuroganeOS {version}",
        "version": version,
        "architecture": "x86-64",
        "channel": "development",
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        "files": files,
    }
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"release manifest: {args.output} ({len(files)} files, version {version})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
