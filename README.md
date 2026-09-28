# myVMP — Dynamic, Multi-Architecture ELF Virtualization

> **Protect the code that matters. Turn selected ARM32, AArch64 and RV64 functions into
> encrypted, per-build virtual machines—directly in a linked ELF.**

![Language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![License](https://img.shields.io/badge/license-GPL--3.0--only-blue)
![Targets](https://img.shields.io/badge/ELF-AArch64%20%7C%20ARM32%20A32%20%7C%20RV64-success)
![VM](https://img.shields.io/badge/VM-dynamic%20sibling%20profiles-purple)

[中文 README](README.zh-CN.md) · [Documentation](docs/index.md) · [中文文档](docs/zh-CN/index.md) · Version 0.1.0

myVMP is a deployable, function-level protector for **ARM32/A32, AArch64 and RV64 ELF**.
Point it at high-value symbols—license checks, algorithms, protocol logic or key
derivation—and it replaces their native control flow with an embedded VM runtime and
encrypted bytecode. No LLVM fork, compiler plugin or source-tree integration is required,
and the original ELF is never modified.

## Why myVMP?

- **Binary-level workflow:** protects selected symbols in already linked ELF executables
  and shared objects; no compiler plugin is required.
- **A different VM for every build:** seeds change opcode maps, operand codecs, block
  ownership/layout, encryption, immediate lowering, dispatcher constants, handler
  variants and destroyed-body trap fill—not just a bytecode XOR key.
- **Real sibling multi-VM:** basic blocks can be owned by two independently seeded VM
  profiles with validated cross-profile bridges; this is not a static second dispatcher.
- **Three production paths, one design:** AArch64, ARM32/A32 and RV64 share semantic IR,
  bytecode/profile APIs, manifests and quality gates while retaining audited ISA-specific
  lifters, runtimes and range-safe detours.
- **AArch64 SIMD virtualization:** a tested fixed 128-bit Advanced SIMD subset covers
  integer and floating-point lanes, Q loads/stores, compare/select, shifts and EXT through
  dynamic profiles, sibling VMs and the native-call bridge.
- **Selective by design:** protect one function or an explicit set with `protect-many`,
  keeping startup time and file growth focused on security-critical code.
- **Fail closed:** unsupported instructions, relocations, malformed ELF ranges and unsafe
  patch plans are rejected before publication.
- **Measurable quality:** reproducible manifests, byte exposure metrics, multi-seed
  diversity gates, QEMU behavior comparison and large-ELF stress tests ship with the tree.

### What protection changes

This is real AArch64 output from the checked-in `candidate_branch_ops` fixture. The
76-byte native loop becomes an entry detour; the remaining native body is destroyed with
seed-derived traps. Its semantics execute from encrypted VM blocks in the appended
payload.

```text
before                                      after
400780: mov  x5, x0                        400780: b    0x47d720
400784: mov  x1, #0                        400784: brk  #0x175a
40078c: lsl  x7, x5, #1                    400788: brk  #0xfef1
400790: add  x4, x5, x1                    40078c: brk  #0xeb9c
   ... conditional loop ...                   ... seed-derived traps ...
4007c4: b.ne 0x400790                      4007c8: brk  #0x3381
4007c8: ret
```

### Measured protection quality

These are real repository fixtures protected in sibling multi-VM mode and executed under
QEMU. Quality compares the **original function bytes** with the **actual encrypted VM
block bytes**, not with the mostly unchanged whole ELF.

| Target | Native → encrypted VM bytes | Entropy before → after | Same-position bytes | 32/64-bit native-word exposure | QEMU |
|---|---:|---:|---:|---:|---:|
| ARM32/A32 | 68 → 264 | 4.128 → 7.242 | 0 | 0 / 0 | identical |
| AArch64 | 76 → 197 | 4.682 → 7.081 | 0 | 0 / 0 | identical |
| RV64 | 32 → 141 | 4.601 → 6.627 | 0 | 0 / 0 | identical |

Literal exposure was also zero in every sample and all quality thresholds passed. These
metrics are regression evidence—not a claim of irreversible protection.

### Selective protection and real ELF scale

| Case | Original ELF | Protected ELF | Growth | Result |
|---|---:|---:|---:|---:|
| ARM32, one function | 12,596 B | 12,596 B | 0 B | identical |
| AArch64, one sibling-VM function | 641,280 B | 706,816 B | 65,536 B | identical |
| RV64, one sibling-VM function | 8,736 B | 16,593 B | 7,857 B | identical |
| RV64, three functions (`protect-many`) | 8,736 B | 25,111 B | 16,375 B | identical |
| AArch64, one function in a 129 MiB PIE | 135,275,392 B | 135,340,928 B | 65,536 B | identical |

The last case exercises the range-safe far-page detour beyond AArch64's direct-branch
range. See the [methodology and interpretation](docs/readme-benchmarks.md) and the
[machine-readable report](docs/examples/quality-benchmarks.json).

myVMP is a research project. Use it on selected, well-tested functions and evaluate the
security/performance trade-off for your threat model.

## Build

Requirements: Linux, GNU Make, a C++20 compiler and Python 3.

```sh
make -j$(nproc)
make test
```

Outputs:

```text
build/myvmp         command-line protector
build/myvmp_demo    host-side VM demonstration
build/myvmp_tests   unit test suite
```

For a stripped release tool and archive:

```sh
make release
make package
```

Cross compilers and QEMU are only required to rebuild target runtimes or run target
behavior tests. See [Build and platforms](docs/build-and-platforms.md).

On Windows, use WSL2; on Windows or macOS, the supported reproducible alternative is the
Ubuntu container:

```sh
docker build -t myvmp:0.1.0 .
docker run --rm myvmp:0.1.0 build/myvmp --version
```

## Protect a function

myVMP consumes a linked ELF with a visible, non-zero-sized symbol. Keep important
functions standalone until after protection:

```c
#include <stdint.h>

__attribute__((noinline, used))
uint64_t protected_calculation(uint64_t x) {
    return (x * 17u) ^ 0x55aa55aaULL;
}
```

Build the ELF for your target:

```sh
arm-linux-gnueabihf-gcc -O2 -g -fno-inline -fno-stack-protector app.c -o app.arm32
aarch64-linux-gnu-gcc -O2 -g -fno-inline -fno-stack-protector app.c -o app.arm64
riscv64-linux-gnu-gcc  -O2 -g -fno-inline -fno-stack-protector app.c -o app.rv64
```

Inspect, plan and protect one symbol. The same CLI works for every target; a non-zero seed
makes the transformation reproducible:

```sh
# ARM32/A32
./build/myvmp inspect-elf app.arm32 protected_calculation
./build/myvmp protect app.arm32 protected_calculation app.protected.arm32 1001
arm32_sysroot=$(dirname "$(dirname "$(arm-linux-gnueabihf-gcc -print-file-name=libc.so.6)")")
qemu-arm -L "$arm32_sysroot" ./app.protected.arm32

# AArch64
./build/myvmp inspect-elf app.arm64 protected_calculation
./build/myvmp protect-dry-run app.arm64 protected_calculation 1001
./build/myvmp protect app.arm64 protected_calculation app.protected.arm64 1001
aarch64_sysroot=$(dirname "$(dirname "$(aarch64-linux-gnu-gcc -print-file-name=libc.so.6)")")
qemu-aarch64 -L "$aarch64_sysroot" ./app.protected.arm64

# RV64
./build/myvmp inspect-elf app.rv64 protected_calculation
./build/myvmp protect-dry-run app.rv64 protected_calculation 1001
./build/myvmp protect app.rv64 protected_calculation app.protected.rv64 1001
riscv64_sysroot=$(dirname "$(dirname "$(riscv64-linux-gnu-gcc -print-file-name=libc.so.6)")")
qemu-riscv64 -L "$riscv64_sysroot" ./app.protected.rv64
```

Each output has a companion `.myvmp.json` manifest. Compare complete program behavior
before publishing:

```sh
aarch64_sysroot=$(dirname "$(dirname "$(aarch64-linux-gnu-gcc -print-file-name=libc.so.6)")")
qemu-aarch64 -L "$aarch64_sysroot" ./app.arm64 > before.txt
qemu-aarch64 -L "$aarch64_sysroot" ./app.protected.arm64 > after.txt
cmp before.txt after.txt
```

## Enable sibling multi-VM

Functions with at least two CFG blocks can use two independently seeded VM profiles:

```sh
./build/myvmp protect app.arm64 protected_calculation app.multi.arm64 \
  1001 --multi-vm 2002
```

The manifest records both profiles, per-block ownership and every bridge. Zero/equal
seeds, single-block functions and invalid ownership fail closed.

## Protect several selected functions

`protect-many` derives per-function seeds from a master seed and publishes the final ELF
only after all requested symbols succeed:

```sh
./build/myvmp protect-many app.rv64 app.protected.rv64 123456 \
  verify_license derive_session_key validate_transaction

./build/myvmp protect-many app.rv64 app.multi.rv64 123456 \
  --multi-vm 654321 verify_license derive_session_key
```

It writes both `.myvmp.json` and aggregate `.myvmp.batch.json` metadata and refuses to
overwrite an existing result.

## Inspect and measure

```sh
./build/myvmp lift-elf-symbol app.arm64 protected_calculation
./build/myvmp profile-manifest 1001
./build/myvmp quality-report --json original-bytes.bin protected-bytes.bin
python3 scripts/generate_analysis_report.py \
  --tool build/myvmp --input app.arm64 --protected app.protected.arm64 \
  --symbol protected_calculation --output analysis
```

The report bundle combines ELF planning, profile metadata, hashes and byte-level quality
metrics. `quality-report --json` declares the stable `myvmp-quality-v1` schema so CI can
validate the contract before parsing fields. Metrics are regression evidence, not a proof
of resistance to devirtualization.

## Validation targets

```sh
make arm64-vm-protect-test
make arm64-far-elf-test
make multi-vm-quality-test
make riscv64-vm-protect-test
make riscv64-elf-stress-test
make riscv64-multi-vm-quality-test
```

## Documentation

- [Documentation map](docs/index.md)
- [Architecture and protection pipeline](docs/architecture.md)
- [Obfuscation and multi-VM design](docs/obfuscation.md)
- [Complete CLI usage](docs/usage.md)
- [Analysis and reports](docs/analysis-and-reports.md)
- [Published cross-architecture benchmark](docs/readme-benchmarks.md)
- [Supported platforms and environment setup](docs/build-and-platforms.md)
- [Security model and limitations](docs/security-model.md)
- [Testing and release validation](docs/testing.md)
- [Research notes and references](docs/research.md)
- [Roadmap](docs/roadmap.md)
- [Contributing](CONTRIBUTING.md) and [security policy](SECURITY.md)

## License

myVMP is licensed under the [GNU General Public License v3.0 only](LICENSE). Contributions
use the same license and Developer Certificate of Origin sign-off described in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Citation

If myVMP is useful in your research or engineering work, please cite the project:

```bibtex
@software{mu_2026_myvmp,
  author  = {Mu, Yuchen},
  title   = {myVMP: Dynamic Multi-Architecture ELF Virtualization},
  year    = {2026},
  version = {0.1.0},
  url     = {https://github.com/TephrocactusMYC/myVMP},
  license = {GPL-3.0-only}
}
```
