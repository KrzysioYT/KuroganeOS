#!/usr/bin/env python3
"""Host regressions for installer-side production path contracts."""

from __future__ import annotations

import argparse
import importlib.util
import pathlib
import re


def load_builder(root: pathlib.Path):
    module_path = root / "scripts" / "build-install-package.py"
    spec = importlib.util.spec_from_file_location("kurogane_install_package", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load installer builder: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def production_userspace_outputs(root: pathlib.Path) -> list[str]:
    build = (root / "scripts" / "build-linux.sh").read_text(encoding="utf-8")
    start = build.find("applications=(")
    if start < 0:
        raise AssertionError("build-linux.sh has no applications table")
    end = build.find("\n)\n", start)
    if end < 0:
        raise AssertionError("build-linux.sh applications table is unterminated")
    table = build[start:end]
    outputs: list[str] = []
    pattern = re.compile(r'^\s*"[^|]+\|[^|]+\|([^|]+)\|(?:asm|c)"\s*$', re.MULTILINE)
    for match in pattern.finditer(table):
        outputs.append(match.group(1))
    if not outputs:
        raise AssertionError("build-linux.sh applications table yielded no outputs")
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    builder = load_builder(root)

    outputs = production_userspace_outputs(root)
    seen_outputs: set[str] = set()
    for output in outputs:
        checked = builder.checked_path("/" + output)
        if checked != "/" + output:
            raise AssertionError(f"userspace output changed during validation: {output}")
        folded = checked.upper()
        if folded in seen_outputs:
            raise AssertionError(f"duplicate userspace output path: {output}")
        seen_outputs.add(folded)

    manifest_root = root / "rootfs" / "apps" / "appman"
    if not manifest_root.is_dir():
        raise AssertionError(f"missing application manifest directory: {manifest_root}")

    manifests = sorted(path for path in manifest_root.iterdir() if path.is_file())
    if not manifests:
        raise AssertionError("application manifest directory is empty")

    mapped_paths: set[str] = set()
    for manifest in manifests:
        relative = manifest.relative_to(root / "rootfs").as_posix()
        mapped = builder.installer_relative_path(relative)
        checked = builder.checked_path("/" + mapped)
        if checked != "/" + mapped:
            raise AssertionError(f"installer path changed during validation: {mapped}")
        mapped_path = pathlib.PurePosixPath(mapped)
        if mapped_path.parent.as_posix() != builder.APP_MANIFEST_DIRECTORY:
            raise AssertionError(f"manifest escaped physical directory: {mapped}")
        if mapped_path.suffix.upper() != ".MNF":
            raise AssertionError(f"manifest transport extension is not .MNF: {mapped}")
        if not builder.is_short_component(mapped_path.parent.name):
            raise AssertionError(f"manifest directory is not FAT 8.3-safe: {mapped}")
        if not builder.is_short_component(mapped_path.name):
            raise AssertionError(f"manifest filename is not FAT 8.3-safe: {mapped}")
        if mapped in mapped_paths:
            raise AssertionError(f"manifest physical-path collision: {mapped}")
        mapped_paths.add(mapped)

    ordinary = "apps/about"
    if builder.installer_relative_path(ordinary) != ordinary:
        raise AssertionError("non-manifest package path was unexpectedly remapped")

    print(
        "installer FAT 8.3 production paths passed "
        f"({len(outputs)} userspace outputs, {len(manifests)} manifests)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
