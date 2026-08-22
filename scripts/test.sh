#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/host-tests"
logs="$root/build/logs"
mkdir -p "$out" "$logs"
log="$logs/host-tests.log"
exec > >(tee "$log") 2>&1

echo "[check] generated GUI icon registry"
"${PYTHON:-python3}" "$root/scripts/generate-gui-assets.py" --check

cxx="${CXX:-g++}"
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic)
metrics_stub="$root/tests/host_system_metrics_stub.cpp"

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
run_test libk "$root/tests/test_libk.cpp" \
    "$root/kernel/libk/status.cpp" \
    "$root/kernel/libk/memory.cpp" \
    "$root/kernel/libk/string.cpp" \
    "$root/kernel/libk/format.cpp" \
    "$root/kernel/libk/hash.cpp" \
    "$root/kernel/libk/crc.cpp" \
    "$root/kernel/libk/utf8.cpp"
run_test driver-core "$root/tests/test_driver_core.cpp" \
    "$root/kernel/drivers/core/device_manager.cpp" \
    "$root/kernel/drivers/core/driver_manager.cpp"
run_test acpi "$root/tests/test_acpi.cpp" \
    "$root/kernel/arch/x86_64/acpi.cpp"
run_test install-package "$root/tests/test_install_package.cpp" \
    "$root/kernel/install/package.cpp" \
    "$root/kernel/libk/crc.cpp"
run_test install-disk-layout "$root/tests/test_install_disk_layout.cpp" \
    "$metrics_stub" \
    "$root/kernel/install/disk_layout.cpp" \
    "$root/kernel/storage/gpt.cpp" \
    "$root/kernel/storage/partition_device.cpp" \
    "$root/kernel/fs/fat32.cpp" \
    "$root/kernel/libk/crc.cpp"
run_test input -DKUROGANE_HOST_TEST "$root/tests/test_input.cpp" \
    "$root/kernel/input/input.cpp" \
    "$root/kernel/drivers/mouse_protocol.cpp"
run_test usb-protocol "$root/tests/test_usb_protocol.cpp" \
    "$root/kernel/drivers/usb/protocol.cpp"
run_test user-console "$root/tests/test_user_console.cpp" \
    "$root/kernel/user/console.cpp"
run_test window-manager -DKUROGANE_HOST_TEST -I"$root/sdk/include" \
    "$root/tests/test_window_manager.cpp" \
    "$root/kernel/ui/window_manager.cpp"
run_test icon-registry -I"$root/sdk/include" \
    "$root/tests/test_icon_registry.cpp" \
    "$root/kernel/ui/icon_registry.cpp"
run_test virtual-memory "$root/tests/test_virtual_memory.cpp" \
    "$root/kernel/memory/virtual_memory.cpp"
run_test elf-loader "$root/tests/test_elf_loader.cpp" \
    "$root/kernel/user/elf_loader.cpp" \
    "$root/kernel/memory/virtual_memory.cpp" \
    "$root/kernel/memory/physical_memory.cpp"
run_test gpt "$root/tests/test_gpt.cpp" \
    "$metrics_stub" \
    "$root/kernel/storage/gpt.cpp"
run_test partition-device "$root/tests/test_partition_device.cpp" \
    "$metrics_stub" \
    "$root/kernel/storage/partition_device.cpp"
run_test storage-scratch "$root/tests/test_storage_scratch.cpp" \
    "$metrics_stub" \
    "$root/kernel/storage/scratch_test.cpp"
run_test ahci "$root/tests/test_ahci.cpp" \
    "$metrics_stub" \
    "$root/kernel/storage/ahci_protocol.cpp" \
    "$root/kernel/storage/dma.cpp" \
    "$root/kernel/memory/physical_memory.cpp"
run_test ramfs "$root/tests/test_ramfs.cpp" \
    "$root/kernel/fs/ramfs.cpp" "$root/kernel/memory/allocator.cpp" \
    "$root/kernel/core/string.cpp"
run_test vfs "$root/tests/test_vfs.cpp" \
    "$root/kernel/fs/vfs.cpp"
run_test fat32 "$root/tests/test_fat32.cpp" \
    "$metrics_stub" \
    "$root/kernel/fs/fat32.cpp" \
    "$root/kernel/fs/fat32_vfs.cpp" \
    "$root/kernel/fs/vfs.cpp"
if [[ -f "$root/build/images/KuroganeOS-base.img" ]]; then
    echo "[build] root-volume-image"
    "$cxx" "${flags[@]}" \
        "$root/tests/test_root_volume_image.cpp" \
        "$metrics_stub" \
        "$root/tests/host_process_path_stub.cpp" \
        "$root/kernel/fs/root_volume.cpp" \
        "$root/kernel/fs/fat32.cpp" \
        "$root/kernel/fs/fat32_vfs.cpp" \
        "$root/kernel/fs/vfs.cpp" \
        "$root/kernel/install/package.cpp" \
        "$root/kernel/install/package_vfs.cpp" \
        "$root/kernel/libk/crc.cpp" \
        "$root/kernel/storage/gpt.cpp" \
        "$root/kernel/storage/partition_device.cpp" \
        -o "$out/root-volume-image"
    echo "[run] root-volume-image"
    "$out/root-volume-image" "$root/build/images/KuroganeOS-base.img"
    echo "[pass] root-volume-image"
else
    echo "[skip] root-volume-image (build Foundation image first)"
fi
run_test scheduler "$root/tests/test_scheduler.cpp" \
    "$root/kernel/task/scheduler.cpp"
echo "[build] kernel-thread"
"$cxx" "${flags[@]}" -DKUROGANE_HOST_TEST -c \
    "$root/tests/test_thread.cpp" -o "$out/test-thread.o"
"$cxx" "${flags[@]}" -DKUROGANE_HOST_TEST -c \
    "$root/kernel/task/thread.cpp" -o "$out/thread.o"
"$cxx" -DKUROGANE_HOST_TEST -c -x assembler-with-cpp \
    "$root/kernel/arch/x86_64/context_switch.asm" \
    -o "$out/context-switch.o"
"$cxx" "$out/test-thread.o" "$out/thread.o" "$out/context-switch.o" \
    -o "$out/kernel-thread"
echo "[run] kernel-thread"
"$out/kernel-thread"
echo "[pass] kernel-thread"
echo "[build] process-core"
"$cxx" "${flags[@]}" -DKUROGANE_HOST_TEST -c \
    "$root/tests/test_process.cpp" -o "$out/test-process.o"
"$cxx" "${flags[@]}" -DKUROGANE_HOST_TEST -c \
    "$root/kernel/task/process.cpp" -o "$out/process.o"
"$cxx" "$out/test-process.o" "$out/process.o" \
    "$out/thread.o" "$out/context-switch.o" -o "$out/process-core"
echo "[run] process-core"
"$out/process-core"
echo "[pass] process-core"
run_test network "$root/tests/test_network.cpp" \
    "$root/kernel/net/network.cpp" \
    "$root/kernel/net/protocols.cpp"
run_test network-protocols "$root/tests/test_network_protocols.cpp" \
    "$root/kernel/net/network.cpp" \
    "$root/kernel/net/protocols.cpp"
run_test profiler -DKUROGANE_HOST_TEST "$root/tests/test_profiler.cpp" \
    "$root/tests/host_network_service_stub.cpp" \
    "$root/kernel/diagnostics/profiler.cpp" \
    "$root/kernel/apps/framework.cpp" \
    "$root/kernel/memory/allocator.cpp" \
    "$root/kernel/memory/physical_memory.cpp" \
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
