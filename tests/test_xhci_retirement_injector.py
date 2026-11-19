#!/usr/bin/env python3
"""Keep the negative QEMU fixture connected to the production driver."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "retirement_injector", root / "scripts/qualification/inject-xhci-retirement-failure.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = (root / "kernel/drivers/usb/xhci.cpp").read_text()
result = module.inject(source)
assert module.MARKER not in source
assert result.count(module.MARKER) == 2  # Mutually exclusive observed PASS/FAIL.
assert result.count(module.PROBE) == 1
assert result.index(module.PROBE) < result.index(
    "    if (!g_controller.initialized || budget == 0U) return 0U;")
for invalid in (result, source.replace(module.RETIRE, ""), source.replace(module.POLL, "")):
    try:
        module.inject(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted duplicate fixture or missing driver path")
print("xHCI retirement QEMU fault fixture matches production and rejects reinjection: PASS")
