#!/usr/bin/env python3
"""Deterministic DNS + HTTPS endpoints for the KuroganeOS TLS runtime gate."""

from __future__ import annotations

import argparse
import http.server
import ipaddress
import socket
import ssl
import struct
import threading
from pathlib import Path


TEST_NAMES = {
    "kurogane.test": "10.0.2.2",
    "wrong.kurogane.test": "10.0.2.2",
    "untrusted.kurogane.test": "10.0.2.2",
}


def parse_question(packet: bytes) -> tuple[str, int, int, int] | None:
    if len(packet) < 12:
        return None
    question_count = struct.unpack_from("!H", packet, 4)[0]
    if question_count != 1:
        return None
    offset = 12
    labels: list[str] = []
    while True:
        if offset >= len(packet):
            return None
        length = packet[offset]
        offset += 1
        if length == 0:
            break
        if length > 63 or offset + length > len(packet):
            return None
        try:
            labels.append(packet[offset : offset + length].decode("ascii"))
        except UnicodeDecodeError:
            return None
        offset += length
    if offset + 4 > len(packet):
        return None
    qtype, qclass = struct.unpack_from("!HH", packet, offset)
    return ".".join(labels).lower(), qtype, qclass, offset + 4


def dns_response(packet: bytes) -> tuple[bytes, str] | None:
    parsed = parse_question(packet)
    if parsed is None:
        return None
    name, qtype, qclass, question_end = parsed
    transaction = packet[:2]
    question = packet[12:question_end]
    if qtype == 1 and qclass == 1 and name in TEST_NAMES:
        header = transaction + struct.pack("!HHHHH", 0x8180, 1, 1, 0, 0)
        answer = (
            b"\xc0\x0c"
            + struct.pack("!HHIH", 1, 1, 30, 4)
            + ipaddress.IPv4Address(TEST_NAMES[name]).packed
        )
        return header + question + answer, name
    header = transaction + struct.pack("!HHHHH", 0x8183, 1, 0, 0, 0)
    return header + question, name


class DnsThread(threading.Thread):
    def __init__(self, log_path: Path) -> None:
        super().__init__(daemon=True)
        self.log_path = log_path
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(("127.0.0.1", 53))

    def run(self) -> None:
        while True:
            packet, address = self.socket.recvfrom(4096)
            response = dns_response(packet)
            if response is None:
                continue
            payload, name = response
            with self.log_path.open("a", encoding="utf-8") as handle:
                handle.write(name + "\n")
            self.socket.sendto(payload, address)


class TlsHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path == "/ok":
            body = b"KURO_TLS_OK\n"
            status = 200
        elif self.path == "/large":
            body = b"K" * 8192
            status = 200
        else:
            body = b"not found\n"
            status = 404
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        del format, args


def make_server(
    trusted_cert: Path,
    trusted_key: Path,
    untrusted_cert: Path,
    untrusted_key: Path,
    sni_log: Path,
) -> http.server.ThreadingHTTPServer:
    trusted = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    trusted.minimum_version = ssl.TLSVersion.TLSv1_2
    trusted.load_cert_chain(str(trusted_cert), str(trusted_key))

    untrusted = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    untrusted.minimum_version = ssl.TLSVersion.TLSv1_2
    untrusted.load_cert_chain(str(untrusted_cert), str(untrusted_key))

    def select_certificate(
        tls_socket: ssl.SSLSocket,
        server_name: str | None,
        initial_context: ssl.SSLContext,
    ) -> None:
        del initial_context
        name = (server_name or "<none>").lower()
        with sni_log.open("a", encoding="utf-8") as handle:
            handle.write(name + "\n")
        if name == "untrusted.kurogane.test":
            tls_socket.context = untrusted

    trusted.set_servername_callback(select_certificate)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 443), TlsHandler)
    server.socket = trusted.wrap_socket(server.socket, server_side=True)
    return server


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trusted-cert", type=Path, required=True)
    parser.add_argument("--trusted-key", type=Path, required=True)
    parser.add_argument("--untrusted-cert", type=Path, required=True)
    parser.add_argument("--untrusted-key", type=Path, required=True)
    parser.add_argument("--sni-log", type=Path, required=True)
    parser.add_argument("--dns-log", type=Path, required=True)
    args = parser.parse_args()

    args.sni_log.write_text("", encoding="utf-8")
    args.dns_log.write_text("", encoding="utf-8")
    dns = DnsThread(args.dns_log)
    dns.start()
    server = make_server(
        args.trusted_cert,
        args.trusted_key,
        args.untrusted_cert,
        args.untrusted_key,
        args.sni_log,
    )
    print("tls-local-services: DNS 127.0.0.1:53 + HTTPS 127.0.0.1:443 ready", flush=True)
    try:
        server.serve_forever(poll_interval=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
