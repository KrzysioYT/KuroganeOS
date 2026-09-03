#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

HOST_CC="${HOST_CC:-cc}"
HOST_CXX="${HOST_CXX:-c++}"
HOST_PYTHON="${HOST_PYTHON:-python3}"
OUT_DIR="${HOST_TEST_DIR:-build/tests/host}"

mkdir -p "$OUT_DIR"

echo "[host-tests] C compiler:   $HOST_CC"
echo "[host-tests] C++ compiler: $HOST_CXX"
echo "[host-tests] python:       $HOST_PYTHON"

"$HOST_PYTHON" tests/test_release_version.py

"$HOST_PYTHON" tests/test_mouse_first_apps.py

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isdk/include \
  tests/test_sdk_abi.cpp \
  -o "$OUT_DIR/test_sdk_abi"

"$OUT_DIR/test_sdk_abi"

# Exercise production libui row geometry and pointer hit-testing.
"$HOST_CC" \
  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -ffreestanding \
  -Isdk/include \
  tests/test_libui_pointer.c sdk/src/libui.c \
  -o "$OUT_DIR/test_libui_pointer"

"$OUT_DIR/test_libui_pointer"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isdk/include \
  tests/test_graphics_runtime.cpp \
  -o "$OUT_DIR/test_graphics_runtime"

"$OUT_DIR/test_graphics_runtime"

# Exercise the real framebuffer backbuffer with a bounded outer damage mask.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_framebuffer_damage.cpp \
  kernel/drivers/framebuffer.cpp \
  -o "$OUT_DIR/test_framebuffer_damage"

"$OUT_DIR/test_framebuffer_damage"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  tests/test_vfs_process_paths.cpp \
  kernel/fs/vfs.cpp \
  -o "$OUT_DIR/test_vfs_process_paths"

"$OUT_DIR/test_vfs_process_paths"

# Exercise KuroFS v1 metadata persistence through the production block-device ABI.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_kurofs_allocator.cpp \
  kernel/fs/kurofs.cpp \
  -o "$OUT_DIR/test_kurofs_allocator"

"$OUT_DIR/test_kurofs_allocator"

# Exercise durable KuroFS extent contents and generation-safe inode publication.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_kurofs_data.cpp \
  kernel/fs/kurofs.cpp \
  -o "$OUT_DIR/test_kurofs_data"

"$OUT_DIR/test_kurofs_data"

# Exercise CRC-protected KuroFS directory persistence and copy-on-grow.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_kurofs_directory.cpp \
  kernel/fs/kurofs.cpp \
  -o "$OUT_DIR/test_kurofs_directory"

"$OUT_DIR/test_kurofs_directory"

# Interrupt every persistent write/flush phase of cross-directory moves and
# require remount recovery to expose exactly the old or the new namespace.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_kurofs_move_recovery.cpp \
  kernel/fs/kurofs.cpp \
  -o "$OUT_DIR/test_kurofs_move_recovery"

"$OUT_DIR/test_kurofs_move_recovery"

# Refuse live inode overlap, free-block ownership, stale directory identities
# and unsupported inode/link metadata before a KuroFS mount is exposed.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_kurofs_consistency.cpp \
  kernel/fs/kurofs.cpp \
  -o "$OUT_DIR/test_kurofs_consistency"

"$OUT_DIR/test_kurofs_consistency"

# Exercise KuroFS through the production VFS routing/open/read/readdir API.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_kurofs_vfs.cpp \
  kernel/fs/kurofs_vfs.cpp \
  kernel/fs/kurofs.cpp \
  kernel/fs/vfs.cpp \
  -o "$OUT_DIR/test_kurofs_vfs"

"$OUT_DIR/test_kurofs_vfs"

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
  -DKUROGANE_HOST_TEST -ffunction-sections -fdata-sections \
  tests/test_socket.cpp \
  kernel/net/socket.cpp \
  kernel/net/tcp_client.cpp \
  -Wl,--gc-sections \
  -o "$OUT_DIR/test_socket"

"$OUT_DIR/test_socket"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_dns_protocol.cpp \
  kernel/net/protocols.cpp \
  kernel/net/network.cpp \
  -o "$OUT_DIR/test_dns_protocol"

"$OUT_DIR/test_dns_protocol"

"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -DKUROGANE_HOST_TEST \
  tests/test_diagnostic_event_ring.cpp \
  kernel/diagnostics/event_ring.cpp \
  -o "$OUT_DIR/test_diagnostic_event_ring"

"$OUT_DIR/test_diagnostic_event_ring"

# Exercise the production Flux Window Core, including retained surface
# ownership and generation-safe cleanup, without framebuffer hardware.
"$HOST_CXX" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -DKUROGANE_HOST_TEST \
  tests/test_window_manager.cpp \
  kernel/ui/window_manager.cpp \
  -o "$OUT_DIR/test_window_manager"

"$OUT_DIR/test_window_manager"

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

# Verify that every application manifest has a deterministic, unique physical
# installer path that satisfies the kernel installer's FAT 8.3 contract.
"$HOST_PYTHON" tests/test_install_package_paths.py --root "$ROOT_DIR"
"$HOST_PYTHON" tests/test_install_package_builder.py

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
