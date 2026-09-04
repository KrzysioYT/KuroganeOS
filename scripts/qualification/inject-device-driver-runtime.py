#!/usr/bin/env python3
"""Inject the native 4.0 Device/Driver qualification launch points."""

from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"{path}: qualification anchor count={text.count(old)}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


main = Path("kernel/main.cpp")
driver_include = '#include "drivers/core/driver_manager.hpp"\n'
replace_once(
    main,
    driver_include,
    driver_include + '#include "drivers/core/runtime_qualification.hpp"\n',
)

framework_anchor = """    if (!context.safe_mode) {
        pci::scan();
        initialize_device_framework(false);
        initialize_storage_probe();
"""
qualification = """    if (!context.safe_mode) {
        pci::scan();
        initialize_device_framework(false);
        drivers::runtime_qualification::Result device_driver_result{};
        const KStatus device_driver_status =
            drivers::runtime_qualification::run(&device_driver_result);
        terminal::println(device_driver_result.device_claim
            ? "[TEST] device_claim: PASS" : "[TEST] device_claim: FAIL");
        terminal::println(device_driver_result.device_generation
            ? "[TEST] device_generation: PASS" : "[TEST] device_generation: FAIL");
        terminal::println(device_driver_result.device_unbind
            ? "[TEST] device_unbind: PASS" : "[TEST] device_unbind: FAIL");
        terminal::println(device_driver_result.device_remove_cleanup
            ? "[TEST] device_remove_cleanup: PASS"
            : "[TEST] device_remove_cleanup: FAIL");
        terminal::println(device_driver_result.device_stale_handle
            ? "[TEST] device_stale_handle: PASS"
            : "[TEST] device_stale_handle: FAIL");
        terminal::println(device_driver_result.device_failure_isolation
            ? "[TEST] device_failure_isolation: PASS"
            : "[TEST] device_failure_isolation: FAIL");
        terminal::println(device_driver_result.device_rebind
            ? "[TEST] device_rebind: PASS" : "[TEST] device_rebind: FAIL");
        terminal::println(device_driver_result.device_resource_boundary
            ? "[TEST] device_resource_boundary: PASS"
            : "[TEST] device_resource_boundary: FAIL");
        terminal::println(device_driver_result.driver_match
            ? "[TEST] driver_match: PASS" : "[TEST] driver_match: FAIL");
        terminal::println(device_driver_result.driver_attach
            ? "[TEST] driver_attach: PASS" : "[TEST] driver_attach: FAIL");
        terminal::println(device_driver_result.driver_fallback
            ? "[TEST] driver_fallback: PASS" : "[TEST] driver_fallback: FAIL");
        terminal::println(device_driver_result.driver_failure_cleanup
            ? "[TEST] driver_failure_cleanup: PASS"
            : "[TEST] driver_failure_cleanup: FAIL");
        if (device_driver_status != KStatus::Ok ||
            !device_driver_result.complete()) {
            boot_failure("DEVICE", "Device/Driver runtime qualification failed");
        }
        initialize_storage_probe();
"""
replace_once(main, framework_anchor, qualification)

init = Path("userspace/system/init/main.c")
init_anchor = '    (void)u_puts("[TEST] userspace_init_pid1: PASS\\n");\n'
ring3_launch = init_anchor + """

    int32_t device_probe_exit = -1;
    if (!u_spawn_wait("/system/devprobe", &device_probe_exit) ||
        device_probe_exit != 0) {
        (void)u_puts("[TEST] device_ring3_probe_exit: FAIL\\n");
        ku_exit(70);
    }
    (void)u_puts("[TEST] device_ring3_probe_exit: PASS\\n");
"""
replace_once(init, init_anchor, ring3_launch)

print("[qualification] Device Model + Driver Manager runtime injection applied")
