#!/usr/bin/env python3
"""Validate the source-controlled KuroganeOS Web PKI trust bundle."""

from __future__ import annotations

import argparse
import base64
import hashlib
import pathlib
import re


PEM_PATTERN = re.compile(
    rb"-----BEGIN CERTIFICATE-----\s+([A-Za-z0-9+/=\r\n]+?)"
    rb"\s+-----END CERTIFICATE-----"
)
MIN_CERTIFICATES = 50
MAX_CERTIFICATES = 256
MAX_BUNDLE_BYTES = 512 * 1024 - 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=pathlib.Path)
    args = parser.parse_args()

    payload = args.bundle.read_bytes()
    if not payload or len(payload) > MAX_BUNDLE_BYTES:
        raise SystemExit(
            f"invalid trust bundle size: {len(payload)} bytes "
            f"(maximum {MAX_BUNDLE_BYTES})"
        )

    matches = list(PEM_PATTERN.finditer(payload))
    if not MIN_CERTIFICATES <= len(matches) <= MAX_CERTIFICATES:
        raise SystemExit(
            f"invalid trust anchor count: {len(matches)} "
            f"(expected {MIN_CERTIFICATES}..{MAX_CERTIFICATES})"
        )

    for index, match in enumerate(matches, start=1):
        encoded = re.sub(rb"\s+", b"", match.group(1))
        try:
            der = base64.b64decode(encoded, validate=True)
        except ValueError as error:
            raise SystemExit(f"certificate {index}: invalid base64: {error}") from error
        if len(der) < 128 or der[0] != 0x30:
            raise SystemExit(f"certificate {index}: invalid DER certificate")

    digest = hashlib.sha256(payload).hexdigest()
    print(
        f"[trust] source-controlled Mozilla bundle: "
        f"{len(matches)} roots, {len(payload)} bytes, sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
