#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "runtime" / "arm32_vm.S"
OUTPUT = ROOT / "include" / "myvmp" / "runtime" / "arm32_blob.inc"


def text_section(elf: bytes) -> bytes:
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise RuntimeError("Clang did not emit a little-endian ELF32 object")
    shoff = struct.unpack_from("<I", elf, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 46)
    headers = [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    strings = headers[shstrndx]
    names = elf[strings[4] : strings[4] + strings[5]]
    for header in headers:
        end = names.find(b"\0", header[0])
        if names[header[0] : end] == b".text":
            return elf[header[4] : header[4] + header[5]]
    raise RuntimeError("ARM32 runtime object has no .text section")


def generate() -> str:
    with tempfile.TemporaryDirectory(prefix="myvmp-arm32-runtime-") as directory:
        obj = Path(directory) / "runtime.o"
        subprocess.run(["clang", "--target=armv7-linux-gnueabihf", "-c", str(SOURCE), "-o", str(obj)], check=True)
        relocations = subprocess.run(["readelf", "-rW", str(obj)], check=True, text=True,
                                     stdout=subprocess.PIPE).stdout
        if "There are no relocations" not in relocations:
            raise RuntimeError("ARM32 runtime template must not contain relocations")
        data = text_section(obj.read_bytes())
    lines = ["// Generated from runtime/arm32_vm.S; do not edit manually."]
    for offset in range(0, len(data), 12):
        lines.append("    " + " ".join(f"0x{byte:02x}," for byte in data[offset : offset + 12]))
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
        print("ARM32 runtime blob is up to date")
        return 0
    OUTPUT.write_text(content, encoding="utf-8")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
