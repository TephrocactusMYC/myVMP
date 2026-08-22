#!/usr/bin/env python3
"""Behavior and diversity gate for independently seeded sibling-VM payloads."""

from __future__ import annotations

import itertools
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import statistics

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "build" / "myvmp"
INPUT = ROOT / "build" / "fixtures" / "generated" / "gen_branch_ops.aarch64"
SYMBOL = "candidate_branch_ops"
REPORT = ROOT / "build" / "logs" / "quality" / "multi-vm-quality.json"


def distance(lhs: bytes, rhs: bytes) -> float:
    length = max(len(lhs), len(rhs))
    if length == 0:
        return 0.0
    common = min(len(lhs), len(rhs))
    changed = sum(a != b for a, b in zip(lhs[:common], rhs[:common]))
    return (changed + length - common) / length


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def timed_run(command: list[str]) -> tuple[subprocess.CompletedProcess[str], float]:
    started = time.perf_counter()
    result = run(command)
    return result, time.perf_counter() - started


def main() -> int:
    count = int(sys.argv[1], 0) if len(sys.argv) > 1 else 8
    if count < 4:
        raise RuntimeError("multi-VM quality gate requires at least four seed pairs")
    baseline_samples = [timed_run(["qemu-aarch64", str(INPUT)]) for _ in range(5)]
    baseline = baseline_samples[0][0]
    if baseline.returncode != 0:
        raise RuntimeError(baseline.stderr.strip() or "baseline QEMU execution failed")

    payloads: list[bytes] = []
    manifests: list[str] = []
    bridge_counts: list[int] = []
    owner_balances: list[float] = []
    size_ratios: list[float] = []
    runtime_ratios: list[float] = []
    baseline_seconds = statistics.median(sample[1] for sample in baseline_samples)
    with tempfile.TemporaryDirectory(prefix="myvmp-multi-quality-") as directory:
        temp = Path(directory)
        for index in range(count):
            primary, secondary = 3001 + index, 7001 + index
            output = temp / f"sample-{index}.aarch64"
            protected = run([str(TOOL), "protect", str(INPUT), SYMBOL, str(output),
                             str(primary), "--multi-vm", str(secondary)])
            if protected.returncode != 0:
                raise RuntimeError(protected.stderr.strip() or protected.stdout.strip())
            executed_samples = [timed_run(["qemu-aarch64", str(output)]) for _ in range(3)]
            executed = executed_samples[0][0]
            if executed.returncode != 0 or executed.stdout != baseline.stdout:
                raise RuntimeError(f"sibling VM behavior mismatch for seeds {primary}/{secondary}")
            offset_match = re.search(r"^payload_offset=0x([0-9a-f]+)$", protected.stdout, re.MULTILINE)
            if not offset_match:
                raise RuntimeError("protect output omitted payload_offset")
            payload_offset = int(offset_match.group(1), 16)
            data = output.read_bytes()
            manifest_text = Path(str(output) + ".myvmp.json").read_text(encoding="utf-8")
            manifest = json.loads(manifest_text)
            manifests.append(manifest_text)
            if manifest["mode"] != "sibling-vm":
                raise RuntimeError("sidecar did not identify sibling VM mode")
            owners = [block["profile_id"] for block in manifest["blocks"]]
            if set(owners) != {primary, secondary}:
                raise RuntimeError("both sibling profiles must own physical blocks")
            bridge_counts.append(len(manifest["bridges"]))
            encoded_size = sum(block["payload_size"] for block in manifest["blocks"])
            encoded_offset = payload_offset + manifest["encrypted_bytecode_offset"]
            payloads.append(data[encoded_offset:encoded_offset + encoded_size])
            owner_balances.append(min(owners.count(primary), owners.count(secondary)) / len(owners))
            size_ratios.append(len(data) / INPUT.stat().st_size)
            runtime_ratios.append(statistics.median(sample[1] for sample in executed_samples) /
                                  baseline_seconds)

    distances = [distance(a, b) for a, b in itertools.combinations(payloads, 2)]
    result = {
        "seed_pairs": count,
        "unique_payloads": len(set(payloads)),
        "unique_manifests": len(set(manifests)),
        "minimum_payload_distance": min(distances),
        "average_payload_distance": sum(distances) / len(distances),
        "minimum_bridge_count": min(bridge_counts),
        "minimum_owner_balance": min(owner_balances),
        "maximum_output_size_ratio": max(size_ratios),
        "median_runtime_ratio": statistics.median(runtime_ratios),
        "maximum_runtime_ratio": max(runtime_ratios),
        "behavior_matches": True,
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if (result["unique_payloads"] == count and
                 result["unique_manifests"] == count and
                 result["minimum_payload_distance"] >= 0.03 and
                 result["minimum_bridge_count"] >= 1 and
                 result["minimum_owner_balance"] > 0.0 and
                 result["maximum_output_size_ratio"] <= 2.0 and
                 result["maximum_runtime_ratio"] <= 20.0) else 1


if __name__ == "__main__":
    raise SystemExit(main())
