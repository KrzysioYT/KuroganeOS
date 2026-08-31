#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-native-ui-packet.py")
source = source_path.read_text(encoding="utf-8")

fixes = {
    'text = replace_between(text, "static void append_text(", "static kui_view* find_view(", "static kui_view* find_view(", "remove text append")':
        'text = replace_between(text, "static void append_text(", "static kui_view* find_view(", "", "remove text append")',
    'text = replace_between(text, "static void render_view_line(", "void kui_frame_initialize(", "void kui_frame_initialize(", "remove text row renderer")':
        'text = replace_between(text, "static void render_view_line(", "void kui_frame_initialize(", "", "remove text row renderer")',
    'text = replace_between(text, "uint32_t kui_scene_hit_test(", "void kui_flow_begin(", new_scene_transport + "void kui_flow_begin(", "replace libui scene transport")':
        'text = replace_between(text, "uint32_t kui_scene_hit_test(", "void kui_flow_begin(", new_scene_transport, "replace libui scene transport")',
    'text = replace_between(text, "void draw_user_window(\\n", "void queue_user_event(", new_draw + "void queue_user_event(", "replace Ring3 UI renderer")':
        'text = replace_between(text, "void draw_user_window(\\n", "void queue_user_event(", new_draw, "replace Ring3 UI renderer")',
    'text = replace_between(text, "        case KU_SYS_UI_PRESENT: {\\n", "        case KU_SYS_UI_POLL: {\\n", new_present + "        case KU_SYS_UI_POLL: {\\n", "replace UI_PRESENT dispatch")':
        'text = replace_between(text, "        case KU_SYS_UI_PRESENT: {\\n", "        case KU_SYS_UI_POLL: {\\n", new_present, "replace UI_PRESENT dispatch")',
}

for old, new in fixes.items():
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"native UI v2 patcher anchor mismatch: {old[:72]!r} count={count}")
    source = source.replace(old, new, 1)

compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
