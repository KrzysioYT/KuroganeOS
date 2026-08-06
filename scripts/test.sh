#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/host-tests"
logs="$root/build/logs"
mkdir -p "$out" "$logs"
log="$logs/host-tests.log"
exec > >(tee "$log") 2>&1

cxx="${CXX:-g++}"
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic)

run_test() {
    local name="$1"
    shift
    echo "[build] $name"
    "$cxx" "${flags[@]}" "$@" -o "$out/$name"
    echo "[run] $name"
    "$out/$name"
    echo "[pass] $name"
}

# Freestanding libc symbols must not interpose on the hosted process runtime.
run_test memory "$root/tests/test_memory_allocator.cpp" \
    "$root/kernel/memory/allocator.cpp" \
    "$root/kernel/memory/physical_memory.cpp"
run_test virtual-memory "$root/tests/test_virtual_memory.cpp" \
    "$root/kernel/memory/virtual_memory.cpp"
run_test ramfs "$root/tests/test_ramfs.cpp" \
    "$root/kernel/fs/ramfs.cpp" "$root/kernel/memory/allocator.cpp" \
    "$root/kernel/core/string.cpp"
run_test scheduler "$root/tests/test_scheduler.cpp" \
    "$root/kernel/task/scheduler.cpp"
run_test network "$root/tests/test_network.cpp" \
    "$root/kernel/net/network.cpp"
run_test profiler "$root/tests/test_profiler.cpp" \
    "$root/kernel/diagnostics/profiler.cpp" \
    "$root/kernel/apps/framework.cpp" \
    "$root/kernel/memory/allocator.cpp" \
    "$root/kernel/memory/physical_memory.cpp" \
    "$root/kernel/net/network.cpp" \
    "$root/kernel/net/service.cpp" \
    "$root/kernel/task/scheduler.cpp" \
    "$root/kernel/core/string.cpp"
run_test sdk-abi -I"$root/sdk/include" \
    "$root/tests/test_sdk_abi.cpp"
run_test sdk-test -I"$root/sdk/include" \
    "$root/tests/test_sdk_test.cpp"

echo "[build] sdk-sysroot"
CXX="$cxx" "$root/scripts/build-sdk.sh"
echo "[run] sdk-project-generator"
"${PYTHON:-python3}" "$root/tests/test_sdk_project_generator.py" \
    --root "$root" --cxx "$cxx" --make "${MAKE:-make}"
echo "[pass] sdk-project-generator"

echo "[pass] all host tests"
