#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-flux-status-strip.py")
source = source_path.read_text(encoding="utf-8")

old = '''text = replace_once(
    text,
    "    g_damage_count = 0U;\\n    g_screen_width = static_cast<int32_t>(screen_width);\\n",
    "    g_damage_count = 0U;\\n"
    "    g_system_status = {};\\n"
    "    g_last_status_tick = 0U;\\n"
    "    g_status_sampled = false;\\n"
    "    g_screen_width = static_cast<int32_t>(screen_width);\\n",
    "initialize status strip state")'''

new = '''text = replace_once(
    text,
    "    g_count = 0U;\\n    g_screen_width = static_cast<int32_t>(screen_width);\\n",
    "    g_count = 0U;\\n"
    "    g_system_status = {};\\n"
    "    g_last_status_tick = 0U;\\n"
    "    g_status_sampled = false;\\n"
    "    g_screen_width = static_cast<int32_t>(screen_width);\\n",
    "initialize status strip state")'''

if source.count(old) != 1:
    raise SystemExit(
        f"Flux status strip v2: initialize patcher anchor count={source.count(old)}")
source = source.replace(old, new, 1)
compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
