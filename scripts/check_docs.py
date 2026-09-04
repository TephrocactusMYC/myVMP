#!/usr/bin/env python3
"""Validate local Markdown links and the English/Chinese documentation mirror."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
ROOT_PAIRS = ["README", "CONTRIBUTING", "SECURITY"]


def local_target(source: pathlib.Path, raw: str) -> pathlib.Path | None:
    target = raw.strip().split("#", 1)[0]
    if not target or "://" in target or target.startswith("mailto:"):
        return None
    return (source.parent / target).resolve()


def main() -> int:
    errors: list[str] = []
    root_markdown = [ROOT / "CHANGELOG.md"]
    for stem in ROOT_PAIRS:
        english = ROOT / f"{stem}.md"
        chinese = ROOT / f"{stem}.zh-CN.md"
        if not english.is_file() or not chinese.is_file():
            errors.append(f"missing bilingual root document pair: {stem}")
        root_markdown.extend((english, chinese))
    markdown = [*root_markdown, *sorted(DOCS.rglob("*.md"))]
    for source in markdown:
        for match in LINK.finditer(source.read_text(encoding="utf-8")):
            target = local_target(source, match.group(1))
            if target is not None and not target.exists():
                errors.append(f"{source.relative_to(ROOT)}: missing {match.group(1)}")

    english = sorted(path.name for path in DOCS.glob("*.md"))
    chinese = sorted(path.name for path in (DOCS / "zh-CN").glob("*.md"))
    if english != chinese:
        errors.append(f"documentation mirrors differ: English={english}, Chinese={chinese}")

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?", version):
        errors.append(f"VERSION is not semantic: {version!r}")

    if errors:
        print("documentation check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print(f"documentation check passed: {len(markdown)} Markdown files, "
          f"{len(english) + len(ROOT_PAIRS)} bilingual pairs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
