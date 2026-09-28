#!/usr/bin/env python3
"""Attacker-side structural oracle for myVMP ELF + published sidecar.

Extracts the encrypted VM region and indexes sidecar block/profile/CFG metadata.
It intentionally does not claim ciphertext is plaintext: runtime block
whitening, operand transforms, and handler semantics are not normalized.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("sidecar must contain a JSON object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="protected ELF artifact")
    parser.add_argument("--sidecar", type=Path, help="sidecar (defaults to ELF + .myvmp.json)")
    parser.add_argument("--payload-file-offset", type=lambda x: int(x, 0), required=True,
                        help="file offset where embedded runtime payload starts; use protect output's payload_offset")
    parser.add_argument("--output", type=Path, help="write JSON report here; default stdout")
    args = parser.parse_args()
    sidecar = args.sidecar or Path(str(args.elf) + ".myvmp.json")
    manifest = load_json(sidecar)
    architecture = manifest.get("architecture")
    profile = manifest.get("profile", {})
    if architecture not in ("aarch64", "arm32", "rv64") or not isinstance(profile, dict):
        raise ValueError("sidecar has no supported architecture/profile")
    blocks = manifest.get("blocks", [])
    if not isinstance(blocks, list) or any(
        not isinstance(block, dict) or not isinstance(block.get("payload_offset"), int)
        or not isinstance(block.get("payload_size"), int)
        or block["payload_offset"] < 0 or block["payload_size"] <= 0
        for block in blocks
    ):
        raise ValueError("sidecar has malformed block ranges")
    if not blocks:
        raise ValueError("sidecar contains no VM blocks")
    ordered_blocks = sorted(blocks, key=lambda block: block["payload_offset"])
    seen_ids: set[int] = set()
    expected_offset = 0
    for block in ordered_blocks:
        block_id = block.get("id")
        if not isinstance(block_id, int) or block_id in seen_ids:
            raise ValueError("sidecar has missing or duplicate block IDs")
        seen_ids.add(block_id)
        if block["payload_offset"] != expected_offset:
            raise ValueError("sidecar block ranges must be contiguous and non-overlapping")
        successors = block.get("successors", [])
        if not isinstance(successors, list) or any(not isinstance(item, int) for item in successors):
            raise ValueError(f"block {block_id} has malformed successors")
        expected_offset += block["payload_size"]
    payload_size = expected_offset
    relative_offset = manifest.get("encrypted_bytecode_offset")
    if not isinstance(relative_offset, int) or relative_offset < 0:
        raise ValueError("sidecar has invalid encrypted_bytecode_offset")
    if args.payload_file_offset < 0:
        raise ValueError("payload file offset must be non-negative")
    absolute_offset = args.payload_file_offset + relative_offset
    with args.elf.open("rb") as stream:
        stream.seek(absolute_offset)
        encrypted = stream.read(payload_size)
    if len(encrypted) != payload_size:
        raise ValueError(f"payload truncated: expected {payload_size} bytes, found {len(encrypted)}")
    report = {
        "schema": "myvmp-reference-devirtualizer-v1",
        "evidence_class": "encrypted_payload_extraction_and_sidecar_metadata",
        "devirtualization_performed": False,
        "architecture": architecture,
        "elf": str(args.elf), "sidecar": str(sidecar),
        "payload_file_offset": absolute_offset, "payload_size": payload_size,
        "encrypted_payload_sha256": hashlib.sha256(encrypted).hexdigest(),
        "payload_recovery": "encrypted bytes extracted at sidecar-declared offset; decryption not claimed",
        "recovery_limits": [
            "AArch64 and ARM32 apply per-block rolling-key/whitening layers in native runtime code.",
            "Native runtimes decode and later re-encode operand windows dynamically.",
            "Profile opcode tables alone do not undo runtime whitening or recover handler semantics.",
        ],
        "blocks": [
            {
                "block_id": block.get("id"),
                "profile_id": block.get("profile_id"),
                "sidecar_successors": block.get("successors", []),
                "payload_offset": block.get("payload_offset"),
                "payload_size": block.get("payload_size"),
                "ciphertext_sha256": hashlib.sha256(encrypted[
                    block["payload_offset"]:block["payload_offset"] + block["payload_size"]
                ]).hexdigest(),
            }
            for block in ordered_blocks
        ],
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, json.JSONDecodeError, ValueError) as error:
        raise SystemExit(f"reference_devirtualizer: {error}")
