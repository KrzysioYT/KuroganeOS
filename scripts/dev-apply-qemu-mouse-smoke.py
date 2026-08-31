#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "scripts/smoke-uefi-iso-qemu.sh"
text = PATH.read_text(encoding="utf-8")


def replace_once(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    text = text.replace(old, new, 1)

replace_once(
    'echo "usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--persistent-disk] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--require-network] [--require-tls] [--send-key-after-marker TEXT KEY] [--send-key-after-marker-count TEXT COUNT KEY ...] [--require-marker TEXT ...] [--require-marker-count TEXT COUNT ...]" >&2',
    'echo "usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--persistent-disk] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--require-network] [--require-tls] [--send-key-after-marker TEXT KEY] [--send-key-after-marker-count TEXT COUNT KEY ...] [--click-after-marker TEXT X Y ...] [--require-marker TEXT ...] [--require-marker-count TEXT COUNT ...]" >&2',
    "usage")

replace_once(
    'send_key_count_sent=()\nrequire_markers=()',
    'send_key_count_sent=()\nclick_markers=()\nclick_x=()\nclick_y=()\nclick_sent=()\nrequire_markers=()',
    "mouse arrays")

replace_once(
    '        --require-marker) [[ $# -ge 2 && -n "$2" ]] || usage; require_markers+=("$2"); shift 2 ;;',
    '        --click-after-marker)\n            [[ $# -ge 4 && -n "$2" && "$3" =~ ^[0-9]+$ && "$4" =~ ^[0-9]+$ ]] || usage\n            click_markers+=("$2")\n            click_x+=("$3")\n            click_y+=("$4")\n            click_sent+=(false)\n            shift 4\n            ;;\n        --require-marker) [[ $# -ge 2 && -n "$2" ]] || usage; require_markers+=("$2"); shift 2 ;;',
    "mouse option")

replace_once(
    'for key in "${send_key_count_names[@]}"; do\n    if [[ ! "$key" =~ ^[A-Za-z0-9_-]+$ ]]; then\n        echo "invalid QEMU sendkey name: $key" >&2\n        exit 2\n    fi\ndone',
    'for key in "${send_key_count_names[@]}"; do\n    if [[ ! "$key" =~ ^[A-Za-z0-9_-]+$ ]]; then\n        echo "invalid QEMU sendkey name: $key" >&2\n        exit 2\n    fi\ndone\nfor index in "${!click_markers[@]}"; do\n    if (( click_x[index] > 4095 || click_y[index] > 4095 )); then\n        echo "invalid QEMU click coordinate: ${click_x[$index]},${click_y[$index]}" >&2\n        exit 2\n    fi\ndone',
    "mouse validation")

marker = 'firmware_code="${OVMF_CODE:-}"\n'
mouse_function = r'''send_qemu_click() {
    local target_x="$1"
    local target_y="$2"
    python3 - "$qmp" "$target_x" "$target_y" <<'PY'
import json
import socket
import sys
import time

path = sys.argv[1]
target_x = int(sys.argv[2])
target_y = int(sys.argv[3])
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


def execute(stream, name, arguments=None):
    payload = {"execute": name}
    if arguments is not None:
        payload["arguments"] = arguments
    stream.write(json.dumps(payload).encode("utf-8") + b"\r\n")
    receive_object(stream, name)


def relative(stream, dx, dy):
    events = []
    if dx:
        events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
    if dy:
        events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
    if events:
        execute(stream, "input-send-event", {"events": events})


def move_axis(stream, axis, amount):
    remaining = amount
    while remaining:
        step = max(-100, min(100, remaining))
        relative(stream, step if axis == "x" else 0, step if axis == "y" else 0)
        remaining -= step


for _ in range(40):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.settimeout(2.0)
        client.connect(path)
        stream = client.makefile("rwb", buffering=0)
        greeting = json.loads(stream.readline().decode("utf-8"))
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")
        execute(stream, "qmp_capabilities")

        # Kurogane's pointer state is clamped. Drive far negative first so the
        # resulting guest coordinate is deterministically (0,0), independent
        # of the firmware/guest cursor history, then walk to the target.
        for _ in range(50):
            relative(stream, -100, -100)
        move_axis(stream, "x", target_x)
        move_axis(stream, "y", target_y)
        execute(stream, "input-send-event", {"events": [
            {"type": "btn", "data": {"down": True, "button": "left"}}
        ]})
        time.sleep(0.08)
        execute(stream, "input-send-event", {"events": [
            {"type": "btn", "data": {"down": False, "button": "left"}}
        ]})
        stream.close()
        client.close()
        print(f"[qmp] clicked guest pointer at {target_x},{target_y}")
        raise SystemExit(0)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        last_error = error
        try:
            client.close()
        except OSError:
            pass
        time.sleep(0.05)
raise SystemExit(f"cannot click through QMP: {last_error}")
PY
}

'''
replace_once(marker, mouse_function + marker, "mouse function insertion")

replace_once(
    '        for index in "${!send_key_count_markers[@]}"; do\n            if [[ "${send_key_count_sent[$index]}" == true ]]; then\n                continue\n            fi\n            marker="${send_key_count_markers[$index]}"\n            target="${send_key_count_targets[$index]}"\n            if (( $(marker_occurrences "$marker") >= target )); then\n                key="${send_key_count_names[$index]}"\n                send_qemu_key "$key"\n                send_key_count_sent[$index]=true\n                echo "[uefi-qemu] sent key \'$key\' after marker occurrence $target: $marker"\n            fi\n        done',
    '        for index in "${!send_key_count_markers[@]}"; do\n            if [[ "${send_key_count_sent[$index]}" == true ]]; then\n                continue\n            fi\n            marker="${send_key_count_markers[$index]}"\n            target="${send_key_count_targets[$index]}"\n            if (( $(marker_occurrences "$marker") >= target )); then\n                key="${send_key_count_names[$index]}"\n                send_qemu_key "$key"\n                send_key_count_sent[$index]=true\n                echo "[uefi-qemu] sent key \'$key\' after marker occurrence $target: $marker"\n            fi\n        done\n        for index in "${!click_markers[@]}"; do\n            if [[ "${click_sent[$index]}" == true ]]; then\n                continue\n            fi\n            marker="${click_markers[$index]}"\n            if grep -Fq "$marker" "$serial"; then\n                send_qemu_click "${click_x[$index]}" "${click_y[$index]}"\n                click_sent[$index]=true\n                echo "[uefi-qemu] clicked ${click_x[$index]},${click_y[$index]} after marker: $marker"\n            fi\n        done',
    "mouse event loop")

PATH.write_text(text, encoding="utf-8")
print("QMP mouse smoke support applied")
