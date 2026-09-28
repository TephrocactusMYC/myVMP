#!/usr/bin/env python3
"""Report attacker-visible VM structure from myVMP sidecars.

The report measures metadata observability and structural proxies. It does not
execute bytecode, infer handler semantics, or claim to perform devirtualization.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def canonical_hash(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return value


def resolve_sidecar(path: Path) -> tuple[Path | None, Path | None]:
    """Return (sidecar, protected ELF); raw ELF without sidecar is still reportable."""
    if path.name.endswith(".myvmp.json"):
        return path, None
    candidate = Path(str(path) + ".myvmp.json")
    if candidate.is_file():
        return candidate, path
    return None, path


def profile_view(profile: Any) -> dict[str, Any]:
    if not isinstance(profile, dict):
        return {"available": False, "reason": "profile absent"}
    opcodes = profile.get("opcodes")
    if not isinstance(opcodes, dict):
        opcodes = {}
    variants = profile.get("handler_variants")
    if not isinstance(variants, dict):
        variants = {}
    dispatcher = profile.get("dispatcher")
    if not isinstance(dispatcher, dict):
        dispatcher = {}
    signature = {
        "opcodes": opcodes,
        "handler_variant_mask": profile.get("handler_variant_mask"),
        "handler_variants": variants,
        "dispatcher": dispatcher,
        "duplicate_opcodes": profile.get("duplicate_opcodes", {}),
        "superoperators": profile.get("superoperators", {}),
    }
    handler_fingerprints = {
        str(name): canonical_hash({
            "logical_handler": name,
            "physical_opcode": physical,
            "handler_variant_mask": profile.get("handler_variant_mask"),
            "handler_variants": variants,
            "dispatcher_organization": dispatcher.get("organization"),
        })
        for name, physical in sorted(opcodes.items())
    }
    return {
        "available": True,
        "seed": profile.get("seed"),
        "logical_opcode_count_listed": len(opcodes),
        "physical_opcode_values_unique": len(set(v for v in opcodes.values()
                                                    if isinstance(v, int))),
        "mapping_fingerprint_sha256": canonical_hash(signature),
        "dispatcher_organization": dispatcher.get("organization"),
        "handler_variant_mask": profile.get("handler_variant_mask"),
        "handler_variant_enabled_count": sum(v is True for v in variants.values()),
        "duplicate_opcode_alias_count": len(profile.get("duplicate_opcodes", {})),
        "superoperator_count_listed": len(profile.get("superoperators", {})),
        "handler_fingerprints": handler_fingerprints,
        "_fingerprint": signature,
    }


def compute_dominators(block_ids: list[int], edges: dict[int, set[int]], entry: int
                       ) -> dict[int, set[int]]:
    nodes = set(block_ids)
    predecessors = {node: set() for node in nodes}
    for source, targets in edges.items():
        for target in targets:
            if target in predecessors:
                predecessors[target].add(source)
    dom = {node: ({node} if node == entry else set(nodes)) for node in nodes}
    changed = True
    while changed:
        changed = False
        for node in block_ids:
            if node == entry:
                continue
            preds = predecessors[node]
            common = set.intersection(*(dom[pred] for pred in preds)) if preds else set()
            updated = {node} | common
            if updated != dom[node]:
                dom[node] = updated
                changed = True
    return dom


def block_graph(manifest: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[int, set[int]]]:
    raw_blocks = manifest.get("blocks")
    if not isinstance(raw_blocks, list):
        raise ValueError("sidecar has no blocks array")
    blocks: list[dict[str, Any]] = []
    edges: dict[int, set[int]] = {}
    ids: set[int] = set()
    for raw in raw_blocks:
        if not isinstance(raw, dict) or not isinstance(raw.get("id"), int):
            raise ValueError("sidecar contains a malformed block")
        node = dict(raw)
        node_id = node["id"]
        if node_id in ids:
            raise ValueError(f"duplicate block id {node_id}")
        ids.add(node_id)
        blocks.append(node)
        successors = node.get("successors", [])
        if not isinstance(successors, list) or any(not isinstance(v, int) for v in successors):
            raise ValueError(f"block {node_id} has malformed successors")
        edges[node_id] = set(successors)
    missing = sorted({target for targets in edges.values() for target in targets} - ids)
    if missing:
        raise ValueError(f"CFG references absent block IDs: {missing}")
    return blocks, edges


def trace_ids(path: Path | None) -> list[int] | None:
    if path is None:
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read trace JSON {path}: {error}") from error
    ids = value.get("block_ids") if isinstance(value, dict) else value
    if not isinstance(ids, list) or any(not isinstance(item, int) for item in ids):
        raise ValueError("trace must be a JSON array of block IDs or an object with block_ids")
    return ids


def analyze(path: Path, trace_path: Path | None) -> dict[str, Any]:
    sidecar_path, elf_path = resolve_sidecar(path)
    if sidecar_path is None:
        result: dict[str, Any] = {
            "input": str(path),
            "analysis_status": "opaque_without_sidecar",
            "evidence_class": "artifact_metadata_only",
            "devirtualization_performed": False,
            "limitation": "No adjacent .myvmp.json sidecar; encrypted payload and ELF alone do not reveal CFG/profile fields to this tool.",
        }
        if elf_path is not None and elf_path.is_file():
            data_hash = hashlib.sha256()
            with elf_path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    data_hash.update(chunk)
            result["protected_elf"] = {"size_bytes": elf_path.stat().st_size,
                                       "sha256": data_hash.hexdigest()}
        return result

    manifest = load_json(sidecar_path)
    blocks, edges = block_graph(manifest)
    ordered_ids = [block["id"] for block in blocks]
    owner = {block["id"]: block.get("profile_id") for block in blocks}
    profile_values = [manifest.get("profile")]
    if isinstance(manifest.get("secondary_profile"), dict):
        profile_values.append(manifest["secondary_profile"])
    profiles = [profile_view(profile) for profile in profile_values]
    clean_profiles = [{key: value for key, value in profile.items() if key != "_fingerprint"}
                      for profile in profiles]

    edge_count = sum(len(targets) for targets in edges.values())
    entry = ordered_ids[0] if ordered_ids else None
    reachable: set[int] = set()
    if entry is not None:
        pending = [entry]
        while pending:
            current = pending.pop()
            if current in reachable:
                continue
            reachable.add(current)
            pending.extend(edges[current] - reachable)
    dom = compute_dominators(ordered_ids, edges, entry) if entry is not None else {}
    back_edges = sorted((source, target) for source, targets in edges.items()
                        for target in targets if target in dom.get(source, set()))
    owner_edge_counts: dict[str, int] = {}
    cross_profile_edges = []
    for source, targets in edges.items():
        for target in targets:
            key = f"{owner.get(source)}->{owner.get(target)}"
            owner_edge_counts[key] = owner_edge_counts.get(key, 0) + 1
            if owner.get(source) != owner.get(target):
                cross_profile_edges.append([source, target])

    encoded_owners = [owner.get(node) for node in ordered_ids]
    owner_runs: list[dict[str, Any]] = []
    for node, profile_id in zip(ordered_ids, encoded_owners):
        if owner_runs and owner_runs[-1]["profile_id"] == profile_id:
            owner_runs[-1]["block_ids"].append(node)
        else:
            owner_runs.append({"profile_id": profile_id, "block_ids": [node]})

    supplied_trace = trace_ids(trace_path)
    trace_result: dict[str, Any] = {
        "observed": supplied_trace is not None,
        "metric_kind": "observed_trace" if supplied_trace is not None else "static_cfg_proxy",
    }
    if supplied_trace is not None:
        invalid_trace_ids = sorted(set(supplied_trace) - set(owner))
        trace_edges = list(zip(supplied_trace, supplied_trace[1:]))
        trace_result.update({
            "block_visit_count": len(supplied_trace),
            "unique_blocks_visited": len(set(supplied_trace)),
            "unknown_block_ids": invalid_trace_ids,
            "owner_run_count": (sum(index == 0 or owner.get(block) != owner.get(supplied_trace[index - 1])
                                     for index, block in enumerate(supplied_trace))),
            "observed_transition_count": len(trace_edges),
            "observed_transitions_in_cfg": sum(target in edges.get(source, set())
                                                for source, target in trace_edges),
        })

    bridges = manifest.get("bridges", [])
    if not isinstance(bridges, list):
        bridges = []
    # Hide seed values from comparisons; fingerprints describe exposed profile structure.
    fingerprints = [profile["mapping_fingerprint_sha256"] for profile in profiles
                    if profile.get("available")]
    result = {
        "input": str(path),
        "sidecar": str(sidecar_path),
        "analysis_status": "sidecar_analyzed",
        "schema": "myvmp-attacker-metrics-v1",
        "evidence_class": "sidecar_observable_plus_static_proxies",
        "devirtualization_performed": False,
        "manifest": {
            "architecture": manifest.get("architecture"),
            "mode": manifest.get("mode"),
            "payload_encrypted": manifest.get("payload_encrypted"),
            "block_count": len(blocks),
            "bridge_count_declared": manifest.get("bridge_count", len(bridges)),
        },
        "profile_observability": {
            "profiles": clean_profiles,
            "unique_profile_fingerprints": len(set(fingerprints)),
            "profile_count": len(profiles),
            "cross_sample_profile_reuse": "computed in comparison section when multiple inputs are given",
        },
        "operand_grammar_observability": {
            "classification": "implementation_known_global_contract",
            "per_function_grammar_in_manifest": False,
            "per_opcode_operand_sizes_in_manifest": False,
            "cross_function_format_reuse_proxy": 1.0,
            "interpretation": "The sidecar identifies codecs, while the implementation uses a shared operand_size contract; this is an implementation-level observability finding, not recovered payload decoding.",
        },
        "cfg_recovery_proxy": {
            "metric_kind": "manifest_graph_exact_metadata_proxy",
            "block_ids_and_successors_exposed_by_sidecar": True,
            "node_count": len(blocks),
            "edge_count": edge_count,
            "reachable_from_entry_count": len(reachable),
            "unreachable_block_count": len(blocks) - len(reachable),
            "cyclomatic_complexity_proxy": max(0, edge_count - len(blocks) + (1 if blocks else 0)),
            "dominator_back_edge_count": len(back_edges),
            "dominator_back_edges": [list(edge) for edge in back_edges],
            "owner_edge_counts": dict(sorted(owner_edge_counts.items())),
            "cross_profile_cfg_edge_count": len(cross_profile_edges),
            "cross_profile_cfg_edges": [list(edge) for edge in sorted(cross_profile_edges)],
            "bridge_records": len(bridges),
            "bridge_edge_coverage_proxy": (len(bridges) / len(cross_profile_edges)
                                            if cross_profile_edges else None),
            "encoded_block_owner_runs": owner_runs,
            "warning": "These values measure metadata exposure and graph shape; they do not estimate the effort to recover hidden semantics from ciphertext or execution.",
        },
        "trace_shape": trace_result,
        "profile_fingerprint": canonical_hash(fingerprints),
    }
    if elf_path is not None and elf_path.is_file():
        digest = hashlib.sha256()
        with elf_path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        result["protected_elf"] = {"size_bytes": elf_path.stat().st_size,
                                   "sha256": digest.hexdigest()}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="sidecar JSON or protected ELF (with adjacent .myvmp.json)")
    parser.add_argument("--trace", type=Path,
                        help="optional JSON block trace: [block_id, ...] or {\"block_ids\": [...]}; applies to one input")
    parser.add_argument("--output", type=Path, help="write JSON to a file instead of stdout")
    args = parser.parse_args()
    if args.trace is not None and len(args.inputs) != 1:
        parser.error("--trace can only be used with one input")
    try:
        reports = [analyze(path, args.trace) for path in args.inputs]
        cross_sample: dict[str, Any] = {"sample_count": len(reports)}
        if len(reports) > 1:
            fingerprints = [report.get("profile_fingerprint") for report in reports
                            if report.get("profile_fingerprint")]
            handler_fingerprints = [fingerprint
                                    for report in reports
                                    for profile in report.get("profile_observability", {}).get("profiles", [])
                                    for fingerprint in profile.get("handler_fingerprints", {}).values()]
            cross_sample.update({
                "analyzed_sample_count": len(fingerprints),
                "unique_profile_fingerprints": len(set(fingerprints)),
                "fingerprint_reuse_ratio": (1.0 - len(set(fingerprints)) / len(fingerprints)
                                             if fingerprints else None),
                "handler_fingerprint_instances": len(handler_fingerprints),
                "unique_handler_fingerprints": len(set(handler_fingerprints)),
                "handler_fingerprint_reuse_ratio": (
                    1.0 - len(set(handler_fingerprints)) / len(handler_fingerprints)
                    if handler_fingerprints else None),
                "interpretation": "Fingerprint equality is a structural reuse proxy; inequality does not establish resistance to normalization.",
            })
        document = {"schema": "myvmp-attacker-metrics-v1",
                    "metric_warning": "Proxy metrics only; this tool is not a reference devirtualizer and does not measure semantic recovery cost.",
                    "cross_sample_profile_reuse": cross_sample,
                    "reports": reports}
        text = json.dumps(document, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        else:
            print(text, end="")
    except (OSError, ValueError) as error:
        parser.exit(2, f"vm attacker metrics failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
