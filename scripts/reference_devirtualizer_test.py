#!/usr/bin/env python3
"""Integration checks against real AArch64, ARM32, and RV64 protected ELFs."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from vm_bytecode_recovery import TARGET_OFFSETS


ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "build" / "myvmp"
ORACLE = ROOT / "scripts" / "reference_devirtualizer.py"


def run(*arguments: str | Path) -> str:
    completed = subprocess.run([str(item) for item in arguments], cwd=ROOT,
                               check=True, capture_output=True, text=True)
    return completed.stdout


def check_artifact(original: Path, symbol: str, architecture: str, output: Path,
                   multi: bool = True) -> None:
    command: list[str | Path] = [TOOL, "protect", original, symbol, output, "12345"]
    if multi:
        command.extend(["--multi-vm", "67890"])
    protection = run(*command)
    offset_match = re.search(r"^payload_offset=(0x[0-9a-fA-F]+)$", protection, re.MULTILINE)
    if not offset_match:
        raise AssertionError(f"protector did not report a payload offset: {protection}")
    payload_offset = offset_match.group(1)
    sidecar = json.loads(Path(str(output) + ".myvmp.json").read_text(encoding="utf-8"))
    with_sidecar = json.loads(run(sys.executable, ORACLE, output,
                                 "--payload-file-offset", payload_offset))
    count = len(sidecar["blocks"])
    assert with_sidecar["schema"] == "myvmp-reference-devirtualizer-v3"
    assert with_sidecar["sidecar_comparison"]["block_boundary_matches"] == count
    assert with_sidecar["sidecar_comparison"]["block_owner_matches"] == count
    assert with_sidecar["sidecar_comparison"]["recovered_cfg_edge_matches"] == \
        with_sidecar["sidecar_comparison"]["sidecar_cfg_edges"]
    assert with_sidecar["sidecar_comparison"]["recovered_cfg_edge_extras"] == 0
    assert with_sidecar["recovered_vm_instructions"] == len(with_sidecar["instructions"])
    assert sum(block["recovered_instruction_count"] for block in with_sidecar["blocks"]) == \
        with_sidecar["recovered_vm_instructions"]
    assert all(instruction["logical_opcode"] != 0xff for instruction in with_sidecar["instructions"])
    without_sidecar = json.loads(run(
        sys.executable, ORACLE, output, "--no-sidecar", "--architecture", architecture,
        "--runtime-code-size", str(sidecar["runtime_code_size"]),
        "--payload-file-offset", payload_offset))
    assert without_sidecar["sidecar"] is None
    assert without_sidecar["sidecar_comparison"] is None
    assert without_sidecar["recovered_block_boundaries"] == count
    assert without_sidecar["recovered_block_owners"] == count
    assert [block["recovered_owner_index"] for block in without_sidecar["blocks"]] == [
        block["recovered_owner_index"] for block in with_sidecar["blocks"]]
    assert without_sidecar["instructions"] == with_sidecar["instructions"]
    assert without_sidecar["cfg_edges"] == with_sidecar["cfg_edges"]
    assert all("sidecar_block_id" not in block for block in without_sidecar["blocks"])

    truncated = output.with_name(output.name + ".truncated")
    offset = int(payload_offset, 0)
    bytecode_offset = with_sidecar["encrypted_bytecode_file_offset"]
    truncated.write_bytes(output.read_bytes()[:bytecode_offset + 1])
    rejected = subprocess.run([
        sys.executable, str(ORACLE), str(truncated), "--no-sidecar",
        "--architecture", architecture,
        "--runtime-code-size", str(sidecar["runtime_code_size"]),
        "--payload-file-offset", str(offset),
    ], cwd=ROOT, capture_output=True, text=True)
    assert rejected.returncode != 0 and "truncated" in rejected.stderr

    invalid_size = output.with_name(output.name + ".invalid-size")
    image = bytearray(output.read_bytes())
    first = with_sidecar["instructions"][0]
    table_start = offset + sidecar["runtime_code_size"]
    if architecture == "aarch64":
        table_start += 32 + count * 4
        if multi:
            table_start += count * 4 + 8 + 512
            size_index = first["logical_opcode"]
        else:
            header_start = offset + sidecar["runtime_code_size"]
            dispatcher_xor = int.from_bytes(image[header_start + 16:header_start + 20],
                                            "little") & 0xff
            size_index = first["physical_opcode"] ^ dispatcher_xor
    else:
        table_start += 24 + 512
        size_index = first["physical_opcode"]
    image[table_start + size_index] = 255
    invalid_size.write_bytes(image)
    rejected_size = subprocess.run([
        sys.executable, str(ORACLE), str(invalid_size), "--no-sidecar",
        "--architecture", architecture,
        "--runtime-code-size", str(sidecar["runtime_code_size"]),
        "--payload-file-offset", str(offset),
    ], cwd=ROOT, capture_output=True, text=True)
    assert rejected_size.returncode != 0 and "crosses its block boundary" in rejected_size.stderr

    control = next((instruction for instruction in with_sidecar["instructions"]
                    if instruction["explicit_target_offset"] is not None), None)
    if control is not None:
        invalid_target = output.with_name(output.name + ".invalid-target")
        image = bytearray(output.read_bytes())
        operand_index = TARGET_OFFSETS[control["logical_opcode"]] - 1
        decoded_operands = bytes.fromhex(control["operands_hex"])
        target_location = (with_sidecar["encrypted_bytecode_file_offset"] +
                           control["offset"] + TARGET_OFFSETS[control["logical_opcode"]])
        for index in range(4):
            image[target_location + index] ^= decoded_operands[operand_index + index] ^ 0xff
        invalid_target.write_bytes(image)
        rejected_target = subprocess.run([
            sys.executable, str(ORACLE), str(invalid_target), "--no-sidecar",
            "--architecture", architecture,
            "--runtime-code-size", str(sidecar["runtime_code_size"]),
            "--payload-file-offset", str(offset),
        ], cwd=ROOT, capture_output=True, text=True)
        assert rejected_target.returncode != 0 and "not a block entry" in rejected_target.stderr

    if multi:
        corrupted = output.with_name(output.name + ".invalid-owner")
        image = bytearray(output.read_bytes())
        if architecture == "aarch64":
            owner_offset = offset + sidecar["runtime_code_size"] + 32 + count * 4
            image[owner_offset] ^= 4
        else:
            owner_offset = offset + sidecar["runtime_code_size"] + 1048 + 8
            image[owner_offset:owner_offset + 4] = (7).to_bytes(4, "little")
        corrupted.write_bytes(image)
        rejected_owner = subprocess.run([
            sys.executable, str(ORACLE), str(corrupted), "--no-sidecar",
            "--architecture", architecture,
            "--runtime-code-size", str(sidecar["runtime_code_size"]),
            "--payload-file-offset", str(offset),
        ], cwd=ROOT, capture_output=True, text=True)
        assert rejected_owner.returncode != 0 and "invalid owner" in rejected_owner.stderr


def main() -> int:
    (ROOT / "build" / "tmp").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reference-oracle-", dir=ROOT / "build" / "tmp") as directory:
        temporary = Path(directory)
        arm64_input = temporary / "arm64-input"
        run("aarch64-linux-gnu-gcc", "-O2", "-static", "-fno-stack-protector",
            ROOT / "fixtures" / "generated" / "gen_branch_ops.c", "-o", arm64_input)
        check_artifact(arm64_input, "candidate_branch_ops", "aarch64",
                       temporary / "arm64-sibling")
        check_artifact(arm64_input, "candidate_branch_ops", "aarch64",
                       temporary / "arm64-branch-single", multi=False)
        run("aarch64-linux-gnu-gcc", "-O2", "-static", "-fno-stack-protector",
            ROOT / "fixtures" / "basic_arithmetic.c", "-o", arm64_input)
        check_artifact(arm64_input, "protected_candidate", "aarch64",
                       temporary / "arm64-single", multi=False)

        run(sys.executable, ROOT / "scripts" / "generate_arm32_executable_fixture.py")
        arm32_input = ROOT / "build" / "fixtures" / "arm32_vm_probe"
        check_artifact(arm32_input, "candidate_arm32_vm", "arm32",
                       temporary / "arm32-sibling")

        rv64_input = temporary / "rv64-input"
        run("riscv64-linux-gnu-gcc", "-O2", "-static", "-march=rv64imafd",
            "-mabi=lp64d", "-mno-relax", "-fno-pic", "-fno-stack-protector",
            "-Wl,--no-relax", ROOT / "tests" / "fixtures" / "rv64_integer.c",
            ROOT / "tests" / "fixtures" / "rv64_driver.c", "-o", rv64_input)
        check_artifact(rv64_input, "rv64_integer", "rv64", temporary / "rv64-sibling")
    print("reference runtime-metadata recovery passed for AArch64, ARM32, and RV64")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
