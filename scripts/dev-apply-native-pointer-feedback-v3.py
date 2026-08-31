#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
test_path = ROOT / "tests/test_window_manager.cpp"
text = test_path.read_text(encoding="utf-8")

old = '''    if (create_window("RED FLUX HOME", 17U, {100, 100, 360, 260},
                      draw, receive, nullptr, &home) != Status::Ok ||
        query(home, &info) != Status::Ok ||
        info.state != WindowState::Minimized) return 57;
    uint8_t home_payload[16]{};
    home_payload[0] = UINT8_C(0x5a);
    if (present_surface(home, 4U, 4U, 4U, home_payload, sizeof(home_payload)) !=
            Status::Ok ||
        restore(home) != Status::Ok || query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal) return 58;
'''
new = '''    if (create_window("RED FLUX HOME", 17U, {100, 100, 360, 260},
                      draw, receive, nullptr, &home) != Status::Ok ||
        query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal || focused_window() != home) return 57;
    uint8_t home_payload[16]{};
    home_payload[0] = UINT8_C(0x5a);
    if (present_surface(home, 4U, 4U, 4U, home_payload, sizeof(home_payload)) !=
            Status::Ok || query(home, &info) != Status::Ok ||
        info.state != WindowState::Normal) return 58;
'''
if text.count(old) != 1:
    raise SystemExit(
        f"native pointer feedback v3: HOME lifecycle anchor count={text.count(old)}")
test_path.write_text(text.replace(old, new, 1), encoding="utf-8")

source_path = Path(__file__).with_name("dev-apply-native-pointer-feedback-v2.py")
source = source_path.read_text(encoding="utf-8")
compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
