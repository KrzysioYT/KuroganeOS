#!/usr/bin/env python3
"""Instrument actual xHCI HID delivery; no fabricated reports or USB backend."""

from pathlib import Path


ANCHOR = "        record_keyboard_input(controller, event);"
MARKER = "[TEST] usb_hid_f12_press:"
STATE_ANCHOR = "Controller g_controller{};"
STATE_INSTRUMENTATION = STATE_ANCHOR + """
device::DeviceHandle qualification_keyboard_handle = device::INVALID_DEVICE_HANDLE;
bool qualification_shift_down = false;
bool qualification_shift_released = false;
bool qualification_disconnected = false;
"""
ATTACH_ANCHOR = "    controller.keyboard_device = id;\n    return true;"
ATTACH_INSTRUMENTATION = """    controller.keyboard_device = id;
    const auto new_handle = device::handle_for(id);
    if (qualification_disconnected) {
        const bool fresh = new_handle != qualification_keyboard_handle &&
            device::resolve(qualification_keyboard_handle) == nullptr &&
            device::resolve(new_handle) != nullptr;
        terminal::println(fresh ? "[TEST] usb_hid_reconnect: PASS"
                                : "[TEST] usb_hid_reconnect: FAIL");
    }
    qualification_keyboard_handle = new_handle;
    return true;"""
DETACH_ANCHOR = '        log::write(log::Level::Info, "USB", "keyboard disconnected; slot retired");'
DETACH_INSTRUMENTATION = DETACH_ANCHOR + """
        bool released = qualification_shift_released && !qualification_shift_down &&
            controller.pending_key_count == 0U && controller.slot_id == 0U &&
            !controller.report_queued && controller.report_trb == 0U &&
            controller.keyboard_decoder.previous_modifiers == 0U &&
            controller.keyboard_device == device::INVALID_DEVICE_ID &&
            device::resolve(qualification_keyboard_handle) == nullptr;
        for (uint8_t key : controller.keyboard_decoder.previous_keys) {
            if (key != 0U) released = false;
        }
        qualification_disconnected = released;
        terminal::println(released ? "[TEST] usb_hid_disconnect: PASS"
                                   : "[TEST] usb_hid_disconnect: FAIL");"""
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
                if (event.key == keyboard::KeyCode::LeftShift) {
                    if (event.pressed) {
                        terminal::println(qualification_shift_down
                            ? "[TEST] usb_hid_shift_press: FAIL"
                            : "[TEST] usb_hid_shift_press: PASS");
                        qualification_shift_down = true;
                        qualification_shift_released = false;
                    } else {
                        if (qualification_shift_down && controller.keyboard_lifecycle ==
                                KeyboardLifecycle::ReleaseKeys) {
                            qualification_shift_released = true;
                            terminal::println("[TEST] usb_hid_disconnect_release: PASS");
                        }
                        qualification_shift_down = false;
                    }
                }
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
    replacements = [(ANCHOR, INSTRUMENTATION), (EMPTY_ANCHOR, EMPTY_INSTRUMENTATION),
                    (STATE_ANCHOR, STATE_INSTRUMENTATION),
                    (ATTACH_ANCHOR, ATTACH_INSTRUMENTATION),
                    (DETACH_ANCHOR, DETACH_INSTRUMENTATION)]
    for anchor, replacement in replacements:
        if source.count(anchor) != 1:
            raise ValueError(f"USB qualifier anchor must occur exactly once: {anchor}")
        source = source.replace(anchor, replacement, 1)
    return source


if __name__ == "__main__":
    path = Path("kernel/drivers/usb/xhci.cpp")
    updated = inject(path.read_text(encoding="utf-8"))
    path.write_text(updated, encoding="utf-8")
    print("Instrumented real xHCI HID input-queue publication")
