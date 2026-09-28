#!/usr/bin/env python3
"""Behavior, domain independence, and diversity gate for RV64 sibling VMs."""

from __future__ import annotations

import itertools
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "build" / "myvmp"
REPORT = ROOT / "build" / "logs" / "quality" / "riscv64-multi-vm-quality.json"
TEMP_ROOT = ROOT / "build" / "tmp"


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def timed(command: list[str]) -> tuple[subprocess.CompletedProcess[str], float]:
    started = time.perf_counter()
    result = run(command)
    return result, time.perf_counter() - started


def distance(lhs: bytes, rhs: bytes) -> float:
    length = max(len(lhs), len(rhs))
    common = min(len(lhs), len(rhs))
    return (sum(a != b for a, b in zip(lhs[:common], rhs[:common])) + length - common) / length


def main() -> int:
    count = int(sys.argv[1], 0) if len(sys.argv) > 1 else 8
    if count < 4:
        raise RuntimeError("RV64 quality gate requires at least four seed pairs")
    TEMP_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="myvmp-rv64-quality-", dir=TEMP_ROOT) as directory:
        temp = Path(directory)
        original = temp / "original"
        compiled = run(["riscv64-linux-gnu-gcc", "-O2", "-static", "-march=rv64imafd",
                        "-mabi=lp64d", "-mno-relax", "-fno-pic", "-fno-stack-protector",
                        "-Wl,--no-relax", str(ROOT / "tests/fixtures/rv64_integer.c"),
                        str(ROOT / "tests/fixtures/rv64_driver.c"), "-o", str(original)])
        if compiled.returncode != 0:
            raise RuntimeError(compiled.stderr.strip())
        baseline_samples = [timed(["qemu-riscv64", str(original)]) for _ in range(5)]
        baseline = baseline_samples[0][0]
        if baseline.returncode != 0:
            raise RuntimeError(baseline.stderr.strip() or "baseline QEMU execution failed")
        baseline_seconds = statistics.median(sample[1] for sample in baseline_samples)

        ciphertexts: list[bytes] = []
        manifests: list[str] = []
        bridges: list[int] = []
        balances: list[float] = []
        runtime_ratios: list[float] = []
        size_ratios: list[float] = []
        for index in range(count):
            primary = 0x1301 + index * 0x101
            secondary = 0x7103 + index * 0x181
            output = temp / f"protected-{index}"
            protected = run([str(TOOL), "protect", str(original), "rv64_integer", str(output),
                             str(primary), "--multi-vm", str(secondary)])
            if protected.returncode != 0:
                raise RuntimeError(protected.stderr.strip() or protected.stdout.strip())
            samples = [timed(["qemu-riscv64", str(output)]) for _ in range(3)]
            if any(item[0].returncode != 0 or item[0].stdout != baseline.stdout for item in samples):
                raise RuntimeError(f"behavior mismatch for seeds {primary}/{secondary}")
            match = re.search(r"^payload_offset=0x([0-9a-f]+)$", protected.stdout, re.MULTILINE)
            if not match:
                raise RuntimeError("protect output omitted payload_offset")
            manifest_text = Path(str(output) + ".myvmp.json").read_text(encoding="utf-8")
            manifest = json.loads(manifest_text)
            if manifest.get("runtime_dispatch") != "per-block-dual-domain-v1":
                raise RuntimeError("RV64 sidecar does not publish dynamic dual-domain dispatch")
            owners = [block["profile_id"] for block in manifest["blocks"]]
            if set(owners) != {primary, secondary}:
                raise RuntimeError("both RV64 sibling domains must own physical blocks")
            payload_size = sum(block["payload_size"] for block in manifest["blocks"])
            start = int(match.group(1), 16) + manifest["encrypted_bytecode_offset"]
            ciphertexts.append(output.read_bytes()[start:start + payload_size])
            manifests.append(manifest_text)
            bridges.append(len(manifest["bridges"]))
            balances.append(min(owners.count(primary), owners.count(secondary)) / len(owners))
            runtime_ratios.append(statistics.median(item[1] for item in samples) / baseline_seconds)
            size_ratios.append(output.stat().st_size / original.stat().st_size)

    distances = [distance(a, b) for a, b in itertools.combinations(ciphertexts, 2)]
    result = {
        "seed_pairs": count,
        "behavior_matches": True,
        "unique_ciphertexts": len(set(ciphertexts)),
        "unique_manifests": len(set(manifests)),
        "minimum_ciphertext_distance": min(distances),
        "average_ciphertext_distance": statistics.mean(distances),
        "minimum_bridge_count": min(bridges),
        "minimum_owner_balance": min(balances),
        "median_runtime_ratio": statistics.median(runtime_ratios),
        "maximum_runtime_ratio": max(runtime_ratios),
        "maximum_output_size_ratio": max(size_ratios),
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if (result["unique_ciphertexts"] == count and
                 result["unique_manifests"] == count and
                 result["minimum_ciphertext_distance"] >= 0.50 and
                 result["minimum_bridge_count"] >= 1 and
                 result["minimum_owner_balance"] > 0.0) else 1


if __name__ == "__main__":
    raise SystemExit(main())
