#!/usr/bin/env python3
"""Integration test for runnable KuroganeOS SDK project generation."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def run(command: list[str], expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if result.returncode != expected:
        raise AssertionError(
            f"command returned {result.returncode}, expected {expected}: {command}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_reset(workspace: Path, build_root: Path) -> None:
    resolved_workspace = workspace.resolve()
    resolved_build = build_root.resolve()
    if resolved_workspace == resolved_build or resolved_build not in resolved_workspace.parents:
        raise AssertionError(f"unsafe generator test workspace: {resolved_workspace}")
    if workspace.exists():
        shutil.rmtree(workspace)
    workspace.mkdir(parents=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--make", default="make")
    args = parser.parse_args()

    root = args.root.resolve()
    build_root = root / "build"
    workspace = build_root / "sdk" / "project-generator-tests"
    sysroot = build_root / "sdk" / "sysroot"
    generator = root / "scripts" / "create-sdk-project.py"
    abi_header = sysroot / "usr" / "include" / "kurogane" / "abi.h"
    if not abi_header.is_file():
        raise AssertionError(
            f"SDK sysroot is missing: {abi_header}; run scripts/build-sdk.sh first"
        )

    safe_reset(workspace, build_root)
    listing = run([sys.executable, str(generator), "--list-templates"]).stdout
    for expected in ("console:", "gui:", "service:", "driver: unavailable"):
        if expected not in listing:
            raise AssertionError(f"template listing is missing {expected!r}")

    for template in ("console", "gui", "service"):
        name = f"sdk-{template}-probe"
        project = workspace / name
        run(
            [
                sys.executable,
                str(generator),
                "--template",
                template,
                "--name",
                name,
                "--output",
                str(project),
            ]
        )
        manifest = json.loads((project / "kurogane-project.json").read_text("utf-8"))
        if manifest["artifact_kind"] != "elf64-executable":
            raise AssertionError("generator produced an unexpected artifact kind")
        if not manifest["runtime_supported"] or not manifest["link_supported"]:
            raise AssertionError("runnable template does not advertise its link contract")
        run(
            [
                args.make,
                "-C",
                str(project),
                f"CXX={args.cxx}",
                f"KUROGANE_SYSROOT={sysroot}",
            ]
        )
        expected_elf = project / "build" / name
        if not expected_elf.is_file() or expected_elf.stat().st_size < 64:
            raise AssertionError(f"generated project did not build {expected_elf}")
        header = expected_elf.read_bytes()[:20]
        if header[:5] != b"\x7fELF\x02" or int.from_bytes(header[16:18], "little") != 2:
            raise AssertionError(f"generated project is not ELF64 ET_EXEC: {expected_elf}")

    protected_project = workspace / "sdk-console-probe"
    protected_source = protected_project / "src" / "capability_probe.cpp"
    source_digest = digest(protected_source)
    overwrite = run(
        [
            sys.executable,
            str(generator),
            "--template",
            "console",
            "--name",
            "sdk-console-probe",
            "--output",
            str(protected_project),
        ],
        expected=2,
    )
    if "refusing to overwrite" not in overwrite.stderr:
        raise AssertionError("generator did not explain overwrite refusal")
    if digest(protected_source) != source_digest:
        raise AssertionError("overwrite refusal changed an existing project")

    driver_output = workspace / "unsupported-driver"
    driver = run(
        [
            sys.executable,
            str(generator),
            "--template",
            "driver",
            "--name",
            "unsupported-driver",
            "--output",
            str(driver_output),
        ],
        expected=2,
    )
    if "no driver/module ABI" not in driver.stderr or driver_output.exists():
        raise AssertionError("unsupported driver template was not rejected safely")

    invalid_output = workspace / "invalid-name"
    invalid = run(
        [
            sys.executable,
            str(generator),
            "--template",
            "console",
            "--name",
            "../escape",
            "--output",
            str(invalid_output),
        ],
        expected=2,
    )
    if "invalid project name" not in invalid.stderr or invalid_output.exists():
        raise AssertionError("unsafe project name was not rejected")

    print("SDK project generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
