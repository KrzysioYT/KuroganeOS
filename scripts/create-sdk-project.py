#!/usr/bin/env python3
"""Create a linkable KuroganeOS ELF64 application project."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


NAME_PATTERN = re.compile(r"[A-Za-z][A-Za-z0-9_-]{0,63}\Z")
SUPPORTED_TEMPLATES = {
    "console": {
        "features": ["processes"],
        "feature_expression": "KU_ABI_FEATURE_PROCESSES",
    },
    "gui": {
        "features": ["processes", "gui", "input"],
        "feature_expression": (
            "KU_ABI_FEATURE_PROCESSES | KU_ABI_FEATURE_GUI | "
            "KU_ABI_FEATURE_INPUT"
        ),
    },
    "service": {
        "features": ["processes"],
        "feature_expression": "KU_ABI_FEATURE_PROCESSES",
    },
}
UNAVAILABLE_TEMPLATES = {
    "driver": (
        "the public SDK has no driver/module ABI; generating a loadable-driver "
        "project would falsely imply kernel support"
    )
}
TEXT_FILES = {
    "common/Makefile.in": "Makefile",
    "common/README.md.in": "README.md",
    "{template}/capability_probe.cpp.in": "src/capability_probe.cpp",
}


class GeneratorError(RuntimeError):
    pass


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate a freestanding, statically linked KuroganeOS ELF64 project."
        )
    )
    parser.add_argument("--template", help="console, gui, service, or driver")
    parser.add_argument("--name", help="project name (letters, digits, '_' or '-')")
    parser.add_argument("--output", type=Path, help="new project directory")
    parser.add_argument(
        "--list-templates",
        action="store_true",
        help="list supported and intentionally unavailable templates",
    )
    args = parser.parse_args(argv)
    if not args.list_templates:
        missing = [
            option
            for option in ("template", "name", "output")
            if getattr(args, option) is None
        ]
        if missing:
            parser.error(
                "the following arguments are required: "
                + ", ".join(f"--{option}" for option in missing)
            )
    return args


def print_templates() -> None:
    for name in sorted(SUPPORTED_TEMPLATES):
        print(f"{name}: runnable ELF64 application")
    for name, reason in sorted(UNAVAILABLE_TEMPLATES.items()):
        print(f"{name}: unavailable ({reason})")


def render(template_text: str, replacements: dict[str, str]) -> str:
    result = template_text
    for key, value in replacements.items():
        result = result.replace(f"@{key}@", value)
    unresolved = sorted(set(re.findall(r"@[A-Z_]+@", result)))
    if unresolved:
        raise GeneratorError(
            "unresolved template fields: " + ", ".join(unresolved)
        )
    return result


def check_destination(output: Path) -> Path:
    if os.path.lexists(output):
        raise GeneratorError(f"refusing to overwrite existing path: {output}")

    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        parent = output.parent.resolve(strict=True)
    except OSError as error:
        raise GeneratorError(
            f"cannot prepare output parent {output.parent}: {error}"
        ) from error

    destination = parent / output.name
    if os.path.lexists(destination):
        raise GeneratorError(f"refusing to overwrite existing path: {destination}")
    if destination.name in ("", ".", ".."):
        raise GeneratorError("output must name a new project directory")
    return destination


def generate_project(
    repository_root: Path, template: str, name: str, output: Path
) -> Path:
    if not NAME_PATTERN.fullmatch(name):
        raise GeneratorError(
            "invalid project name; use 1-64 characters, start with a letter, "
            "and use only letters, digits, '_' or '-'"
        )
    if template in UNAVAILABLE_TEMPLATES:
        raise GeneratorError(
            f"template '{template}' is unavailable: {UNAVAILABLE_TEMPLATES[template]}"
        )
    if template not in SUPPORTED_TEMPLATES:
        choices = ", ".join(sorted(SUPPORTED_TEMPLATES | UNAVAILABLE_TEMPLATES))
        raise GeneratorError(f"unknown template '{template}'; choices: {choices}")

    destination = check_destination(output)
    templates = repository_root / "sdk" / "project-templates"
    metadata = SUPPORTED_TEMPLATES[template]
    symbol = name.replace("-", "_")
    replacements = {
        "PROJECT_NAME": name,
        "PROJECT_SYMBOL": symbol,
        "TEMPLATE": template,
        "REQUIRED_FEATURES": metadata["feature_expression"],
        "REQUIRED_FEATURE_NAMES": ", ".join(metadata["features"]),
    }

    temporary = Path(
        tempfile.mkdtemp(prefix=f".{name}.tmp-", dir=str(destination.parent))
    )
    try:
        for source_pattern, relative_destination in TEXT_FILES.items():
            source = templates / source_pattern.format(template=template)
            try:
                content = source.read_text(encoding="utf-8")
            except OSError as error:
                raise GeneratorError(f"cannot read template {source}: {error}") from error
            target = temporary / relative_destination
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(render(content, replacements), encoding="utf-8")

        manifest = {
            "schema_version": 1,
            "name": name,
            "template": template,
            "artifact_kind": "elf64-executable",
            "runtime_supported": True,
            "link_supported": True,
            "required_abi": {"major": 1, "minor": 0},
            "required_features": metadata["features"],
        }
        (temporary / "kurogane-project.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        (temporary / ".gitignore").write_text("/build/\n", encoding="utf-8")
        temporary.replace(destination)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    return destination


def main(argv: list[str]) -> int:
    args = parse_arguments(argv)
    if args.list_templates:
        print_templates()
        return 0

    repository_root = Path(__file__).resolve().parent.parent
    try:
        destination = generate_project(
            repository_root, args.template, args.name, args.output
        )
    except GeneratorError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    except OSError as error:
        print(f"error: could not create project: {error}", file=sys.stderr)
        return 2

    print(f"Created runnable KuroganeOS SDK project: {destination}")
    print("Build it to produce a static ELF64 application for KuroganeOS.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
