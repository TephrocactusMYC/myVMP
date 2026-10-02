#!/usr/bin/env python3
"""Check that MkDocs emits Mermaid containers for both documentation languages."""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path


class MermaidContainerParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.count = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag not in ("pre", "div"):
            return
        classes = dict(attrs).get("class") or ""
        if "mermaid" in classes.split():
            self.count += 1


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", type=Path, help="MkDocs output directory")
    args = parser.parse_args()
    for relative in ("architecture/index.html", "zh-CN/architecture/index.html"):
        page = args.site / relative
        content = page.read_text(encoding="utf-8")
        checker = MermaidContainerParser()
        checker.feed(content)
        if checker.count < 2:
            raise SystemExit(f"{page}: expected two Mermaid containers, found {checker.count}")
        print(f"{page}: {checker.count} Mermaid containers")


if __name__ == "__main__":
    main()
