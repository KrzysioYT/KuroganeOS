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

# Exercise canonicalization, relative paths, cwd/chdir/getcwd, chroot bounds,
# generation-checked handles and the mutable VFS contract on the host. These
# semantics are shared by the Ring-3 process-local PathContext wrappers.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_vfs.cpp \
  kernel/fs/vfs.cpp \
  -o "$OUT_DIR/test_vfs"

"$OUT_DIR/test_vfs"

# Keep the process table's cwd metadata and child inheritance under regression
# coverage. The process/thread host path does not enter Ring 3, so it is safe to
# run as a normal host executable.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -DKUROGANE_HOST_TEST=1 \
  tests/test_process.cpp \
  kernel/task/process.cpp \
  kernel/task/thread.cpp \
  -o "$OUT_DIR/test_process"

"$OUT_DIR/test_process"

# The project-generator integration test consumes the real SDK sysroot rather
# than an ad-hoc header copy. Building it here also validates that public ABI
# header changes still compile into crt0/libc/libkurogane/libui and desktop ELFs.
bash ./scripts/build-sdk.sh

"$HOST_PYTHON" tests/test_sdk_project_generator.py \
  --root "$ROOT_DIR" \
  --cxx "${KUROGANE_CXX:-${CXX:-g++}}"

echo "[host-tests] PASS"
