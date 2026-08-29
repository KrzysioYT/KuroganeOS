#!/usr/bin/env python3
"""Validate a KuroganeOS fatal-diagnostic serial transcript."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

R12_SENTINEL = 0x4B55524F50414E49
R13_SENTINEL = 0x435452415046524D


def fail(message: str) -> None:
    raise SystemExit(f"panic serial validation failed: {message}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("serial_log", type=Path)
    parser.add_argument("--nested", action="store_true")
    return parser.parse_args()


def require(text: str, needle: str) -> None:
    if needle not in text:
        fail(f"missing marker {needle!r}")


def key_values(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line or line.startswith("EVENT "):
            continue
        key, value = line.split("=", 1)
        if re.fullmatch(r"[A-Z0-9_\[\]]+", key):
            result[key] = value.strip()
    return result


def parse_hex(values: dict[str, str], key: str) -> int:
    raw = values.get(key)
    if raw is None:
        fail(f"missing {key}")
    if not re.fullmatch(r"0x[0-9A-Fa-f]+", raw):
        fail(f"{key} is not hexadecimal: {raw!r}")
    return int(raw, 16)


def parse_uint(values: dict[str, str], key: str) -> int:
    raw = values.get(key)
    if raw is None or not raw.isdigit():
        fail(f"{key} is not an unsigned integer: {raw!r}")
    return int(raw)


def validate_nested(text: str) -> None:
    require(text, "[PANIC-TEST] deliberate invalid opcode now")
    require(text, "=== KUROGANE_FATAL_NESTED ===")
    require(text, "NESTED PANIC: minimal serial fallback")
    require(text, "DUMP UNAVAILABLE: nested panic")
    if "[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL" in text:
        fail("ordinary boot-failure marker appeared during nested panic smoke")


def validate_full(text: str) -> None:
    require(text, "[PANIC-TEST] deliberate invalid opcode now")
    require(text, "=== KUROGANE_FATAL_BEGIN ===")
    require(text, "=== KUROGANE_FATAL_END ===")

    values = key_values(text)
    expected = {
        "REASON": "invalid opcode",
        "VECTOR": "6",
        "ERROR_CODE": "0x0",
        "CONTEXT": "KERNEL",
        "PROCESS": "kernel",
        "THREAD": "panic-qualifier",
        "CR2": "N/A",
    }
    for key, wanted in expected.items():
        actual = values.get(key)
        if actual != wanted:
            fail(f"{key} expected {wanted!r}, got {actual!r}")

    if values.get("BUILD_ID", "").endswith("unavailable"):
        fail("build identifier is unavailable")
    if values.get("COMMIT_ID", "").endswith("unavailable"):
        fail("commit identifier is unavailable")

    if parse_uint(values, "TID") == 0:
        fail("panic qualifier did not expose a real kernel ThreadId")
    parse_uint(values, "PID")
    parse_uint(values, "UPTIME_TICKS")

    for register in (
        "RIP", "RSP", "RFLAGS", "CS", "SS", "RAX", "RBX", "RCX",
        "RDX", "RSI", "RDI", "RBP", "R8", "R9", "R10", "R11",
        "R12", "R13", "R14", "R15",
    ):
        parse_hex(values, register)
    if parse_hex(values, "RIP") == 0 or parse_hex(values, "RSP") == 0:
        fail("RIP/RSP must be non-zero real trap state")
    if parse_hex(values, "R12") != R12_SENTINEL:
        fail("R12 trap-frame sentinel mismatch")
    if parse_hex(values, "R13") != R13_SENTINEL:
        fail("R13 trap-frame sentinel mismatch")

    event_count = parse_uint(values, "LAST_KERNEL_EVENTS_COUNT")
    if event_count == 0:
        fail("LAST KERNEL EVENTS snapshot is empty")
    if not re.search(
        r"^EVENT .*subsystem=PANIC-TEST message=deliberate invalid-opcode fault armed$",
        text,
        flags=re.MULTILINE,
    ):
        fail("real pre-fault PANIC-TEST event is missing")

    dump = values.get("DUMP", "")
    if not (
        dump.startswith("DUMP UNAVAILABLE:")
        or dump.startswith("WRITTEN: KURODMP1")
    ):
        fail(f"dump status is not typed: {dump!r}")

    if "[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL" in text:
        fail("ordinary boot-failure marker appeared during deliberate panic smoke")


args = parse_args()
if not args.serial_log.is_file():
    fail(f"serial log does not exist: {args.serial_log}")
text = args.serial_log.read_text(encoding="utf-8", errors="replace")
if args.nested:
    validate_nested(text)
else:
    validate_full(text)
print("[panic-serial] PASS")
