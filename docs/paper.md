# CactiVM: Selective Multi-Architecture Binary Virtualization for ELF Functions

[中文](zh-CN/paper.md) · Technical paper draft, 2 October 2026

**Author:** Yuchen Mu

> This is an implementation-grounded research manuscript, not a peer-reviewed paper or a
> claim of cryptographic security. Measurements below are reproducible fixture results,
> not a comparison against commercial protectors or a large application corpus.

## Abstract

Virtualization-based software protection replaces selected native instructions with a
bytecode program executed by an embedded interpreter. Its practical difficulty lies less
in choosing a private opcode numbering than in preserving binary interfaces, control flow,
memory effects and deployment constraints across real executables. We present CactiVM, a
function-selective ELF transformer for AArch64, ARM32/A32 and RV64. It lifts a named
function into a common intermediate representation (IR), partitions its control-flow
graph (CFG), encodes it with seed-derived VM profiles, embeds a target-native runtime and
patches the original entry. Sibling mode assigns physical blocks to two profiles and
switches their decoding domain at CFG edges. The implementation includes bounded
immediate rewriting, duplicate physical opcodes, selected superoperators, operand-window
encoding and architecture-specific native handlers. Correctness is evaluated with host
semantic tests and original-versus-protected QEMU runs. A reference attacker tool reverses
the shipped runtime format and recovers VM instructions and static CFG edges without the
optional manifest. This result demonstrates an important limit: the existing encoding
adds diversity but is not a confidentiality boundary. The contribution of this manuscript
is an auditable, cross-ISA engineering design and a measurement methodology that reports
both protection transformations and attacker-visible structure.

## 1. Introduction

Function-level protection is useful when an application has a small number of sensitive
routines and whole-program virtualization would impose unacceptable size, runtime or
compatibility cost. A practical protector must operate after linking, preserve the native
calling convention and make no assumption that every instruction family is supported.
For ELF, it must also account for symbol ranges, relocations, segment layout, executable
placement and detour reach. These requirements are especially visible when extending a
single-architecture prototype to three instruction sets.

CactiVM's unit of transformation is a named, file-backed function in a little-endian ELF.
The input remains unchanged; the output is another executable or shared object. The
protector does not require an LLVM pass or a custom loader. It rejects an unsupported
native instruction or unsafe binary range instead of passing that operation through an
unmodeled VM instruction. This scope is deliberate: **the system protects selected
functions, not all code in an ELF**, and it does not claim to protect data once a capable
analyst observes plaintext execution state.

The engineering contributions are:

1. A common IR and bytecode contract shared by three target-specific decoders, ELF
   patchers and native runtimes.
2. A layered, seed-derived encoding in which build, block and instruction choices can
   differ, including sibling-profile ownership of CFG blocks.
3. A fail-closed binary publication path and tests spanning host semantics, native ABIs,
   large ELF placement and QEMU differential execution.
4. An attacker-side oracle that quantifies what the current runtime format exposes
   without a manifest, rather than equating ciphertext entropy with resistance to
   devirtualization.

The implementation and its scope are described by the [architecture](architecture.md),
[obfuscation](obfuscation.md), [security model](security-model.md) and
[analysis guide](analysis-and-reports.md). This paper states their central contracts and
the evidence supporting them.

## 2. Threat model and claims

The defender controls the input ELF, symbol choice, protection seeds and build process.
The attacker obtains a protected ELF, can disassemble it, run or instrument it, and knows
the published source code and format. An optional `.myvmp.json` sidecar is a diagnostic
artifact and should not be distributed when its profile and CFG metadata are sensitive.
We do not assume secrets can be hidden in a client-side runtime. Seeds are reproducibility
and diversification inputs, not secret cryptographic keys.

The intended property is an increased cost of mapping protected machine code to the
original function's semantics. We do **not** claim semantic indistinguishability,
unbreakable obfuscation, anti-debugging, tamper resistance or a lower bound on an
analyst's work. A reference oracle already recovers a useful part of the VM structure.
This attacker result is part of the evaluation, not an exception hidden from it.

## 3. System architecture

```text
  linked ELF + selected symbol
              |
       ELF/range/relocation checks
              |
       ISA decoder and lifter  <--- AArch64 / A32 / RV64
              |
          common typed IR
              |
       CFG + liveness analysis
              |
  profile generation + block ownership
              |
   bytecode encoding + native VM runtime
              |
       ELF placement + entry detour
              |
  protected ELF + optional sidecar
```

The ELF frontend validates class, machine, section/segment ranges, symbol bounds and
file-backed executable bytes. The ISA frontend decodes the complete selected function
at its actual virtual address; it lifts registers, flags, memory, control flow, calls and
the explicitly supported floating-point/vector operations into typed IR. A failure in
any instruction or relocation class rejects the function. The common IR is then
validated for register widths, targets and exits.

The bytecode layer identifies leaders, builds blocks and remaps control targets after
expansion and physical placement. The protector can reorder non-entry blocks while
retaining explicit VM branches and call continuations. The runtime is target-native
assembly bundled as a checked-in byte blob generated from auditable sources. A host VM
interpreter provides an independent semantic test path. Target-specific patchers decide
near/far detour form and update ELF layout; AArch64 and RV64 share a parameterized ELF64
layout engine, whereas ARM32 uses its ELF32/A32 path. Publication of multi-function
protection is staged so a failed selection does not leave a partially protected output.

### 3.1 Native execution boundary

At a protected entry, the detour transfers control to the runtime, which saves the
target ABI state, establishes a VM frame, locates the current physical block, selects its
profile and dispatches a handler. Native call gates synchronize the register/stack state
needed by unvirtualized callees, then resume VM execution. The original body is replaced
by seed-derived trap fill after the entry patch. This is a native ELF rewrite: ordinary
unselected code still runs as compiled.

### 3.2 Architecture support is explicit

The shipped scalar baselines cover AArch64, ARM32 in A32 state, and RV64. AArch64 also
has a tested fixed-width 128-bit Advanced SIMD subset, including vector load/store,
integer/bitwise operations, selected comparisons, shifts, permutation and FP32/FP64
arithmetic with native-call ABI tests. This is not a claim of all NEON, SVE/SME, RVV,
Thumb or ARM32 VFP/NEON support. Unknown or unsafe forms fail closed. The precise
supported instruction boundary lives in decoder/runtime tests, not in a generic
architecture label.

## 4. Protection representation

### 4.1 Logical ISA and profiles

IR operations lower to a shared logical VM ISA. A `VmProfile` derives a physical opcode
assignment from a nonzero seed; common operations may have duplicate physical encodings
selected per occurrence. The profile also varies dispatcher comparison constants, native
handler variants, immediate reconstruction templates, block layout and fill bytes. Thus
the deployed byte sequence is not merely the source instruction sequence with permuted
opcode numbers. The logical operation set and operand-size contract are, however, still
common across functions. Function-specific logical semantics and independently synthesized
handler families are research directions, not deployed capabilities.

### 4.2 Operand and payload encoding

Each VM instruction declares a central operand width. Before execution, the runtime
decodes an operand window using key material, current physical offset, opcode domain,
owner and operand-byte index; it re-encodes the window before the next dispatch. A
separate payload transform and, on AArch64, rolling block transform diversify bytes by
position and block. Immediate values may be reconstructed from bounded XOR or modular
shares. These layers reduce direct literal and instruction-byte exposure. They are
**reversible encodings**: the runtime must contain the key and tables, and the published
reference oracle reverses them. They should not be described as encryption against an
attacker who owns the binary.

### 4.3 CFG and sibling profiles

The encoder gives each logical block a stable identity while allowing a different
physical ordering. Sibling mode assigns blocks to one of two seeded profiles. The
initial ownership algorithm uses CFG, loop, dominator and liveness information alongside
seeded variation; the entry remains anchored. Each static cross-owner edge has a
validated `VmBridge` recording source/destination profile, target/continuation, live
state policy and other execution metadata. The native runtime selects the active
translation/handler domain from the destination block's owner. In the current native
runtime, sibling profiles share a full execution frame. A host interpreter additionally
tests reversible, profile-local register/flag/vector/SP representations and live-state
transfers, but target assembly does not yet execute these representation transforms.

```text
  logical CFG:       B0 ----> B1 ----> B3
                      \      /
                       -> B2

  physical layout:   [B0:A] [B3:A] [B1:B] [B2:B]
                        |       ^       |
                        +-- bridge -----+

  A/B are profile owners, not separate operating-system processes.
  Arrows describe logical edges; byte offsets follow physical layout.
```

Bounded superoperators combine only eligible same-block compare/test and branch
patterns. They do not cross targets, calls or memory-order boundaries. This restriction
keeps behavioral equivalence testable and avoids obscuring an unsupported side effect.

## 5. Correctness and fail-closed design

The transformation is accepted only when native decoding, IR validation, bytecode
targets, VM ownership, detour reach and ELF placement satisfy their contracts. Unknown
opcodes and malformed runtime metadata enter explicit failure paths. The host VM executes
logical bytecode for rapid semantic tests; real protected ELFs are then run under QEMU
against originals. Test layers include CLI error contracts, runtime-blob regeneration,
cross-toolchain builds, multi-seed checks, native call/ABI cases, ARM32 and RV64
behavior, AArch64 SIMD differential cases and large-ELF far-detour stress. These tests
are evidence for the checked cases, not a proof over all programs or compiler outputs.

## 6. Attacker-side analysis

An evaluator should not treat high entropy or low byte overlap as evidence that a
devirtualizer cannot recover structure. We therefore maintain
[`reference_devirtualizer.py`](https://github.com/TephrocactusMYC/CactiVM/blob/master/scripts/reference_devirtualizer.py), an oracle for the
current shipped format. Given the protected ELF, architecture, runtime start and code
size, it parses native runtime tables and block ranges, reverses payload/operand
transforms, identifies VM instruction boundaries and logical opcodes, and reconstructs
explicit branch targets plus statically inferable fallthrough edges. It does not require
the sidecar. With one, it compares recovered boundaries, owners and CFG edges to ground
truth. Its regression suite includes AArch64 single and sibling modes, ARM32 and RV64,
plus malformed size, target and owner checks.

For AArch64 single-VM mode, mapping a physical opcode to a logical one additionally
uses patched native dispatcher comparisons and the comparison order in this open-source
repository. Sibling AArch64 and ARM32/RV64 use exposed runtime translation tables. The
oracle does **not** model handler effects, dynamic VM-return edges, or lift bytecode back
to source-level semantic IR. A logical VM opcode count is therefore not an IR recovery
count. This distinction defines a concrete next benchmark: recover handler semantics
and compare normalized IR/CFG across profiles and compiler versions.

## 7. Evaluation and interpretation

The checked-in [benchmark snapshot](readme-benchmarks.md) was captured on Ubuntu 22.04
with GCC cross-toolchains and QEMU 6.2. Each listed protected ELF had stdout compared
byte-for-byte with its original. The byte-quality sample compares a selected native
function range to concatenated encrypted VM block ranges, **not** entire files.

| Fixture | Native bytes | VM payload bytes | Payload entropy (bits/byte) | Identical QEMU stdout |
|---|---:|---:|---:|---|
| ARM32/A32 VM probe | 68 | 264 | 7.241544 | yes |
| AArch64 branch operations | 76 | 197 | 7.081294 | yes |
| RV64 loop | 32 | 141 | 6.627036 | yes |

For these three fixtures, same-position and tested literal/word exposure ratios were
zero. These are small examples and do not establish resistance to semantic recovery.
The ELF packaging cost also varies: in the snapshot, the AArch64 one-function case grew
by 65,536 bytes; RV64 grew by 7,857 bytes for one selected function and 16,375 bytes for
three; the ARM32 example reused existing space and showed no file-size growth. A
135,275,392-byte AArch64 PIE far-detour case also grew by 65,536 bytes and preserved
observed QEMU stdout. Runtime slowdowns are not reported in this snapshot; no general
performance claim follows from it.

The attacker oracle gives a contrasting observation. For its sibling-mode integration
fixtures (seeds `12345`/`67890`), it recovered 27 VM instructions and all 6 sidecar CFG
edges on AArch64, 37 instructions and 6/6 edges on ARM32, and 29 instructions and 2/2
edges on RV64. These exact matches show that diversification and reversible encoding
did not conceal that structure in these cases. The fixtures are too small to establish
cross-sample attack cost or a statistical security result.

## 8. Limitations, validity and research agenda

The current native system has two sibling profiles rather than N-way heterogeneous VM
models. Profiles vary encoding and handlers but share the logical ISA and native VM frame.
Host-only state-representation experiments and a bounded sequence miner must not be
confused with generated native state bridges or synthesized operators. AArch64 SIMD is a
defined subset, and ARM32/RV64 vector extensions are not implied. Shared objects, TLS,
unwinding, unusual relocations, new compiler idioms and very large production binaries
need broader qualification. Selected symbols must be known and safely patchable.

Internal validity depends on the fixtures, compiler flags and QEMU behavior checks.
External validity is limited by the absence of large independent application corpora,
human reverse-engineering studies, and measured devirtualizer time/cost. Our reference
oracle is format-aware and open-source, so its success does not quantify how fast an
analyst with no source could reproduce it. Conversely, its failure to lift semantic IR
does not prove that such lifting is hard. Future experiments should report recovered
source-level IR/CFG, trace-shape alignment, handler clustering, tool runtime and human
effort alongside size and protected-program overhead.

## 9. Related work and ethics

Virtualization and attacker normalization are discussed in the
[SoK on automatic deobfuscation](https://eprints.cs.univie.ac.at/7012/1/3465481.3465772.pdf).
[Tigress's virtualization documentation](https://tigress.wtf/virtualize.html) describes
design options such as duplicate instructions and superoperators. These are conceptual
references, not a claim that CactiVM reproduces either implementation. Binary rewriting
and platform correctness are informed by [E9Patch](https://doi.org/10.1145/3316415),
the [Arm ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)
and the [RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/).
The full design bibliography is in [research notes](research.md).

Software protection is dual-use: the same transformations can conceal malicious logic.
This project publishes format details, limitations and an attacker-side oracle to make
security claims falsifiable. Users remain responsible for applying it only to software
they are authorized to modify and for preserving legal and operational auditability.

## 10. Reproducibility

The repository is GPL-3.0-only. Start with [build instructions](build-and-platforms.md),
then run `make release-check`, `make reference-devirtualizer-test` and the relevant
architecture/QEMU targets in [testing](testing.md). The exact benchmark environment,
inputs, selections and values are in the checked-in
[JSON evidence](examples/quality-benchmarks.json). `protect` sidecars preserve seeds,
profile and block metadata for controlled reproduction; they should be handled as
diagnostic artifacts. A complete experiment should archive input/output hashes,
toolchain and QEMU versions, selected symbols, flags, seeds, sidecars, observed behavior
and machine-readable analysis reports.

## 11. Conclusion

CactiVM demonstrates a modular, selective binary virtualization pipeline across three
ELF architectures, with explicit correctness gates and real-runtime execution tests.
Its layered profiles and sibling ownership change the protected execution surface, but
the present runtime format leaves VM instructions and static CFG recoverable. The result
supports a narrower and more useful conclusion than “strong obfuscation”: the system is
deployable for qualified selected functions, while meaningful claims about analysis
resistance require semantic attacker baselines, larger corpora and measured costs.
