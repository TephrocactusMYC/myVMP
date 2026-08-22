#!/usr/bin/env python3
from pathlib import Path
import itertools
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MYVMP = ROOT / "build" / "myvmp"
LOG_DIR = ROOT / "build" / "logs" / "quality"
OUT = LOG_DIR / "bytecode_seed_sweep.md"


def hamming_distance(lhs: bytes, rhs: bytes) -> float:
    limit = min(len(lhs), len(rhs))
    extra = abs(len(lhs) - len(rhs))
    if limit + extra == 0:
        return 0.0
    different = sum(1 for i in range(limit) if lhs[i] != rhs[i]) + extra
    return different / float(limit + extra)


def read_hex(path: Path) -> bytes:
    return bytes(int(part, 16) for part in path.read_text(encoding="utf-8").split())


def run_demo(seed: int) -> tuple[bytes, bytes, str]:
    result = subprocess.run(
        [str(MYVMP), "lift-arm64", "--seed", str(seed), "0xd28000a0", "0x91001c00", "0xd1000800", "0xd65f03c0"],
        cwd=ROOT,
        env={"MYVMP_LOG_DIR": str(LOG_DIR / f"seed-{seed}"), "MYVMP_DUMP_BYTECODE": "1"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"lift-arm64 failed for seed {seed}")

    manifest = subprocess.run(
        [str(MYVMP), "profile-manifest", str(seed)],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if manifest.returncode != 0:
        raise RuntimeError(manifest.stderr.strip() or f"profile-manifest failed for seed {seed}")

    seed_dir = LOG_DIR / f"seed-{seed}"
    encoded = read_hex(seed_dir / "lift-arm64.bytecode.encoded.hex")
    decoded = read_hex(seed_dir / "lift-arm64.bytecode.decoded.hex")
    return encoded, decoded, manifest.stdout


def main() -> int:
    count = 16
    if len(sys.argv) >= 2:
        count = int(sys.argv[1], 0)
    if count < 2:
        raise RuntimeError("seed sweep requires at least two seeds")

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    payloads: list[bytes] = []
    decoded_payloads: list[bytes] = []
    manifests: list[str] = []
    duplicate_occurrences = {name: 0 for name in ("LoadImm64", "AddImm64", "SubImm64", "XorImm64")}
    for seed in range(1, count + 1):
        payload, decoded, manifest = run_demo(seed)
        payloads.append(payload)
        decoded_payloads.append(decoded)
        manifests.append(manifest)
        profile = json.loads(manifest)
        duplicate_by_value = {value: name for name, value in profile["duplicate_opcodes"].items()}
        logical_by_value = {value: name for name, value in profile["opcodes"].items()}
        operand_sizes = {"LoadImm64": 9, "AddImm64": 10, "SubImm64": 10,
                         "XorImm64": 10, "Return": 1, "Halt": 0}
        pc = 0
        while pc < len(decoded):
            physical = decoded[pc]
            name = duplicate_by_value.get(physical, logical_by_value.get(physical))
            if name is None or name not in operand_sizes:
                raise RuntimeError(f"unexpected demo opcode {physical} for seed {seed}")
            if physical in duplicate_by_value:
                duplicate_occurrences[name] += 1
            pc += 1 + operand_sizes[name]

    distances = [hamming_distance(lhs, rhs) for lhs, rhs in itertools.combinations(payloads, 2)]
    unique_payloads = len(set(payloads))
    unique_manifests = len(set(manifests))
    handler_variant_masks = {json.loads(manifest)["handler_variant_mask"] for manifest in manifests}
    covered_handler_variant_bits = 0
    for mask in handler_variant_masks:
        covered_handler_variant_bits |= mask
    source_literals = [value.to_bytes(8, "little") for value in (5, 7, 2)]
    literal_exposures = sum(
        literal in payload for payload in decoded_payloads for literal in source_literals
    )
    summary = {
        "seeds": count,
        "unique_payloads": unique_payloads,
        "unique_manifests": unique_manifests,
        "min_pairwise_distance": min(distances),
        "average_pairwise_distance": sum(distances) / len(distances),
        "decoded_source_literal_exposures": literal_exposures,
        "unique_handler_variant_masks": len(handler_variant_masks),
        "covered_handler_variant_bits": covered_handler_variant_bits,
        "unique_decoded_payload_sizes": len({len(payload) for payload in decoded_payloads}),
        "duplicate_opcode_occurrences": duplicate_occurrences,
    }

    lines = [
        "# Bytecode Seed Sweep",
        "",
        "This report measures deterministic profile diversity for one stable ARM64 lifting sample.",
        "",
        "```json",
        json.dumps(summary, indent=2, sort_keys=True),
        "```",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"bytecode seed sweep written to {OUT}")

    if unique_payloads != count or unique_manifests != count:
        return 1
    if summary["min_pairwise_distance"] < 0.10:
        return 1
    if literal_exposures != 0:
        return 1
    if summary["unique_decoded_payload_sizes"] < 2:
        return 1
    if len(handler_variant_masks) < 2 or covered_handler_variant_bits != 0x3FFFFF:
        return 1
    if any(count == 0 for count in duplicate_occurrences.values()):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
