#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-native-pointer-feedback.py")
source = source_path.read_text(encoding="utf-8")

# update_pointer_feedback() is deliberately near capture lifecycle code, while
# hit_test() lives later beside the rest of window geometry helpers. Give the
# internal helper an exact forward declaration rather than moving either block
# and disturbing the established guarded anchors.
needle = '    "void cancel_capture(WindowId id) {\\n"\n    "    if (g_dragged == id) g_dragged = INVALID_WINDOW;\\n"'
replacement = (
    '    "WindowId hit_test(int32_t x, int32_t y);\\n\\n"\n'
    '    "void cancel_capture(WindowId id) {\\n"\n'
    '    "    if (g_dragged == id) g_dragged = INVALID_WINDOW;\\n"'
)
if source.count(needle) != 1:
    raise SystemExit(
        f"native pointer feedback v2: helper anchor count={source.count(needle)}")
source = source.replace(needle, replacement, 1)

compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
