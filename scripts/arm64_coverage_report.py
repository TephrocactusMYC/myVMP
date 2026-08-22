#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
ASM_DIR = ROOT / "build" / "asm" / "generated"
OUT = ROOT / "build" / "logs" / "arm64_coverage_report.md"

SUPPORTED = {
    "add",
    "adds",
    "sub",
    "subs",
    "and",
    "ands",
    "bic",
    "mov",
    "movn",
    "movk",
    "movz",
    "eor",
    "eon",
    "orr",
    "orn",
    "cmp",
    "cmn",
    "tst",
    "b.eq",
    "b.ne",
    "b.hs",
    "b.cs",
    "b.lo",
    "b.cc",
    "b.mi",
    "b.pl",
    "b.vs",
    "b.vc",
    "b.hi",
    "b.ls",
    "b.ge",
    "b.lt",
    "b.gt",
    "b.le",
    "b.al",
    "b",
    "cbz",
    "cbnz",
    "tbz",
    "tbnz",
    "csel",
    "csinc",
    "csinv",
    "csneg",
    "madd",
    "msub",
    "sdiv",
    "udiv",
    "smull",
    "umull",
    "adr",
    "adrp",
    "bl",
    "blr",
    "br",
    "fmadd",
    "fadd",
    "fsub",
    "fmul",
    "fdiv",
    "fmov",
    "fcmp",
    "fcmpe",
    "lsl",
    "lsr",
    "asr",
    "ror",
    "sxtb",
    "sxth",
    "sxtw",
    "ubfx",
    "umulh",
    "ldr",
    "ldrb",
    "ldrh",
    "ldrsb",
    "ldrsh",
    "ldrsw",
    "ldp",
    "str",
    "strb",
    "strh",
    "stp",
    "ret",
}

PLANNED_REQUIRED = set()


def parse_mnemonics() -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for path in sorted(ASM_DIR.glob("*.asm")):
        mnemonics: set[str] = set()
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            match = re.search(r"^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+([a-z0-9.]+)", line)
            if match:
                mnemonics.add(match.group(1))
        result[path.name] = mnemonics
    return result


def main() -> None:
    ASM_DIR.mkdir(parents=True, exist_ok=True)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    by_file = parse_mnemonics()
    all_mnemonics = set().union(*by_file.values()) if by_file else set()
    supported = sorted(all_mnemonics & SUPPORTED)
    planned = sorted(all_mnemonics & PLANNED_REQUIRED - SUPPORTED)
    unknown = sorted(all_mnemonics - SUPPORTED - PLANNED_REQUIRED)

    lines = [
        "# ARM64 Coverage Report",
        "",
        "This report is generated from AArch64 fixture disassembly and complements the executable before/after regression.",
        "Mnemonic-level classification is coarse; some mnemonics still have unsupported operand forms.",
        "",
        "## Supported Seen",
        "",
        *(f"- `{m}`" for m in supported),
        "",
        "## Planned Required Seen",
        "",
        *(f"- `{m}`" for m in planned),
        "",
        "## Other Seen",
        "",
        *(f"- `{m}`" for m in unknown),
        "",
        "## By File",
        "",
    ]
    for name, mnemonics in by_file.items():
        lines.append(f"### {name}")
        lines.append("")
        lines.append(", ".join(f"`{m}`" for m in sorted(mnemonics)))
        lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"coverage report written to {OUT}")


if __name__ == "__main__":
    main()
