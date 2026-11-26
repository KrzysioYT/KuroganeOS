#!/usr/bin/env python3
"""Validate that the RTL8168 bench probe is opt-in at the intended boot stage."""
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory


root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "rtl8168_physical", root / "scripts/qualification/inject-rtl8168-physical.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def expect_rejected(target):
    try:
        module.inject(target)
    except ValueError:
        return
    raise AssertionError("unsafe RTL8168 probe injection accepted")


with TemporaryDirectory() as directory:
    target = Path(directory)
    physical_path = target / "kernel/net/physical.cpp"
    main_path = target / "kernel/main.cpp"
    physical_path.parent.mkdir(parents=True)
    physical = (root / "kernel/net/physical.cpp").read_text(encoding="utf-8")
    main = (root / "kernel/main.cpp").read_text(encoding="utf-8")
    physical_path.write_text(physical, encoding="utf-8")
    main_path.write_text("missing boot stages", encoding="utf-8")
    expect_rejected(target)
    assert physical_path.read_text(encoding="utf-8") == physical

    main_path.write_text(main, encoding="utf-8")
    module.inject(target)
    probe_main = main_path.read_text(encoding="utf-8")
    probe_physical = physical_path.read_text(encoding="utf-8")
    assert probe_physical == "#define KUROGANE_EXPERIMENTAL_RTL81XX 1\n" + physical
    call = "if (!context.safe_mode) network_status = qualify_rtl8168_rebind(network_status);"
    assert probe_main.count(call) == 1
    assert probe_main.index(call) < probe_main.index("if (scheduler::initialize(0)")
    assert probe_main.index("const auto queued") < probe_main.index(
        "const auto rebound = net::service::initialize()")
    expect_rejected(target)
    assert main_path.read_text(encoding="utf-8") == probe_main
    assert physical_path.read_text(encoding="utf-8") == probe_physical

print("RTL8168 physical qualifier targets pre-scheduler startup and rejects reinjection: PASS")
