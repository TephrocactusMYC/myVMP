#!/usr/bin/env python3
"""Enforce the repository's tool-independent source hygiene rules."""

from __future__ import annotations

import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE_ROOTS = ("include", "src", "tools", "tests", "runtime", "scripts", "fixtures")
TEXT_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".S", ".py", ".sh"}


def tracked_sources() -> list[pathlib.Path]:
    output = subprocess.check_output(
        ["git", "ls-files", "--", *SOURCE_ROOTS], cwd=ROOT, text=True
    )
    return [ROOT / name for name in output.splitlines() if pathlib.Path(name).suffix in TEXT_SUFFIXES]


def main() -> int:
    errors: list[str] = []
    sources = tracked_sources()
    for path in sources:
        raw = path.read_bytes()
        relative = path.relative_to(ROOT)
        if b"\r" in raw:
            errors.append(f"{relative}: CR/CRLF line endings are forbidden")
        if raw and not raw.endswith(b"\n"):
            errors.append(f"{relative}: missing final newline")
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            errors.append(f"{relative}: not UTF-8 ({error})")
            continue
        for number, line in enumerate(text.splitlines(), 1):
            if line.rstrip(" \t") != line:
                errors.append(f"{relative}:{number}: trailing whitespace")
            if "\t" in line:
                errors.append(f"{relative}:{number}: tab indentation is forbidden")
        if path.suffix == ".hpp" and not text.startswith("#pragma once\n"):
            errors.append(f"{relative}: public C++ headers must start with #pragma once")

    if errors:
        print("style check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print(f"style check passed: {len(sources)} source files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
