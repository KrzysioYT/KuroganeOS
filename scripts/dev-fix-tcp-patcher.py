#!/usr/bin/env python3
from pathlib import Path

path = Path("scripts/dev-apply-tcp-socket-contract.py")
text = path.read_text()

old = '''# This exact block appears in send and receive; replace both.
p = Path("kernel/net/socket.cpp")
text = p.read_text()
count = text.count(old_send_progress)
if count != 2:
    raise SystemExit(f"kernel/net/socket.cpp: expected two TCP progress blocks, got {count}")
p.write_text(text.replace(old_send_progress, new_send_progress))
'''

new = """# send() and receive() have deliberately different surrounding state checks,
# so qualify each progression path with its own exact anchor.
replace_once("kernel/net/socket.cpp", old_send_progress, new_send_progress)

old_receive_progress = '''        if (!slot->connected) {
            const net::Status progress_status =
                g_backend.tcp_progress(g_backend.context, &session.client);
            slot->connected = session.client.connected;
            if (!slot->connected) {
                const Status mapped = transport_status(progress_status);
                return mapped == Status::Ok ? Status::WouldBlock : mapped;
            }
        }
'''
new_receive_progress = '''        if (!slot->connected) {
            const Status progress_status = progress_tcp_connection(*slot, session);
            if (progress_status != Status::Ok) return progress_status;
        }
'''
replace_once("kernel/net/socket.cpp", old_receive_progress, new_receive_progress)
"""

count = text.count(old)
if count != 1:
    raise SystemExit(f"patcher progression anchor count is {count}, expected 1")
path.write_text(text.replace(old, new, 1))
print("TCP patcher progression anchors corrected")
