#!/usr/bin/env python3
"""Generate a reproducible myVMP analysis bundle from original/protected artifacts."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
from typing import Any


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], *, allow_failure: bool = False) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0 and not allow_failure:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{detail}")
    return result


def write_text(path: pathlib.Path, value: str) -> None:
    path.write_text(value, encoding="utf-8")


def artifact(path: pathlib.Path) -> dict[str, Any]:
    return {"path": str(path), "size": path.stat().st_size, "sha256": sha256(path)}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=pathlib.Path, default=pathlib.Path("build/myvmp"))
    parser.add_argument("--input", type=pathlib.Path, required=True, help="original ELF")
    parser.add_argument("--protected", type=pathlib.Path, required=True, help="protected ELF")
    parser.add_argument("--symbol", help="symbol used for inspect/lift evidence")
    parser.add_argument("--output", type=pathlib.Path, required=True, help="new report directory")
    parser.add_argument("--before-bytes", type=pathlib.Path,
                        help="optional extracted original byte range for quality metrics")
    parser.add_argument("--after-bytes", type=pathlib.Path,
                        help="optional extracted protected byte range for quality metrics")
    parser.add_argument("--whole-elf-quality", action="store_true",
                        help="explicitly run the potentially expensive whole-ELF quality scan")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    for path, label in ((args.tool, "tool"), (args.input, "input"),
                        (args.protected, "protected input")):
        if not path.is_file():
            raise RuntimeError(f"{label} does not exist: {path}")
    if (args.before_bytes is None) != (args.after_bytes is None):
        raise RuntimeError("--before-bytes and --after-bytes must be supplied together")
    if args.whole_elf_quality and args.before_bytes:
        raise RuntimeError("--whole-elf-quality cannot be combined with extracted byte ranges")
    if args.output.exists():
        raise RuntimeError(f"output directory already exists: {args.output}")
    args.output.mkdir(parents=True)

    inspect_text = ""
    lift_text = ""
    if args.symbol:
        inspected = run([str(args.tool), "inspect-elf", str(args.input), args.symbol])
        inspect_text = inspected.stdout
        write_text(args.output / "inspect.txt", inspect_text)
        lifted = run([str(args.tool), "lift-elf-symbol", str(args.input), args.symbol],
                     allow_failure=True)
        lift_text = lifted.stdout + lifted.stderr
        write_text(args.output / "lift.txt", lift_text)

    quality: dict[str, Any] | None = None
    quality_before = args.before_bytes or args.input
    quality_after = args.after_bytes or args.protected
    if args.before_bytes or args.whole_elf_quality:
        quality_result = run([str(args.tool), "quality-report", "--json",
                              str(quality_before), str(quality_after)])
        quality = json.loads(quality_result.stdout)
        write_text(args.output / "quality.json", json.dumps(quality, indent=2) + "\n")

    manifest_source = pathlib.Path(str(args.protected) + ".myvmp.json")
    manifest: dict[str, Any] | None = None
    if manifest_source.is_file():
        manifest = json.loads(manifest_source.read_text(encoding="utf-8"))
        shutil.copyfile(manifest_source, args.output / "protected.manifest.json")

    report: dict[str, Any] = {
        "schema": "myvmp-analysis-v1",
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "tool": artifact(args.tool),
        "input": artifact(args.input),
        "protected": artifact(args.protected),
        "symbol": args.symbol,
        "quality_scope": ("extracted-ranges" if args.before_bytes else
                          "whole-elf" if args.whole_elf_quality else "not-computed"),
        "quality_inputs": ({"before": artifact(quality_before), "after": artifact(quality_after)}
                           if quality is not None else None),
        "quality": quality,
        "manifest_present": manifest is not None,
        "manifest_summary": None if manifest is None else {
            "architecture": manifest.get("architecture"),
            "mode": manifest.get("mode"),
            "block_count": len(manifest.get("blocks", [])),
            "bridge_count": manifest.get("bridge_count", 0),
        },
        "inspect_file": "inspect.txt" if inspect_text else None,
        "lift_file": "lift.txt" if lift_text else None,
    }
    write_text(args.output / "report.json", json.dumps(report, indent=2) + "\n")

    summary = report["manifest_summary"] or {}
    scope_note = ("Metrics compare extracted byte ranges."
                  if args.before_bytes else
                  "Whole-ELF metrics were explicitly requested; unchanged application code can dominate them."
                  if args.whole_elf_quality else
                  "Quality metrics were not computed. Supply extracted ranges, or opt into the expensive whole-ELF scan.")
    metric = quality or {}
    markdown = f"""# myVMP Analysis Report

- Generated (UTC): `{report['generated_utc']}`
- Symbol: `{args.symbol or 'not supplied'}`
- Original: `{args.input}` ({report['input']['size']} bytes)
- Protected: `{args.protected}` ({report['protected']['size']} bytes)
- Original SHA-256: `{report['input']['sha256']}`
- Protected SHA-256: `{report['protected']['sha256']}`
- Architecture: `{summary.get('architecture', 'manifest unavailable')}`
- VM mode: `{summary.get('mode', 'manifest unavailable')}`
- Blocks / bridges: `{summary.get('block_count', 'n/a')}` / `{summary.get('bridge_count', 'n/a')}`

## Quality summary

- Scope: `{report['quality_scope']}`
- Same-position ratio: `{metric.get('same_position_ratio', 'not computed')}`
- Longest same-position run: `{metric.get('longest_same_position_run', 'not computed')}`
- 32-bit aligned word exposure: `{metric.get('aligned_word32_exposure_ratio', 'not computed')}`
- 64-bit aligned word exposure: `{metric.get('aligned_word64_exposure_ratio', 'not computed')}`
- 32-bit literal exposure: `{metric.get('source_literal32_exposure_ratio', 'not computed')}`
- 64-bit literal exposure: `{metric.get('source_literal64_exposure_ratio', 'not computed')}`
- Size ratio: `{metric.get('size_ratio', 'not computed')}`

{scope_note}

## Evidence files

- `report.json`: machine-readable report
- `quality.json`: raw quality metrics, when requested
- `inspect.txt`: ELF eligibility and patch plan, when a symbol was supplied
- `lift.txt`: original symbol IR or lifting diagnostic, when a symbol was supplied
- `protected.manifest.json`: copied protection sidecar, when available

Metrics are regression evidence, not proof against devirtualization. Validate complete
original/protected behavior separately and archive the result with this bundle.
"""
    write_text(args.output / "report.md", markdown)
    print(args.output / "report.md")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"analysis report failed: {error}", file=sys.stderr)
        raise SystemExit(1)
