#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-flux-deck.py")
source = source_path.read_text(encoding="utf-8")

old = '''    "enum class DockIcon : uint8_t {\\n    Home,\\n    Terminal,\\n    Files,\\n    Monitor,\\n    Settings,\\n    About,\\n};\\n",'''
new = '''    "enum class DockIcon : uint8_t {\\n    Home = 0,\\n    Terminal,\\n    Files,\\n    Monitor,\\n    Settings,\\n    About,\\n};\\n",'''

if source.count(old) != 1:
    raise SystemExit(f"Flux Deck v2: DockIcon patcher anchor count={source.count(old)}")
source = source.replace(old, new, 1)

compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
