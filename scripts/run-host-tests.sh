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

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isdk/include \
  tests/test_graphics_runtime.cpp \
  -o "$OUT_DIR/test_graphics_runtime"

"$OUT_DIR/test_graphics_runtime"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_vfs_process_paths.cpp \
  kernel/fs/vfs.cpp \
  -o "$OUT_DIR/test_vfs_process_paths"

"$OUT_DIR/test_vfs_process_paths"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_ipc.cpp \
  kernel/ipc/channel.cpp \
  -o "$OUT_DIR/test_ipc"

"$OUT_DIR/test_ipc"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_shared_memory.cpp \
  kernel/ipc/shared_memory.cpp \
  kernel/memory/physical_memory.cpp \
  -o "$OUT_DIR/test_shared_memory"

"$OUT_DIR/test_shared_memory"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_event.cpp \
  kernel/ipc/event.cpp \
  -o "$OUT_DIR/test_event"

"$OUT_DIR/test_event"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_socket.cpp \
  kernel/net/socket.cpp \
  -o "$OUT_DIR/test_socket"

"$OUT_DIR/test_socket"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -DKUROGANE_HOST_TEST \
  tests/test_diagnostic_event_ring.cpp \
  kernel/diagnostics/event_ring.cpp \
  -o "$OUT_DIR/test_diagnostic_event_ring"

"$OUT_DIR/test_diagnostic_event_ring"

# Exercise the production TCP client with a deterministic fake wire. This
# protects stream reassembly and the SND.UNA/SND.NXT retransmission contract
# without relying on host sockets or libc networking.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -DKUROGANE_HOST_TEST \
  tests/test_tcp_client.cpp \
  kernel/net/tcp_client.cpp \
  -o "$OUT_DIR/test_tcp_client"

"$OUT_DIR/test_tcp_client"

# Keep the pinned TLS client source set freestanding and host-libc independent.
# This is compile-only by design; runtime HTTPS qualification happens after the
# Kurogane TCP BIO, entropy and trust-store pieces are wired together.
bash ./scripts/probe-mbedtls-freestanding.sh

# The project-generator integration test consumes the real SDK sysroot rather
# than an ad-hoc header copy. Building it here also validates that public ABI
# header changes still compile into crt0/libc/libkurogane/libui and desktop ELFs.
bash ./scripts/build-sdk.sh

"$HOST_PYTHON" tests/test_sdk_project_generator.py \
  --root "$ROOT_DIR" \
  --cxx "${KUROGANE_CXX:-${CXX:-g++}}"

echo "[host-tests] PASS"
