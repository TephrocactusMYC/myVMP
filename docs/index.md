# Documentation

[中文导航](zh-CN/index.md)

This documentation describes the current implementation, not an aspirational design.
Start with the usage guide if you want to protect a binary; read the architecture and
security model before integrating myVMP into a release pipeline.

## User documentation

- [Usage](usage.md) — single-function, multi-VM and batch workflows; CLI reference.
- [Build and platforms](build-and-platforms.md) — host requirements, cross toolchains,
  supported targets and release packaging.
- [Analysis and reports](analysis-and-reports.md) — dry runs, manifests, quality metrics,
  automated report bundles and interpretation.
- [Published benchmark snapshot](readme-benchmarks.md) — real cross-architecture quality,
  size and QEMU behavior results used by the README.
- [Testing](testing.md) — unit, QEMU, large-ELF, diversity and release gates.

## Design documentation

- [Architecture](architecture.md) — modules, data flow, ELF transformation and runtime ABI.
- [Obfuscation](obfuscation.md) — dynamic profiles, operand codec, block encryption,
  polymorphism, superoperators and sibling multi-VM.
- [Security model](security-model.md) — threat model, guarantees, fail-closed boundaries
  and limitations.
- [Research and references](research.md) — reviewed projects, papers, standards and the
  concrete ideas derived from them.
- [Roadmap](roadmap.md) — concise post-release priorities.
- [Android native protection vision](android-vision.md) — research-only feasibility,
  integration route, reuse analysis and decision gates for a future NDK `.so` target.

## Project governance

- [Changelog](../CHANGELOG.md)
- [Contributing](../CONTRIBUTING.md)
- [Security policy](../SECURITY.md)
- [GPL-3.0-only license](../LICENSE)

## Source map

```text
include/myvmp/       public module interfaces
src/{arm64,riscv64}  target instruction decoding and lifting
src/ir               architecture-neutral semantic IR
src/bytecode         logical VM ISA, profiles and codecs
src/vm               host interpreter and sibling-VM model
src/runtime          target runtime image builders
runtime              auditable native interpreter assembly
src/elf              ELF parsing and patch planning
src/patch            detours and ELF payload writers
src/protect          end-to-end target protectors
tools                 command-line programs
scripts              generation, integration and quality automation
tests                 host tests and real-ELF fixtures
```

Historical development notes were consolidated into these documents so that release
users do not have to distinguish completed plans from current behavior. Git history
retains the original planning material.
