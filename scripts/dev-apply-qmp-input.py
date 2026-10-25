#!/usr/bin/env python3
"""Replace fire-and-forget HMP input injection with acknowledged QMP send-key."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    path = "scripts/smoke-uefi-iso-qemu.sh"
    replace_once(
        path,
        'qemu_log="$tmp/qemu.log"\nmonitor="$tmp/qemu-monitor.sock"\npid=""\n',
        'qemu_log="$tmp/qemu.log"\n'
        'monitor="$tmp/qemu-monitor.sock"\n'
        'qmp="$tmp/qemu-qmp.sock"\n'
        'pid=""\n',
    )

    old = '''send_qemu_key() {
    local key="$1"
    python3 - "$monitor" "$key" <<'PY'
import socket
import sys
import time

path, key = sys.argv[1], sys.argv[2]
last_error = None
for _ in range(40):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.connect(path)
        client.sendall((f"sendkey {key}\\n").encode("ascii"))
        client.close()
        raise SystemExit(0)
    except OSError as error:
        last_error = error
        client.close()
        time.sleep(0.05)
raise SystemExit(f"cannot send QEMU key through monitor: {last_error}")
PY
}
'''
    new = '''send_qemu_key() {
    local key="$1"
    python3 - "$qmp" "$key" <<'PY'
import json
import socket
import sys
import time

path, key = sys.argv[1], sys.argv[2]
last_error = None


def receive_object(stream, operation):
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError(f"QMP disconnected during {operation}")
        message = json.loads(line.decode("utf-8"))
        if "event" in message:
            continue
        if "error" in message:
            error = message["error"]
            raise RuntimeError(
                f"QMP {operation} failed: {error.get('class', 'Error')}: "
                f"{error.get('desc', error)}")
        if "return" in message:
            return message["return"]


for _ in range(40):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.settimeout(2.0)
        client.connect(path)
        stream = client.makefile("rwb", buffering=0)
        greeting_line = stream.readline()
        if not greeting_line:
            raise RuntimeError("QMP greeting missing")
        greeting = json.loads(greeting_line.decode("utf-8"))
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")

        stream.write(json.dumps({"execute": "qmp_capabilities"}).encode("utf-8") + b"\\r\\n")
        receive_object(stream, "qmp_capabilities")
        stream.write(json.dumps({
            "execute": "send-key",
            "arguments": {
                "keys": [{"type": "qcode", "data": key}],
                "hold-time": 80,
            },
        }).encode("utf-8") + b"\\r\\n")
        receive_object(stream, f"send-key {key}")
        stream.close()
        client.close()
        print(f"[qmp] acknowledged send-key {key}")
        raise SystemExit(0)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        last_error = error
        try:
            client.close()
        except OSError:
            pass
        time.sleep(0.05)
raise SystemExit(f"cannot send QEMU key through QMP: {last_error}")
PY
}
'''
    replace_once(path, old, new)

    replace_once(
        path,
        '    -monitor "unix:$monitor,server,nowait" \\\n    -display none \\\n',
        '    -monitor "unix:$monitor,server,nowait" \\\n'
        '    -qmp "unix:$qmp,server=on,wait=off" \\\n'
        '    -display none \\\n',
    )

    print("[dev-apply-qmp-input] QEMU input now requires acknowledged QMP send-key")


if __name__ == "__main__":
    main()
