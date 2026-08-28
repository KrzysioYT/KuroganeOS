#!/usr/bin/env python3
"""Fail closed when the deployable portal references missing local assets."""

from __future__ import annotations

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlparse


class References(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.paths: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        attribute = "src" if tag in {"img", "script"} else "href"
        value = values.get(attribute)
        if value:
            self.paths.append(value)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    site = root / "deploy" / "vps" / "site"
    pages = [site / "index.html", site / "docs" / "index.html"]
    failures: list[str] = []
    routed_assets = {
        "/assets/kuroganeos-design-reference.png":
            root / "Forged_Steel_GUI_Reference.png",
    }

    for page in pages:
        source = page.read_text(encoding="utf-8")
        parser = References()
        parser.feed(source)
        for forbidden in ("localhost", "127.0.0.1", "147.79.62.37"):
            if forbidden in source:
                failures.append(f"{page}: deploy source contains {forbidden}")
        for reference in parser.paths:
            parsed = urlparse(reference)
            if parsed.scheme or parsed.netloc or reference.startswith("#"):
                continue
            if not parsed.path.startswith("/assets/"):
                continue
            target = routed_assets.get(
                parsed.path, site / parsed.path.removeprefix("/"))
            if not target.is_file():
                failures.append(f"{page}: missing {parsed.path}")

    expected = {
        site / "assets" / "site.css",
        site / "assets" / "site.js",
        root / "Forged_Steel_GUI_Reference.png",
        site / "assets" / "screenshots" / "secure-access.png",
        site / "assets" / "screenshots" / "blade-launcher.png",
        site / "assets" / "screenshots" / "kurosh.png",
        site / "assets" / "screenshots" / "kurogane-web.png",
        site / "assets" / "screenshots" / "anvil.png",
        site / "assets" / "screenshots" / "performance.png",
    }
    for path in sorted(expected):
        if not path.is_file() or path.stat().st_size == 0:
            failures.append(f"missing required portal asset: {path}")

    if failures:
        print("[site] FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("[site] portal/docs assets: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
