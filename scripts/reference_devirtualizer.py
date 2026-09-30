#!/usr/bin/env python3
"""Recover attacker-visible VM block metadata from a protected ELF runtime.

This structural oracle reads the runtime header and encrypted bytecode without
requiring a published sidecar. A sidecar, when available, supplies ground truth
for a block/owner recovery comparison. It does not recover instruction semantics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, BinaryIO

from vm_bytecode_recovery import aarch64_single_dispatch_map, decode_instructions


ARCHITECTURES = ("aarch64", "arm32", "rv64")
MAX_BYTECODE_SIZE = 65536
MAX_BLOCKS = 65536
ELF_TARGETS = {"aarch64": (2, 183), "arm32": (1, 40), "rv64": (2, 243)}


def normalized_architecture(value: Any) -> Any:
    return "arm32" if value == "arm32-a32" else value


def read_exact(stream: BinaryIO, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0:
        raise ValueError(f"{label} has a negative file range")
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise ValueError(f"{label} is truncated: expected {size} bytes, found {len(data)}")
    return data


def load_sidecar(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("sidecar must contain a JSON object")
    return value


def validate_elf(stream: BinaryIO, architecture: str) -> None:
    header = read_exact(stream, 0, 20, "ELF header")
    expected_class, expected_machine = ELF_TARGETS[architecture]
    if (header[:4] != b"\x7fELF" or header[4] != expected_class or header[5] != 1 or
            int.from_bytes(header[18:20], "little") != expected_machine):
        raise ValueError(f"input is not a little-endian {architecture} ELF")


def validate_blocks(blocks: list[dict[str, int]], bytecode_size: int) -> None:
    if not blocks or len(blocks) > MAX_BLOCKS:
        raise ValueError("runtime has an invalid block count")
    expected = 0
    for index, block in enumerate(blocks):
        if block["start"] != expected or block["end"] <= expected or block["end"] > bytecode_size:
            raise ValueError(f"runtime block {index} has a non-contiguous or invalid range")
        if block["owner"] not in (0, 1):
            raise ValueError(f"runtime block {index} has an invalid owner")
        expected = block["end"]
    if expected != bytecode_size:
        raise ValueError("runtime blocks do not cover the encrypted bytecode")


def recover_runtime(stream: BinaryIO, payload_offset: int, architecture: str,
                    code_size: int) -> tuple[dict[str, Any], bytes, dict[str, Any]]:
    if payload_offset < 0 or code_size < 0:
        raise ValueError("payload file offset and runtime code size must be non-negative")
    header_offset = payload_offset + code_size
    if architecture == "aarch64":
        header = read_exact(stream, header_offset, 32, "AArch64 runtime header")
        bytecode_size, key, _, count, dispatcher_xor, _, profile_count = struct.unpack("<IIIIIQI", header)
        if profile_count not in (0, 2):
            raise ValueError("AArch64 runtime has an unsupported profile count")
        if count == 0 or count > MAX_BLOCKS or bytecode_size == 0 or bytecode_size > MAX_BYTECODE_SIZE:
            raise ValueError("AArch64 runtime has an invalid bytecode or block count")
        table_offset = header_offset + 32
        ends_data = read_exact(stream, table_offset, count * 4, "AArch64 block boundaries")
        ends = struct.unpack(f"<{count}I", ends_data)
        owners = [0] * count
        cursor = table_offset + count * 4
        tables: dict[str, Any] = {"key": key, "dispatcher_xor": dispatcher_xor & 0xff}
        if profile_count == 2:
            owner_data = read_exact(stream, cursor, count * 4, "AArch64 block owners")
            encoded_owners = struct.unpack(f"<{count}I", owner_data)
            owners = [value ^ key ^ ((index * 0x9e37) & 0xffffffff)
                      for index, value in enumerate(encoded_owners)]
            cursor += count * 4 + 8
            maps = read_exact(stream, cursor, 512, "AArch64 opcode translation tables")
            tables["logical"] = (maps[:256], maps[256:])
            cursor += 512
        else:
            code = read_exact(stream, payload_offset, code_size, "AArch64 runtime code")
            tables["single_dispatch"] = aarch64_single_dispatch_map(code, dispatcher_xor & 0xff)
        tables["sizes"] = read_exact(stream, cursor, 256, "AArch64 operand-size table")
        cursor += 256
        blocks = []
        start = 0
        for end, owner in zip(ends, owners):
            blocks.append({"start": start, "end": end, "owner": owner})
            start = end
    else:
        prefix = read_exact(stream, header_offset, 24, f"{architecture} runtime header")
        if architecture == "arm32":
            bytecode_size, key, _, count, _, _ = struct.unpack("<IIIIII", prefix)
        else:
            bytecode_size, key, _, count, _ = struct.unpack("<IIQII", prefix)
        if count == 0 or count > MAX_BLOCKS or bytecode_size == 0 or bytecode_size > MAX_BYTECODE_SIZE:
            raise ValueError(f"{architecture} runtime has an invalid bytecode or block count")
        format_tables = read_exact(stream, header_offset + 24, 1024,
                                   f"{architecture} opcode and operand-size tables")
        tables = {"key": key, "logical": (format_tables[:256], format_tables[256:512]),
                  "sizes": (format_tables[512:768], format_tables[768:1024])}
        table_offset = header_offset + 1048
        block_data = read_exact(stream, table_offset, count * 12, f"{architecture} block table")
        blocks = [dict(zip(("start", "end", "owner"), values))
                  for values in struct.iter_unpack("<III", block_data)]
        cursor = table_offset + count * 12
        profile_count = 2 if any(block["owner"] == 1 for block in blocks) else 1
    validate_blocks(blocks, bytecode_size)
    encrypted = read_exact(stream, cursor, bytecode_size, "encrypted VM bytecode")
    return ({"bytecode_offset": cursor - payload_offset,
             "bytecode_size": bytecode_size, "block_count": count,
             "profile_count_observed": 1 if profile_count == 0 else profile_count,
             "key_exposed_in_header": True,
             "blocks": blocks}, encrypted, tables)


def compare_sidecar(manifest: dict[str, Any], architecture: str,
                    runtime: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if normalized_architecture(manifest.get("architecture")) != architecture:
        raise ValueError("sidecar architecture disagrees with runtime selection")
    if manifest.get("encrypted_bytecode_offset") != runtime["bytecode_offset"]:
        raise ValueError("sidecar bytecode offset disagrees with the runtime header")
    raw_blocks = manifest.get("blocks")
    if not isinstance(raw_blocks, list) or len(raw_blocks) != runtime["block_count"]:
        raise ValueError("sidecar block count disagrees with the runtime header")
    primary = manifest.get("profile")
    secondary = manifest.get("secondary_profile")
    if not isinstance(primary, dict) or not isinstance(primary.get("seed"), int):
        raise ValueError("sidecar has no primary profile seed")
    if secondary is not None and (not isinstance(secondary, dict) or
                                  not isinstance(secondary.get("seed"), int)):
        raise ValueError("sidecar has an invalid secondary profile seed")
    ids: set[int] = set()
    details = []
    boundary_matches = 0
    owner_matches = 0
    for index, (raw, recovered) in enumerate(zip(raw_blocks, runtime["blocks"])):
        if not isinstance(raw, dict) or not isinstance(raw.get("id"), int) or raw["id"] in ids:
            raise ValueError("sidecar has a malformed or duplicate block ID")
        ids.add(raw["id"])
        start, size = raw.get("payload_offset"), raw.get("payload_size")
        if not isinstance(start, int) or not isinstance(size, int) or size <= 0:
            raise ValueError(f"sidecar block {index} has an invalid range")
        profile_id = raw.get("profile_id")
        if profile_id == primary["seed"]:
            expected_owner = 0
        elif secondary is not None and profile_id == secondary["seed"]:
            expected_owner = 1
        else:
            raise ValueError(f"sidecar block {index} has an unknown profile")
        boundary_match = start == recovered["start"] and start + size == recovered["end"]
        owner_match = expected_owner == recovered["owner"]
        boundary_matches += boundary_match
        owner_matches += owner_match
        details.append({"sidecar_block_id": raw["id"],
                        "sidecar_boundary_match": boundary_match,
                        "sidecar_owner_match": owner_match})
    return details, {"ground_truth": "published_sidecar",
                     "block_boundary_matches": boundary_matches,
                     "block_owner_matches": owner_matches,
                     "block_count": runtime["block_count"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="protected ELF artifact")
    parser.add_argument("--sidecar", type=Path, help="sidecar; defaults to ELF + .myvmp.json if present")
    parser.add_argument("--no-sidecar", action="store_true", help="analyze only the protected ELF")
    parser.add_argument("--architecture", choices=ARCHITECTURES,
                        help="required without a sidecar")
    parser.add_argument("--runtime-code-size", type=lambda value: int(value, 0),
                        help="runtime code size in bytes; required without a sidecar")
    parser.add_argument("--payload-file-offset", type=lambda value: int(value, 0), required=True,
                        help="file offset where the embedded runtime payload starts")
    parser.add_argument("--output", type=Path, help="write JSON report here; default stdout")
    args = parser.parse_args()
    if args.sidecar and args.no_sidecar:
        parser.error("--sidecar and --no-sidecar cannot be combined")
    sidecar_path = None if args.no_sidecar else (args.sidecar or Path(str(args.elf) + ".myvmp.json"))
    manifest = load_sidecar(sidecar_path) if sidecar_path and sidecar_path.is_file() else None
    if args.sidecar and manifest is None:
        raise ValueError(f"sidecar does not exist: {args.sidecar}")
    architecture = args.architecture or (normalized_architecture(manifest.get("architecture"))
                                        if manifest else None)
    code_size = (args.runtime_code_size if args.runtime_code_size is not None else
                 manifest.get("runtime_code_size") if manifest else None)
    if architecture not in ARCHITECTURES or not isinstance(code_size, int):
        parser.error("architecture and runtime code size are required without a sidecar")
    if manifest is not None and args.runtime_code_size is not None and args.runtime_code_size != manifest.get("runtime_code_size"):
        raise ValueError("runtime code size disagrees with sidecar")
    with args.elf.open("rb") as stream:
        validate_elf(stream, architecture)
        runtime, encrypted, tables = recover_runtime(stream, args.payload_file_offset,
                                                     architecture, code_size)
    instructions, edges = decode_instructions(encrypted, architecture, runtime, tables)
    details = [{} for _ in runtime["blocks"]]
    comparison = None
    if manifest is not None:
        details, comparison = compare_sidecar(manifest, architecture, runtime)
        physical_by_id = {block["id"]: index for index, block in enumerate(manifest["blocks"])}
        for block in manifest["blocks"]:
            successors = block.get("successors", [])
            if not isinstance(successors, list) or any(not isinstance(target, int) or
                                                       target not in physical_by_id
                                                       for target in successors):
                raise ValueError("sidecar contains an invalid CFG successor")
        expected_edges = {(index, physical_by_id[target])
                          for index, block in enumerate(manifest["blocks"])
                          for target in block.get("successors", [])}
        recovered_edges = {(edge["source_block_index"], edge["destination_block_index"])
                           for edge in edges}
        comparison["sidecar_cfg_edges"] = len(expected_edges)
        comparison["recovered_cfg_edge_matches"] = len(expected_edges & recovered_edges)
        comparison["recovered_cfg_edge_extras"] = len(recovered_edges - expected_edges)
    instruction_counts = [0] * len(runtime["blocks"])
    for instruction in instructions:
        instruction_counts[instruction["block_index"]] += 1
    blocks = []
    for index, (block, detail) in enumerate(zip(runtime["blocks"], details)):
        chunk = encrypted[block["start"]:block["end"]]
        blocks.append({"physical_block_index": index, "payload_offset": block["start"],
                       "payload_size": block["end"] - block["start"],
                       "recovered_owner_index": block["owner"],
                       "recovered_instruction_count": instruction_counts[index],
                       "ciphertext_sha256": hashlib.sha256(chunk).hexdigest(), **detail})
    report = {
        "schema": "myvmp-reference-devirtualizer-v3",
        "evidence_class": "runtime_bytecode_static_recovery",
        "devirtualization_performed": False,
        "bytecode_decoding_performed": True,
        "architecture": architecture,
        "elf": str(args.elf), "sidecar": str(sidecar_path) if manifest else None,
        "payload_file_offset": args.payload_file_offset,
        "runtime_code_size": code_size,
        "encrypted_bytecode_file_offset": args.payload_file_offset + runtime["bytecode_offset"],
        "payload_size": runtime["bytecode_size"],
        "encrypted_payload_sha256": hashlib.sha256(encrypted).hexdigest(),
        "runtime_metadata": {key: value for key, value in runtime.items() if key != "blocks"},
        "recovered_block_boundaries": len(blocks),
        "recovered_block_owners": len(blocks),
        "recovered_vm_instructions": len(instructions),
        "recovered_explicit_targets": sum(item["explicit_target_offset"] is not None
                                          for item in instructions),
        "recovered_cfg_edges": len(edges),
        "recovered_semantic_ir_instructions": 0,
        "sidecar_comparison": comparison,
        "recovery_limits": [
            "VM instruction fields and static bytecode targets are recovered from runtime formats.",
            "Dynamic VM return edges and handler effects are not inferred.",
            "Logical VM opcodes are not equivalent to recovered source-level IR semantics.",
        ],
        "blocks": blocks,
        "instructions": instructions,
        "cfg_edges": edges,
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
