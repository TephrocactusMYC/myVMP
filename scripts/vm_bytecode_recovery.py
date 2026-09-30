"""Open-source VM runtime format recovery used by reference_devirtualizer.py.

The embedded runtime tables and header key are enough to reverse bytecode
whitening and operand-window encoding. This module recovers VM instructions and
explicit bytecode targets; it does not interpret handler semantics.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any


MASK32 = 0xffffffff
STREAM_MIX = 0x9e3779b9
TARGET_OFFSETS = {0x05: 11, 0x06: 1, 0x07: 4, 0x08: 11,
                  0xaf: 2, 0xb9: 3, 0xc2: 4, 0xe4: 1}
CONDITIONAL_BRANCHES = {0x05, 0x07, 0x08, 0xaf, 0xb9, 0xc2}
TERMINATORS = {0x06, 0x44, 0x55}
REPO_ROOT = Path(__file__).resolve().parent.parent


def stream_decode(encrypted: bytes, key: int) -> bytearray:
    return bytearray(value ^ (((key ^ ((index * STREAM_MIX) & MASK32)) >>
                                ((index & 3) * 8)) & 0xff)
                     for index, value in enumerate(encrypted))


def next_block_key(value: int, index: int) -> int:
    value ^= (value << 13) & MASK32
    value ^= value >> 17
    value ^= (value << 5) & MASK32
    return (value ^ (index * 0x85ebca6b)) & MASK32


def undo_aarch64_rolling(payload: bytearray, blocks: list[dict[str, int]],
                         key: int, multi: bool) -> None:
    block_key = key ^ 0x6a09e667
    for index, block in enumerate(blocks):
        if index:
            block_key = next_block_key(block_key, index)
        if multi:
            block_key ^= block["owner"] * 0x27d4eb2d
        for relative, pc in enumerate(range(block["start"], block["end"])):
            payload[pc] ^= (block_key >> ((relative & 3) * 8)) & 0xff


def aarch64_single_dispatch_map(code: bytes, dispatcher_xor: int) -> dict[int, int]:
    """Map physical opcodes using the shipped, patched A64 dispatcher comparisons.

    The comparison order is a native template contract. Its logical order comes
    from this open-source repository, never from a sidecar or protected payload.
    """
    runtime_source = (REPO_ROOT / "src/runtime/aarch64.cpp").read_text(encoding="utf-8")
    enum_source = (REPO_ROOT / "include/myvmp/bytecode/bytecode.hpp").read_text(encoding="utf-8")
    array = runtime_source.split("kDispatcherOpcodes{", 1)[1].split("};", 1)[0]
    names = re.findall(r"bytecode::Opcode::(\w+)", array)
    enum_body = enum_source.split("enum class Opcode : std::uint8_t {", 1)[1].split("};", 1)[0]
    opcodes = {name: int(value, 0) for name, value in re.findall(
        r"^\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", enum_body, re.MULTILINE)}
    if len(names) != 99 or any(name not in opcodes for name in names):
        raise ValueError("AArch64 dispatcher source contract is inconsistent")
    words = [int.from_bytes(code[offset:offset + 4], "little")
             for offset in range(0, len(code) - 3, 4)]
    signature_mask = 0xffc003ff
    primary = [word for word in words if word & signature_mask == 0x7100015f][:len(names)]
    if len(primary) != len(names):
        raise ValueError("AArch64 runtime dispatcher comparison table is incomplete")
    mapping: dict[int, int] = {}

    def record(word: int, logical: int) -> None:
        encoded = (word >> 10) & 0xfff
        if encoded > 0xff:
            raise ValueError("AArch64 dispatcher contains an invalid opcode comparison")
        physical = encoded ^ dispatcher_xor
        if physical in mapping and mapping[physical] != logical:
            raise ValueError("AArch64 dispatcher has conflicting opcode mappings")
        mapping[physical] = logical

    for word, name in zip(primary, names):
        record(word, opcodes[name])
    duplicate_names = ("LoadImm64", "AddImm64", "SubImm64", "AndImm64",
                       "XorImm64", "OrImm64", "MoveReg64")
    duplicates = [word for word in words if word & signature_mask == 0x7100023f][:7]
    if len(duplicates) != len(duplicate_names):
        raise ValueError("AArch64 runtime duplicate-opcode comparisons are incomplete")
    for word, name in zip(duplicates, duplicate_names):
        record(word, opcodes[name])
    for signature, name in ((0x7100021f, "CompareImmBranch"),
                            (0x710001ff, "CompareRegBranch"),
                            (0x710001df, "TestImmBranch")):
        candidates = [word for word in words if word & signature_mask == signature]
        if not candidates:
            raise ValueError(f"AArch64 runtime {name} comparison is missing")
        record(candidates[0], opcodes[name])
    return mapping


def decode_instructions(encrypted: bytes, architecture: str, runtime: dict[str, Any],
                        tables: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, int]]]:
    key = tables["key"]
    blocks = runtime["blocks"]
    payload = stream_decode(encrypted, key)
    multi = runtime["profile_count_observed"] == 2
    if architecture == "aarch64":
        undo_aarch64_rolling(payload, blocks, key, multi)
    instructions: list[dict[str, Any]] = []
    for block_index, block in enumerate(blocks):
        owner = block["owner"]
        pc = block["start"]
        while pc < block["end"]:
            encoded_opcode = payload[pc]
            if architecture == "aarch64":
                if multi:
                    opcode_mask = (key ^ (block_index * 0x3d) ^ (owner * 0x6d)) & 0xff
                    physical = encoded_opcode ^ opcode_mask
                    mapped = tables["logical"][owner][physical]
                    logical = mapped ^ (key & 0xff) ^ ((owner * 0x5d) & 0xff) ^ \
                        ((physical * 0x9b) & 0xff)
                    size = tables["sizes"][logical]
                else:
                    physical = encoded_opcode
                    logical = tables["single_dispatch"].get(physical)
                    if logical is None:
                        raise ValueError(f"unknown AArch64 opcode at byte {pc}")
                    size = tables["sizes"][physical ^ tables["dispatcher_xor"]]
                base = (key ^ (pc * 0x9b) ^ (encoded_opcode * 0x5d)) & 0xff
            else:
                opcode_mask = (key ^ (block["start"] * 0x3d) ^
                               (block["end"] * 0xc7) ^ (owner * 0x6d)) & 0xff
                physical = encoded_opcode ^ opcode_mask
                logical = tables["logical"][owner][physical]
                size = tables["sizes"][owner][physical]
                base = (key ^ (pc * 0x9b) ^ (physical * 0x5d) ^ (owner * 0x6d)) & 0xff
            if logical == 0xff:
                raise ValueError(f"invalid logical opcode at byte {pc}")
            if pc + 1 + size > block["end"]:
                raise ValueError(f"VM instruction at byte {pc} crosses its block boundary")
            operands = bytes(payload[pc + 1 + index] ^ ((base + index * 0xa7) & 0xff)
                             for index in range(size))
            target = None
            target_offset = TARGET_OFFSETS.get(logical)
            if target_offset is not None:
                operand_index = target_offset - 1
                if operand_index + 4 > size:
                    raise ValueError(f"VM control operand at byte {pc} is truncated")
                target = int.from_bytes(operands[operand_index:operand_index + 4], "little")
            instructions.append({"offset": pc, "block_index": block_index,
                                 "physical_opcode": physical, "logical_opcode": logical,
                                 "operand_size": size, "operands_hex": operands.hex(),
                                 "explicit_target_offset": target})
            pc += 1 + size
    block_by_offset = {block["start"]: index for index, block in enumerate(blocks)}
    edges: set[tuple[int, int]] = set()
    for instruction in instructions:
        target = instruction["explicit_target_offset"]
        if target is not None:
            if target not in block_by_offset:
                raise ValueError(f"VM control target {target} is not a block entry")
            edges.add((instruction["block_index"], block_by_offset[target]))
    last_by_block = {item["block_index"]: item for item in instructions}
    for block_index in range(len(blocks) - 1):
        tail = last_by_block[block_index]
        opcode = tail["logical_opcode"]
        if opcode not in TERMINATORS and (opcode != 0xaf or
                                          bytes.fromhex(tail["operands_hex"])[0] != 14):
            edges.add((block_index, block_index + 1))
    return instructions, [{"source_block_index": source, "destination_block_index": target}
                          for source, target in sorted(edges)]
