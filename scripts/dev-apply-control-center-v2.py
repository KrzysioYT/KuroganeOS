#!/usr/bin/env python3
from pathlib import Path

source_path = Path(__file__).with_name("dev-apply-control-center.py")
source = source_path.read_text(encoding="utf-8")
old = "assert '\"CONTROL CENTER\", \\\'v\\\'' in window_manager"
new = "assert \"\\\\\\\"CONTROL CENTER\\\\\\\", 'v'\" in window_manager"
if source.count(old) != 1:
    raise SystemExit(f"Control Center v2: quoting anchor count={source.count(old)}")
source = source.replace(old, new, 1)
compiled = compile(source, str(source_path), "exec")
exec(compiled, {"__name__": "__main__", "__file__": str(source_path)})
