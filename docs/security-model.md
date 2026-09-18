# Security Model and Limitations

[中文](zh-CN/security-model.md)

## Intended threat model

myVMP raises the cost of static inspection and routine patching of selected user-mode
functions in Linux ELF binaries. It assumes an analyst can possess and execute the
protected file, inspect memory, trace instructions and compare samples. The objective is
diversity and analysis friction, not secrecy against an all-powerful local adversary.

## What the design provides

- selected native semantics are replaced by encrypted, encoded VM instructions;
- source/VM correspondence changes across seeds and physical blocks;
- original native body bytes are destroyed after a validated detour is installed;
- sibling mode changes VM ownership dynamically across CFG edges;
- manifests and deterministic seeds support auditing and reproducible diagnosis;
- malformed/unsupported inputs fail before final publication;
- QEMU and host regressions continuously check semantic preservation.

## What it does not provide

- a proof against devirtualization, symbolic execution or dynamic tracing;
- anti-debugging, anti-dump, remote attestation or a secure key store;
- confidentiality once plaintext state/semantics are observed during execution;
- automatic protection of every compiler output, kernel code or privileged instructions;
- memory-safety hardening of the protected application itself;
- tamper-proof manifests or signed releases.

An analyst can eventually model fetch/decode/dispatch/handler behavior. Multi-VM and
polymorphism make normalization more expensive, but complexity is not equivalent to a
cryptographic guarantee.

## Fail-closed boundaries

Protection is rejected for unsupported machines/classes, missing or zero-sized symbols,
unknown instructions, unsafe relocation ranges, unrepresentable branches, invalid CFG or
VM metadata, payload/runtime limits and inconsistent sibling partitions. ARM32/A32 rejects
Thumb, VFP/NEON and privileged families; sibling multi-VM is supported. x86-64 target
protection is not implemented.

The exact instruction boundary is defined by decoder/lifter code and release fixtures.
AArch64 includes the documented, regression-tested fixed 128-bit Advanced SIMD subset;
other NEON forms, SVE/SVE2/SME, RV64 Vector and platform-specific families must not be
assumed supported merely because a runtime preserves vector register state.

## Operational recommendations

- Protect narrowly chosen high-value functions, not an entire very large application.
- Keep unsupported/environmental operations in small native helpers reached through
  tested call gates.
- Use multiple representative seeds during qualification and one controlled seed per
  release build.
- Compare complete behavior under realistic inputs, concurrency and error paths.
- Treat sidecars as sensitive diagnostic metadata when they expose build profiles.
- Sign final artifacts using an external release system and retain unprotected symbols
  only as long as needed for protection/debugging.
- Measure runtime and size on production-like hardware; QEMU ratios are regression data,
  not capacity planning.

## Reporting issues

A useful defect report contains target architecture, compiler/linker versions, minimal
source or redistributable ELF, symbol name, exact command and seed, dry-run logs, sidecar,
original/protected outputs and the smallest behavior mismatch. Do not publish proprietary
binaries or secret production seeds.
