#!/usr/bin/env python3
"""Real QMP hotplug actions. Never creates or modifies guest USB reports."""

import argparse
import json
import socket
import time


def run(path: str, action: str) -> None:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5.0)
        client.connect(path)
        with client.makefile("rwb", buffering=0) as stream:
            if "QMP" not in json.loads(stream.readline()):
                raise RuntimeError("missing QMP greeting")
            deleted = False

            def receive():
                nonlocal deleted
                line = stream.readline()
                if not line:
                    raise RuntimeError("QMP disconnected")
                message = json.loads(line)
                if "error" in message:
                    raise RuntimeError(f"QMP failure: {message['error']}")
                if message.get("event") == "DEVICE_UNPLUG_GUEST_ERROR":
                    raise RuntimeError("QEMU rejected device removal")
                if (message.get("event") == "DEVICE_DELETED" and
                        message.get("data", {}).get("device") == "kurogane_usb_keyboard"):
                    deleted = True
                return message

            def request(command, arguments=None):
                packet = {"execute": command}
                if arguments is not None:
                    packet["arguments"] = arguments
                stream.write(json.dumps(packet).encode() + b"\r\n")
                deadline = time.monotonic() + 5.0
                for _ in range(128):
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    client.settimeout(remaining)
                    if "return" in receive():
                        return
                raise RuntimeError(f"QMP deadline exceeded: {command}")

            request("qmp_capabilities")
            if action in ("hold-shift", "add"):
                request("input-send-event", {"events": [{"type": "key", "data": {
                    "down": action == "hold-shift", "key": {"type": "qcode", "data": "shift"}
                }}]})
            if action == "remove":
                request("device_del", {"id": "kurogane_usb_keyboard"})
                deadline = time.monotonic() + 5.0
                for _ in range(128):
                    if deleted:
                        break
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    client.settimeout(remaining)
                    receive()
                if not deleted:
                    raise RuntimeError("DEVICE_DELETED acknowledgement missing")
            elif action == "add":
                request("device_add", {"driver": "usb-kbd", "bus": "kurogane_xhci.0",
                                       "id": "kurogane_usb_keyboard"})
    print(f"[qmp] USB {action}: acknowledged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("socket_path")
    parser.add_argument("action", choices=("hold-shift", "remove", "add"))
    args = parser.parse_args()
    run(args.socket_path, args.action)
