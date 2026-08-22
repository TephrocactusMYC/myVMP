# Research Notes and References

[中文](zh-CN/research.md)

This is a design bibliography, not a claim that myVMP copies any product or source. The
project implements its own C++ architecture, IR, bytecode, runtimes and ELF transformation.
References were used to understand established designs, attacker workflows, ABI rules and
measurement pitfalls. License terms of referenced code must be reviewed independently;
no external source should be copied without compatible licensing and provenance.

## Virtualization protection and devirtualization

- **SoK: Automatic Deobfuscation of Virtualization-protected Applications** — attacker
  models for recovering fetch/decode/dispatch/handler structure and normalizing VM traces;
  motivated per-block/profile diversity and honest limits on multi-VM complexity.
  [Paper](https://eprints.cs.univie.ac.at/7012/1/3465481.3465772.pdf)
- **Matryoshka / process-level nested virtualization** — evidence that nesting can raise
  analysis cost while imposing substantial overhead; motivated sibling VMs before
  unrestricted recursive nesting.
  [Slides](https://cs.anu.edu.au/cybersec/issisp2018/assets/slides/Davidson_PVM_Obfuscation_Slides.pdf)
- **Tigress Virtualize** — duplicate instructions, operand-location variation,
  superoperators, dispatch diversity and reentrant interpreters; informed bounded,
  testable polymorphism rather than a single opcode permutation.
  [Documentation](https://tigress.wtf/virtualize.html)
- **Recent obfuscation/devirtualization literature reviewed during quality planning** —
  used as prompts for attacker-oriented measurement and evolving VM signatures, not as
  implementation specifications.
  [arXiv:2601.10261](https://arxiv.org/abs/2601.10261),
  [arXiv:2603.18355](https://arxiv.org/abs/2603.18355),
  [SSRN 5147457](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=5147457),
  [IEEE Access reference](https://colab.ws/articles/10.1109%2Faccess.2021.3094517)

## Open-source virtualization and binary protection

- **VMPacker** — ARM64 Linux ELF function virtualization, custom bytecode, opcode
  randomization, encrypted/reordered bytecode and injection workflow. It reinforced the
  value of a binary-level frontend and strict module separation.
  [Repository](https://github.com/LeoChen-CoreMind/VMPacker)
- **xVMP** — LLVM-level virtualization with per-basic-block ideas; informed block identity,
  block-local crypto domains and quality comparisons while myVMP remains binary-level.
  [Repository](https://github.com/GANGE666/xVMP)
- **binprotect** — x64 PE binary virtualization and per-handler layout diversity; useful
  attacker/implementation reference but not a reusable ARM ELF design.
  [Repository](https://github.com/noahware/binprotect)
- **Kiteshield** — x86-64 ELF packing and function encryption; informed separation between
  packing/encryption and semantic VM virtualization.
  [Repository](https://github.com/GunshipPenguin/kiteshield)
- **covirt** — VM/MBA/self-modifying experiments and documented VM-call failure modes;
  reinforced the need for an explicit, validated bridge ABI.
  [Repository](https://github.com/dmaivel/covirt)
- **Miasm** — mature lifting and analysis concepts; useful as a future differential oracle,
  while myVMP retains a compact project-owned IR and no Python runtime dependency.
  [Repository](https://github.com/cea-sec/miasm)

Community analyses of commercial protectors, including VMProtect-protected samples, were
used to study recognizable dispatcher loops, handler normalization, rolling decode state,
operand recovery and trace-based attacks. These analyses motivated changing representation
at build, block, instruction and handler levels. myVMP does not claim compatibility with,
or reproduction of, VMProtect internals.

## Binary rewriting

- **E9Patch** — practical static binary rewriting and placement constraints.
  [Repository](https://github.com/GJDuck/e9patch),
  [paper](https://doi.org/10.1145/3316415)
- **LIEF** — ELF object model and binary transformation API; useful comparison for segment,
  section and dynamic-table repair.
  [Repository](https://github.com/lief-project/LIEF),
  [ELF API](https://lief.re/doc/stable/doxygen/classLIEF_1_1ELF_1_1Binary.html)
- Binary rewriting research on safe patch placement and layout informed fail-closed
  planning and real-ELF stress tests.
  [DOI 10.1145/3546568](https://doi.org/10.1145/3546568),
  [STTT article](https://link.springer.com/article/10.1007/s10009-021-00644-w)

## Architecture and ABI standards

- [Arm AArch64 ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)
- [RISC-V unprivileged ISA](https://docs.riscv.org/reference/isa/unpriv/rv64.html)
- [RISC-V compressed extension](https://docs.riscv.org/reference/isa/v20260120/unpriv/c-st-ext.html)
- [RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)
- [x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI) and
  [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
  are preparatory references for a future x86-64 backend.

Standards, rather than protector writeups, are authoritative for instruction semantics,
relocations, calling conventions and ELF behavior.

## Testing methodology

- [Csmith](https://github.com/csmith-project/csmith) informed the optional randomized
  compiler-output stress layer. Deterministic checked-in fixtures remain primary because
  failures must be reproducible and minimized.
- Attacker-oriented review asks whether an analyst can identify a global opcode map,
  plaintext operand stream, stable block order, uniform handler template or bridge ABI.
  Each question maps to a measurable diversity or correctness gate.

## Ideas adopted

1. Function-level binary virtualization with a project-owned IR.
2. Per-build and per-occurrence physical opcode diversity.
3. Unified rolling operand encoding across the logical opcode set.
4. CFG-aware blocks, encrypted key evolution and movable physical layout.
5. Bounded superoperators and native handler variants.
6. Explicit sibling-VM ownership and bridge contracts instead of ad-hoc recursion.
7. Transactional ELF publication and fail-closed validation.
8. Reproducible manifests plus behavior, diversity, exposure and cost measurement.

## Ideas deliberately deferred or rejected

- treating high entropy as proof of protection;
- silently passing unknown native instructions through the VM;
- unrestricted mutually recursive interpreters without a state/exception ABI;
- whole-program virtualization as the default deployment model;
- copying proprietary formats or incompatible open-source implementations.
