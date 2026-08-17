#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

HOST_CXX="${HOST_CXX:-c++}"
HOST_PYTHON="${HOST_PYTHON:-python3}"
OUT_DIR="${HOST_TEST_DIR:-build/tests/host}"

mkdir -p "$OUT_DIR"

echo "[host-tests] compiler: $HOST_CXX"
echo "[host-tests] python:   $HOST_PYTHON"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isdk/include \
  tests/test_sdk_abi.cpp \
  -o "$OUT_DIR/test_sdk_abi"

"$OUT_DIR/test_sdk_abi"
"$HOST_PYTHON" tests/test_sdk_project_generator.py

echo "[host-tests] PASS"
