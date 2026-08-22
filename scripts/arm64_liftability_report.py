#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ASM_DIR = ROOT / "build" / "asm" / "generated"
LOG_DIR = ROOT / "build" / "logs" / "generated" / "liftability"
OUT = ROOT / "build" / "logs" / "arm64_liftability_report.md"
MYVMP = ROOT / "build" / "myvmp"


def instruction_words(path: Path) -> list[str]:
    words: list[str] = []
    pattern = re.compile(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+")
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if match:
            words.append("0x" + match.group(1))
    return words


def function_base_va(path: Path) -> str:
    pattern = re.compile(r"^([0-9a-f]+) <[^>]+>:$")
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.match(line.strip())
        if match:
            return "0x" + match.group(1)
    return "0"


def main() -> int:
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    rows: list[tuple[str, str, int, str]] = []

    for asm in sorted(ASM_DIR.glob("*.asm")):
        words = instruction_words(asm)
        base = asm.name.removesuffix(".aarch64.asm")
        if not words:
            rows.append((asm.name, "skip", 0, "no instruction words found"))
            continue

        result = subprocess.run(
            [str(MYVMP), "--log-dir", str(LOG_DIR / base), "lift-arm64",
             "--base-va", function_base_va(asm), *words],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        (LOG_DIR / f"{base}.ir.log").write_text(result.stdout, encoding="utf-8")
        (LOG_DIR / f"{base}.err.log").write_text(result.stderr, encoding="utf-8")
        if result.returncode == 0:
            rows.append((asm.name, "ok", len(words), ""))
        else:
            reason = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else f"exit {result.returncode}"
            rows.append((asm.name, "fail", len(words), reason))

    lines = [
        "# ARM64 Liftability Report",
        "",
        "This report lifts generated AArch64 candidate-function disassembly with `myvmp lift-arm64`.",
        "Executable before/after behavior is verified separately by `make arm64-vm-protect-test`.",
        "",
        "| Fixture | Status | Instructions | Reason |",
        "| --- | --- | ---: | --- |",
    ]
    for name, status, count, reason in rows:
        lines.append(f"| `{name}` | {status} | {count} | {reason} |")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"liftability report written to {OUT}")

    strict = "--strict" in sys.argv[1:]
    return 1 if strict and any(status == "fail" for _, status, _, _ in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
