#!/usr/bin/env python3
"""Inject the native 4.0 unified-status Ring-3 qualification launch."""

from pathlib import Path


init = Path("userspace/system/init/main.c")
text = init.read_text(encoding="utf-8")
anchor = '    (void)u_puts("[TEST] userspace_init_pid1: PASS\\n");\n'
launch = anchor + """

    int32_t status_probe_exit = -1;
    if (!u_spawn_wait("/system/stprobe", &status_probe_exit) ||
        status_probe_exit != 0) {
        (void)u_puts("[TEST] unified_status_probe_exit: FAIL\\n");
        ku_exit(71);
    }
    (void)u_puts("[TEST] unified_status_probe_exit: PASS\\n");
"""
if text.count(anchor) != 1:
    raise SystemExit(f"{init}: qualification anchor count={text.count(anchor)}")
init.write_text(text.replace(anchor, launch, 1), encoding="utf-8")

print("[qualification] unified status runtime injection applied")
