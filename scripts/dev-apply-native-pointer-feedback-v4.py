#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-native-pointer-feedback-v3.py")
source = source_path.read_text(encoding="utf-8")
compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "kernel/ui/window_manager.cpp"
text = path.read_text(encoding="utf-8")
old = '''    size_t position = 0U;
    while (position < g_count && g_order[position] != slot_index) ++position;
    if (position == g_count) return Status::NotFound;
    for (size_t index = position + 1U; index < g_count; ++index) {
'''
new = '''    size_t position = 0U;
    while (position < g_count && g_order[position] != slot_index) ++position;
    if (position == g_count) return Status::NotFound;
    // Clicking a widget in the already-focused topmost window does not change
    // z-order or chrome. Keep this path idempotent so transient pressed/hover
    // feedback can remain a bounded window-region repaint instead of forcing
    // a full desktop composition.
    if (g_focused == id && position + 1U == g_count) return Status::Ok;
    for (size_t index = position + 1U; index < g_count; ++index) {
'''
if text.count(old) != 1:
    raise SystemExit(
        f"native pointer feedback v4: focus anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
