#!/usr/bin/env python3
"""Host protocol tests, separate from the mandatory real USB runtime gate."""
import importlib.util
import json
from pathlib import Path
import socket
import tempfile
import threading

spec = importlib.util.spec_from_file_location(
    "usb_qmp", Path(__file__).resolve().parents[1] / "scripts/qualification/usb-hotplug-qmp.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def check(action, order="before", fail=False):
    errors = []
    with tempfile.TemporaryDirectory() as directory:
        path = str(Path(directory) / "qmp.sock")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(path)
        server.listen(1)
        server.settimeout(3)

        def serve():
            try:
                client, _ = server.accept()
                with client, client.makefile("rwb", buffering=0) as stream:
                    def send(obj):
                        stream.write(json.dumps(obj).encode() + b"\n")

                    def read():
                        return json.loads(stream.readline())

                    send({"QMP": {}})
                    assert read()["execute"] == "qmp_capabilities"
                    send({"return": {}})
                    command = read()
                    if action in ("add", "hold-shift"):
                        assert command == {"execute": "input-send-event", "arguments": {
                            "events": [{"type": "key", "data": {"down": action == "hold-shift",
                                "key": {"type": "qcode", "data": "shift"}}}]}}
                        send({"return": {}})
                        if action == "hold-shift":
                            return
                        command = read()
                        assert command["execute"] == "device_add"
                        assert command["arguments"] == {"driver": "usb-kbd",
                            "bus": "kurogane_xhci.0", "id": "kurogane_usb_keyboard"}
                        send({"return": {}})
                        return
                    assert command == {"execute": "device_del", "arguments": {
                        "id": "kurogane_usb_keyboard"}}
                    if order == "error":
                        send({"error": {"class": "DeviceNotFound", "desc": "missing"}})
                        return
                    event = {"event": "DEVICE_DELETED", "data": {
                        "device": "other" if order == "wrong" else "kurogane_usb_keyboard"}}
                    if order == "before":
                        send(event)
                    send({"return": {}})
                    if order in ("after", "wrong"):
                        send(event)
            except BaseException as error:
                errors.append(error)

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            module.run(path, action)
            assert not fail, "accepted missing/error removal acknowledgement"
        except RuntimeError:
            assert fail, "rejected valid QMP operation"
        finally:
            thread.join(4)
            server.close()
        assert not thread.is_alive() and not errors, errors


check("hold-shift")
check("add")
check("remove", "before")
check("remove", "after")
check("remove", "missing", True)
check("remove", "wrong", True)
check("remove", "error", True)
print("USB QMP bounded handshake and removal acknowledgement: PASS (7 cases)")
