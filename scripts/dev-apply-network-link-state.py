#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, got {count}: {old[:120]!r}")
    file.write_text(text.replace(old, new, 1))


replace_once(
    "kernel/net/e1000.hpp",
    "Status initialize();\nbool ready();\nNetworkInterface* interface();\n",
    "Status initialize();\nbool ready();\nbool link_up();\nNetworkInterface* interface();\n",
)

replace_once(
    "kernel/net/e1000.cpp",
    "bool ready() {\n    return g_device.initialized && g_status == Status::Ok;\n}\n\nNetworkInterface* interface() {\n",
    "bool ready() {\n    return g_device.initialized && g_status == Status::Ok;\n}\n\nbool link_up() {\n    return ready() && (read_register(g_device, REG_STATUS) & STATUS_LU) != 0U;\n}\n\nNetworkInterface* interface() {\n",
)

replace_once(
    "kernel/net/physical.hpp",
    "Status initialize();\nbool ready();\nbool detected();\n",
    "Status initialize();\nbool ready();\nbool link_up();\nbool detected();\n",
)

replace_once(
    "kernel/net/physical.cpp",
    "bool ready() {\n    switch (g_driver) {\n        case Driver::VirtioNet: return virtio_net::interface() != nullptr;\n        case Driver::E1000: return e1000::interface() != nullptr;\n        case Driver::Pcnet: return pcnet::interface() != nullptr;\n        case Driver::None: return false;\n    }\n    return false;\n}\n\nbool detected() { return g_detected; }\n",
    "bool ready() {\n    switch (g_driver) {\n        case Driver::VirtioNet: return virtio_net::interface() != nullptr;\n        case Driver::E1000: return e1000::interface() != nullptr;\n        case Driver::Pcnet: return pcnet::interface() != nullptr;\n        case Driver::None: return false;\n    }\n    return false;\n}\n\nbool link_up() {\n    if (!ready()) return false;\n    switch (g_driver) {\n        case Driver::E1000: return e1000::link_up();\n        case Driver::VirtioNet:\n        case Driver::Pcnet: return true;\n        case Driver::None: return false;\n    }\n    return false;\n}\n\nbool detected() { return g_detected; }\n",
)

replace_once(
    "kernel/net/service.cpp",
    "bool ready() { return g_ready; }\n",
    "bool ready() {\n    if (!g_ready) return false;\n    return !g_physical || physical::link_up();\n}\n",
)

replace_once(
    "kernel/net/service.cpp",
    "bool physical_interface() { return g_ready && g_physical; }\n",
    "bool physical_interface() {\n    return g_ready && g_physical && physical::link_up();\n}\n",
)

replace_once(
    "sdk/include/kurogane/network_events.h",
    "/* Edge-triggered state-change topics emitted by /system/neteventd.\n",
    "/* Edge-triggered state-change topics emitted by /system/netevtd.\n",
)

print("live network link-state contract applied")
