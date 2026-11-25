#!/usr/bin/env bash
# Bounded host checks for memory/ELF/IPC, scheduling, ownership and package paths.
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${HOST_CXX:-c++}"
out="${HOST_TEST_DIR:-build/tests/core-ubsan}"
mkdir -p "$out"
# Driver fixtures compile production x86 instructions, even on Apple Silicon.
# Use the same native-host target/linker policy as run-host-tests.sh.
arch_flags=()
link_flags=(-Wl,--gc-sections)
if [[ "$(uname -s)" == Darwin ]]; then
    arch_flags=(-arch x86_64)
    link_flags=(-Wl,-dead_strip)
fi
run() {
    local name="$1"
    shift
    "$compiler" "${arch_flags[@]}" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic \
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
# Re-run production RTL81xx descriptor validation and injected TX/RX fault
# containment under UBSan; simulation does not qualify physical hardware.
run rtl81xx-ring tests/test_rtl81xx_ring.cpp
run rtl81xx-dma-runtime -ffunction-sections -fdata-sections \
    tests/test_rtl81xx_dma_runtime.cpp "${link_flags[@]}"
run rtl81xx-pci-start -ffunction-sections -fdata-sections \
    tests/test_rtl81xx_pci_start.cpp "${link_flags[@]}"
run rtl81xx-cleanup -ffunction-sections -fdata-sections \
    tests/test_rtl81xx_cleanup.cpp "${link_flags[@]}"
run rtl81xx-retirement -ffunction-sections -fdata-sections \
    tests/test_rtl81xx_retirement.cpp "${link_flags[@]}"
run nvme-retirement tests/test_nvme_retirement.cpp kernel/storage/nvme_protocol.cpp
run usb-hub-port tests/test_usb_hub_port.cpp
run usb-hub-ganged-power tests/test_usb_hub_ganged_power.cpp

run xhci-completions -ffunction-sections -fdata-sections \
    tests/test_xhci_completions.cpp kernel/drivers/usb/protocol.cpp \
    kernel/drivers/usb/hid_report.cpp kernel/drivers/usb/mass_storage_protocol.cpp \
    kernel/drivers/usb/xhci_bulk.cpp kernel/drivers/core/device_manager.cpp \
    kernel/storage/device_registry.cpp kernel/storage/scratch_test.cpp \
    kernel/core/system_metrics.cpp "${link_flags[@]}"
