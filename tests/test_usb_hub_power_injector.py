#!/usr/bin/env python3
"""Keep the CI power fixture compatible with the production hub probe."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "hub_power_injector", root / "scripts/qualification/inject-usb-hub-power.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = (root / "kernel/drivers/usb/xhci.cpp").read_text()
injected = module.inject(source)
marker = "USB2 hub port power off verified: "
assert injected.count(marker) == 1
assert injected.index(marker) < injected.index("USB2 hub port status unverified;")
assert source == injected.replace(injected[injected.index(
    "        // Qualification-only fixture."):injected.index(
    "        hub::PortStatus status{};", injected.index(marker))], "", 1)
for invalid in (injected, "// missing production probe"):
    try:
        module.inject(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted duplicate injection or missing hub probe")
print("USB2 hub power qualifier matches production probe and rejects reinjection: PASS")
