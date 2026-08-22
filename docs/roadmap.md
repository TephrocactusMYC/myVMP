# Roadmap

[中文](zh-CN/roadmap.md)

The AArch64 and RV64 scalar function-protection baselines, dynamic operand codec,
CFG-aware bytecode, sibling multi-VM, real-ELF behavior suites and large-placement stress
tests are implemented. The next priorities are intentionally concise.

## Release readiness

- publish GPL-3.0-only versioned releases using the documented signed-tag/checksum policy;
- add reproducible-build evidence and CI across supported toolchains;
- stabilize machine-readable CLI schemas and diagnostic error categories;
- add packaged-tool smoke tests and compatibility fixtures for multiple compiler versions.
- consider native Windows/macOS host ports only when demand justifies maintaining ELF and
  target-tool compatibility layers; WSL2/container workflows are the supported baseline.

## Correctness and platform breadth

- expand optional AArch64/RV64 SIMD/vector support behind explicit semantic and ABI tests;
- extend remaining uncommon atomics, relocations and compiler-specific scalar forms;
- strengthen shared-object, TLS, exception/unwind and very-large-application qualification;
- decide whether ARM/A32 remains experimental or receives a broader maintained scope.

## Quality and analysis resistance

- add more independently structured dispatch/handler families with measured signature
  reduction;
- experiment with selective nested protection only if it materially improves attacker
  workload beyond sibling mode at acceptable cost;
- add dynamic trace-shape, recovered-CFG and handler-clustering metrics;
- improve automated report comparison across releases, compilers and seeds.

## New architectures

- design an x86-64 frontend/runtime using the common IR, profile, codec, manifest and ELF64
  writer contracts;
- keep ISA semantics and detours target-specific rather than contaminating common bytecode;
- consider other executable formats only after ELF architecture boundaries remain clean.

Roadmap items are not support promises. A feature becomes supported only after host
semantics, target-runtime behavior, malformed-input handling and release documentation all
pass together.
