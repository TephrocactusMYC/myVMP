#!/usr/bin/env python3
"""Build two release artifacts in isolated trees and compare their hashes."""

from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMP_ROOT = ROOT / "build" / "tmp"


def digest(path: pathlib.Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def build(output: pathlib.Path, epoch: str) -> tuple[str, str]:
    env = os.environ.copy()
    env["SOURCE_DATE_EPOCH"] = epoch
    build_dir = output / "build"
    subprocess.run(["make", "release", "package", f"BUILD_DIR={build_dir}"],
                   cwd=ROOT, env=env, check=True)
    release = build_dir / "release-build" / "myvmp"
    archives = sorted((build_dir / "dist").glob("*.tar.gz"))
    if not release.is_file() or len(archives) != 1:
        raise RuntimeError("release did not produce exactly one binary and archive")
    return digest(release), digest(archives[0])


def main() -> int:
    TEMP_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="myvmp-repro-", dir=TEMP_ROOT) as temporary:
        root = pathlib.Path(temporary)
        first = build(root / "one", "1700000000")
        second = build(root / "two", "1700000000")
    if first != second:
        raise SystemExit(f"reproducibility check failed: first={first}, second={second}")
    print(f"reproducible release passed: binary={first[0]} archive={first[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
