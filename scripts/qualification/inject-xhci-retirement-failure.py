#!/usr/bin/env python3
"""Inject an unacknowledged live companion retirement; verify real DMA halt."""
from pathlib import Path

RETIRE = """bool retire_companion_slot(Controller& controller) {
    auto& companion = controller.companion;
    if (!disable_slot(controller, companion.slot_id)) {"""
POLL = "size_t poll(size_t budget) {"
MARKER = "xhci_companion_retirement_containment:"
PROBE = """
    // Qualification-only fault. Keep the live slot published, then exercise
    // the production containment path against the real QEMU controller.
    if (g_controller.initialized &&
        g_controller.hid_lifecycle == HidLifecycle::Active &&
        g_controller.companion.hid_lifecycle == HidLifecycle::Active &&
        g_controller.companion.slot_id != 0U) {
        auto& c = g_controller;
        const uint8_t slot = c.companion.slot_id;
        const uint64_t page = c.companion.data_page.physical_address;
        const auto* dcbaa = static_cast<const uint64_t*>(c.dcbaa_page.virtual_address);
        const uint64_t context = dcbaa[slot];
        const bool live = c.dma_published && c.bus_master_enabled &&
            (read32(c.operational, OP_USBCMD) & CMD_RUN) != 0U &&
            c.companion.data_page.allocated && context != 0U;
        const bool retired = retire_companion_slot(c);
        const uint16_t command = pci::read16(c.pci_device, 0x04U);
        const uint32_t status = read32(c.operational, OP_USBSTS);
        const bool contained = live && !retired && !c.initialized &&
            c.cleanup_pending && !c.dma_published && !c.bus_master_enabled &&
            c.runtime_status == Status::CommandFailed &&
            command != UINT16_MAX && (command & 4U) == 0U &&
            status != UINT32_MAX && (status & STS_HALTED) != 0U &&
            (read32(c.operational, OP_USBCMD) & CMD_RUN) == 0U &&
            c.companion.slot_id == slot && dcbaa[slot] == context &&
            c.companion.data_page.allocated &&
            c.companion.data_page.physical_address == page &&
            c.hid_lifecycle == HidLifecycle::Failed &&
            c.companion.hid_lifecycle == HidLifecycle::Failed;
        terminal::println(contained
            ? "[TEST] xhci_companion_retirement_containment: PASS"
            : "[TEST] xhci_companion_retirement_containment: FAIL");
    }
"""


def inject(source):
    if MARKER in source or source.count(RETIRE) != 1 or source.count(POLL) != 1:
        raise ValueError("Companion retirement qualifier anchors changed/already injected")
    fault = RETIRE.replace("if (!disable_slot(controller, companion.slot_id)) {",
        "if ((controller.initialized && companion.slot_id != 0U) ||\n"
        "        !disable_slot(controller, companion.slot_id)) {")
    return source.replace(RETIRE, fault, 1).replace(POLL, POLL + PROBE, 1)


if __name__ == "__main__":
    path = Path("kernel/drivers/usb/xhci.cpp")
    path.write_text(inject(path.read_text()))
