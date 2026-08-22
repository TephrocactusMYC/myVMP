#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "runtime" / "aarch64_vm.S"
OUTPUT = ROOT / "include" / "myvmp" / "runtime" / "aarch64_blob.inc"


def generate() -> str:
    with tempfile.TemporaryDirectory(prefix="myvmp-aarch64-runtime-") as directory:
        temp = Path(directory)
        obj = temp / "runtime.o"
        binary = temp / "runtime.bin"
        subprocess.run(["aarch64-linux-gnu-as", str(SOURCE), "-o", str(obj)], check=True)
        relocations = subprocess.run(
            ["aarch64-linux-gnu-readelf", "-rW", str(obj)],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout
        if "There are no relocations" not in relocations:
            raise RuntimeError("AArch64 runtime template must not contain relocations")
        subprocess.run(
            ["aarch64-linux-gnu-objcopy", "-O", "binary", "--only-section=.text", str(obj), str(binary)],
            check=True,
        )
        data = binary.read_bytes()
    lines = ["// Generated from runtime/aarch64_vm.S; do not edit manually."]
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append("    " + " ".join(f"0x{byte:02x}," for byte in chunk))
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = generate()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != content:
            print(f"generated runtime blob is stale: {OUTPUT}")
            return 1
        print("AArch64 runtime blob is up to date")
        return 0
    OUTPUT.write_text(content, encoding="utf-8")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
