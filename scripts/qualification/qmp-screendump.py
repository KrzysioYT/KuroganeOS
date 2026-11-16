#!/usr/bin/env python3
"""Capture a QEMU display frame through an existing QMP socket."""
import json
import pathlib
import socket
import sys
import time


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
                f"QMP {operation} failed: "
                f"{error.get('class', 'Error')}: {error.get('desc', error)}"
            )
        if "return" in message:
            return message["return"]


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: qmp-screendump.py QMP_SOCKET OUTPUT.ppm")

    qmp_path = sys.argv[1]
    output = pathlib.Path(sys.argv[2]).expanduser().resolve()
    if output.suffix.lower() != ".ppm":
        raise SystemExit("QEMU screendump output must use the .ppm extension")
    if output.exists():
        raise FileExistsError(f"refusing to overwrite screenshot: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.settimeout(2.0)
    deadline = time.monotonic() + 10.0
    while True:
        try:
            connection.connect(qmp_path)
            break
        except OSError as error:
            if time.monotonic() >= deadline:
                raise RuntimeError(f"cannot connect to QMP socket: {error}") from error
            time.sleep(0.05)

    with connection, connection.makefile("rwb", buffering=0) as stream:
        greeting_line = stream.readline()
        if not greeting_line or "QMP" not in json.loads(greeting_line.decode("utf-8")):
            raise RuntimeError("QMP greeting is missing")

        for identifier, name, arguments in (
            (1, "qmp_capabilities", None),
            (2, "screendump", {"filename": str(output)}),
        ):
            request = {"execute": name, "id": identifier}
            if arguments is not None:
                request["arguments"] = arguments
            stream.write(json.dumps(request).encode("utf-8") + b"\r\n")
            receive_object(stream, name)

    if not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError(f"QEMU produced an empty screenshot: {output}")
    print(f"[qmp] screendump saved: {output}")


if __name__ == "__main__":
    main()
