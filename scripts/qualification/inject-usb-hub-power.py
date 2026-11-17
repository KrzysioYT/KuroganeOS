#!/usr/bin/env python3
"""Prepare real switched hub ports for CI power-on qualification."""
from pathlib import Path


def inject(source):
    anchor = """        hub::PortStatus status{};
        if (!exchange(hub::PortRequest::GetStatus, &status)) return true;"""
    setup = """        // Qualification-only fixture. Use real class requests and readback.
        if (controller.initialized || parsed.power_mode != hub::PowerMode::Individual ||
            !transfer(controller, 0x23U, 1U, 8U, port, 0U, false)) return false;
        hub::PortStatus powered_off{};
        if (!exchange(hub::PortRequest::GetStatus, &powered_off) ||
            (powered_off.status & (hub::PORT_POWERED | hub::PORT_ENABLED)) != 0U) {
            return false;
        }
        log::write_u64(log::Level::Info, "XHCI",
            "USB2 hub port power off verified: ", port);
"""
    if source.count(anchor) != 1 or "hub port power off verified" in source:
        raise ValueError("Hub power qualifier anchor changed/already injected")
    return source.replace(anchor, setup + anchor, 1)


if __name__ == "__main__":
    path = Path("kernel/drivers/usb/xhci.cpp")
    path.write_text(inject(path.read_text()))
