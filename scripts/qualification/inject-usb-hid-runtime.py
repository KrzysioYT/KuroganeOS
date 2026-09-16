#!/usr/bin/env python3
"""Instrument actual xHCI HID delivery; no fabricated reports or USB backend."""

from pathlib import Path


ANCHOR = "                static_cast<void>(input::submit_key(events[index]));"
MARKER = "[TEST] usb_hid_f12_press:"
INSTRUMENTATION = """                // Qualification only: require successful publication to the
                // production input queue and an ordered hardware press/release.
                if (!input::submit_key(events[index])) {
                    terminal::println("[TEST] usb_hid_f12_press: FAIL");
                    continue;
                }
                static bool qualification_f12_down = false;
                if (events[index].key == keyboard::KeyCode::F12) {
                    if (events[index].pressed) {
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
    return source.replace(ANCHOR, INSTRUMENTATION, 1)


if __name__ == "__main__":
    path = Path("kernel/drivers/usb/xhci.cpp")
    updated = inject(path.read_text(encoding="utf-8"))
    path.write_text(updated, encoding="utf-8")
    print("Instrumented real xHCI HID input-queue publication")
