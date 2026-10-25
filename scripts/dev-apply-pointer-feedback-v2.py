#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-pointer-feedback.py")
source = source_path.read_text(encoding="utf-8")

old = '''text = replace_once(
    text,
    "                        focused ? kRedMuted : kTheme.border);\\n",
    "                        focused || hovered ? kRedMuted : kTheme.border);\\n",
    "dock task hover border")'''
new = '''text = replace_once(
    text,
    "    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,\\n"
    "                        focused ? kRedMuted : kTheme.border);\\n"
    "    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);\\n",
    "    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,\\n"
    "                        focused || hovered ? kRedMuted : kTheme.border);\\n"
    "    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);\\n",
    "dock task hover border")'''

if source.count(old) != 1:
    raise SystemExit(f"Pointer feedback v2: dock-task patcher anchor count={source.count(old)}")
source = source.replace(old, new, 1)

compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
