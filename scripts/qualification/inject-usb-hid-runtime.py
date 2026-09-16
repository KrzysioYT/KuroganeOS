#!/usr/bin/env python3
"""Instrument actual xHCI HID delivery; no fabricated reports or USB backend."""

from pathlib import Path


ANCHOR = "        record_keyboard_input(controller, event);"
MARKER = "[TEST] usb_hid_f12_press:"
EMPTY_ANCHOR = "    return cleanup == Status::Ok ? failure : cleanup;"
EMPTY_INSTRUMENTATION = """    if (failure == Status::NoDevice) {
        const bool released = cleanup == Status::Ok &&
            !g_controller.cleanup_pending && !g_controller.dma_published &&
            !g_controller.bus_master_enabled && !g_controller.initialized &&
            g_controller.mapped_pages == 0U &&
            g_controller.keyboard_device == device::INVALID_DEVICE_ID;
        terminal::println(released
            ? "[TEST] xhci_empty_cleanup: PASS"
            : "[TEST] xhci_empty_cleanup: FAIL");
    }
    return cleanup == Status::Ok ? failure : cleanup;"""
INSTRUMENTATION = """        record_keyboard_input(controller, event);
                // Reached only after successful production input publication.
                static bool qualification_f12_down = false;
                static bool qualification_ring_wrap = false;
                if (!qualification_ring_wrap &&
                    controller.reports > RING_TRB_COUNT &&
                    !controller.interrupt_ring.cycle &&
                    !controller.event_cycle) {
                    qualification_ring_wrap = true;
                    terminal::println("[TEST] usb_hid_ring_wrap: PASS");
                }
                if (event.key == keyboard::KeyCode::F12) {
                    if (event.pressed) {
                        terminal::println(qualification_f12_down
                            ? "[TEST] usb_hid_f12_press: FAIL"
                            : "[TEST] usb_hid_f12_press: PASS");
                        qualification_f12_down = true;
                    } else {
                        terminal::println(qualification_f12_down
                            ? "[TEST] usb_hid_f12_release: PASS"
                            : "[TEST] usb_hid_f12_release: FAIL");
                        qualification_f12_down = false;
                    }
                }"""


def inject(source: str) -> str:
    if MARKER in source:
        raise ValueError("USB HID qualifier already injected")
    if source.count(ANCHOR) != 1:
        raise ValueError("USB HID input-queue anchor must occur exactly once")
    if source.count(EMPTY_ANCHOR) != 1:
        raise ValueError("xHCI cleanup result anchor must occur exactly once")
    return source.replace(ANCHOR, INSTRUMENTATION, 1).replace(
        EMPTY_ANCHOR, EMPTY_INSTRUMENTATION, 1
    )


if __name__ == "__main__":
    path = Path("kernel/drivers/usb/xhci.cpp")
    updated = inject(path.read_text(encoding="utf-8"))
    path.write_text(updated, encoding="utf-8")
    print("Instrumented real xHCI HID input-queue publication")
