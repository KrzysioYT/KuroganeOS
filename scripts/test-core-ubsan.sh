#!/usr/bin/env bash
# Bounded host checks for memory/ELF/IPC, scheduling, ownership and package paths.
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${HOST_CXX:-c++}"
out="${HOST_TEST_DIR:-build/tests/core-ubsan}"
mkdir -p "$out"
run() {
    local name="$1"
    shift
    "$compiler" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic \
        -fsanitize=undefined -fno-sanitize-recover=all "$@" -o "$out/$name"
    "$out/$name"
    echo "[core-ubsan] $name: PASS"
}
run allocator tests/test_memory_allocator.cpp kernel/memory/allocator.cpp kernel/memory/physical_memory.cpp
run virtual-memory tests/test_virtual_memory.cpp kernel/memory/virtual_memory.cpp
run elf-loader tests/test_elf_loader.cpp kernel/user/elf_loader.cpp kernel/memory/virtual_memory.cpp kernel/memory/physical_memory.cpp
run scheduler tests/test_scheduler2.cpp kernel/task/scheduler2.cpp
run spinlock -pthread tests/test_sync_spinlock.cpp
run ipc tests/test_ipc.cpp kernel/ipc/channel.cpp
run shared-memory tests/test_shared_memory.cpp kernel/ipc/shared_memory.cpp kernel/memory/physical_memory.cpp
run handles tests/test_file_handle.cpp
run session-owner tests/test_flux_session_owner_policy.cpp
run package-vfs tests/test_package_vfs.cpp kernel/install/package_vfs.cpp kernel/install/package.cpp kernel/fs/vfs.cpp kernel/libk/crc.cpp
run abi -Isdk/include tests/test_sdk_abi.cpp
