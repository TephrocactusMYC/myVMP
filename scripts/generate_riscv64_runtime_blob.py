#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "runtime" / "riscv64_vm.S"
OUTPUT = ROOT / "include" / "myvmp" / "runtime" / "riscv64_blob.inc"


def generate() -> str:
    with tempfile.TemporaryDirectory(prefix="myvmp-riscv64-runtime-") as directory:
        obj = Path(directory) / "runtime.o"
        elf = Path(directory) / "runtime.elf"
        raw = Path(directory) / "runtime.bin"
        subprocess.run(["riscv64-linux-gnu-gcc", "-march=rv64im", "-mabi=lp64", "-c",
                        str(SOURCE), "-o", str(obj)], check=True)
        subprocess.run(["riscv64-linux-gnu-ld", "--no-relax", "-Ttext=0", "-e",
                        "myvmp_riscv64_entry", str(obj), "-o", str(elf)], check=True)
        subprocess.run(["riscv64-linux-gnu-objcopy", "-O", "binary", "--only-section=.text",
                        str(elf), str(raw)], check=True)
        data = raw.read_bytes()
    lines = ["// Generated from runtime/riscv64_vm.S; do not edit manually."]
    for offset in range(0, len(data), 12):
        lines.append("    " + " ".join(f"0x{byte:02x}," for byte in data[offset:offset + 12]))
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
        print("RV64 runtime blob is up to date")
        return 0
    OUTPUT.write_text(content, encoding="utf-8")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
