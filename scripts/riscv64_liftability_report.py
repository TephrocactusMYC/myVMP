#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = ["rv64_loop", "rv64_global_access", "rv64_switch"]
TEMP_ROOT = ROOT / "build" / "tmp"


def main() -> int:
    TEMP_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="myvmp-rv64-corpus-", dir=TEMP_ROOT) as directory:
        elf = Path(directory) / "corpus"
        subprocess.run(["riscv64-linux-gnu-gcc", "-O2", "-march=rv64im", "-mabi=lp64",
                        "-mno-relax", "-fno-pic", "-ffreestanding", "-c",
                        str(ROOT / "tests/fixtures/rv64_corpus.c"), "-o", str(elf) + ".o"], check=True)
        subprocess.run(["riscv64-linux-gnu-ld", "--no-relax", "-Ttext=0x10000",
                        "-e", "rv64_loop", str(elf) + ".o", "-o", str(elf)], check=True)
        failed = []
        for symbol in SYMBOLS:
            result = subprocess.run([str(ROOT / "build/myvmp"), "lift-elf-symbol",
                                     str(elf), symbol], text=True, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.PIPE)
            print(f"{symbol}: {'liftable' if result.returncode == 0 else 'rejected'}")
            if result.returncode != 0:
                failed.append((symbol, result.stderr.strip()))
        for symbol, reason in failed:
            print(f"  {symbol}: {reason}")
        return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
