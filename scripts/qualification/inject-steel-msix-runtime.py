#!/usr/bin/env python3
"""Inject the real QEMU e1000e MSI-X runtime qualification."""

from pathlib import Path


main = Path("kernel/main.cpp")
text = main.read_text(encoding="utf-8")
include = '#include "drivers/e1000e_msix_qualification.hpp"\n'
include_anchor = '#include "drivers/pci_edu.hpp"\n'
runtime_marker = "e1000e_msix_qualification::qualify_delivery"
if include in text or runtime_marker in text:
    raise SystemExit(f"{main}: MSI-X qualification already injected")
if text.count(include_anchor) != 1:
    raise SystemExit(
        f"{main}: include anchor count={text.count(include_anchor)}"
    )
text = text.replace(include_anchor, include_anchor + include, 1)

initialize_anchor = """        initialize_device_framework(false);
        const auto pci_msi_qualification = drivers::pci_edu::initialize();
"""
initialize = """        initialize_device_framework(false);
        const auto pci_msix_qualification =
            drivers::e1000e_msix_qualification::initialize();
        if (pci_msix_qualification !=
            drivers::e1000e_msix_qualification::Status::NotFound) {
            log::write(
                pci_msix_qualification ==
                        drivers::e1000e_msix_qualification::Status::Ok
                    ? log::Level::Info
                    : log::Level::Warn,
                "MSIX",
                drivers::e1000e_msix_qualification::status_name(
                    pci_msix_qualification));
        }
        const auto pci_msi_qualification = drivers::pci_edu::initialize();
"""
if text.count(initialize_anchor) != 1:
    raise SystemExit(
        f"{main}: initialization anchor count={text.count(initialize_anchor)}"
    )
text = text.replace(initialize_anchor, initialize, 1)

qualification_anchor = """    if (!run_kernel_preemption_probe()) {
"""
qualification = """    if (drivers::e1000e_msix_qualification::msix_configured()) {
        const bool msix_delivered =
            drivers::e1000e_msix_qualification::qualify_delivery(
                UINT32_C(1000000));
        log::write(
            msix_delivered ? log::Level::Info : log::Level::Warn,
            "MSIX",
            msix_delivered
                ? "e1000e MSI-X reached the Local APIC handler and teardown completed"
                : drivers::e1000e_msix_qualification::status_name(
                    drivers::e1000e_msix_qualification::status()));
        terminal::println(
            msix_delivered
                ? "[TEST] pci_msix_delivery: PASS"
                : "[TEST] pci_msix_delivery: FAIL");
    } else {
        terminal::write("[TEST] pci_msix_delivery: SKIP (");
        terminal::write(
            drivers::e1000e_msix_qualification::status_name(
                drivers::e1000e_msix_qualification::status()));
        terminal::println(")");
    }
    if (!run_kernel_preemption_probe()) {
"""
if text.count(qualification_anchor) != 1:
    raise SystemExit(
        f"{main}: qualification anchor count={text.count(qualification_anchor)}"
    )
text = text.replace(qualification_anchor, qualification, 1)
main.write_text(text, encoding="utf-8")

print("[qualification] Steel e1000e MSI-X runtime injection applied")
