# myVMP

[中文 README](README.zh-CN.md) · [Documentation](docs/index.md) · [中文文档](docs/zh-CN/index.md) · Version 0.1.0

myVMP is an experimental, function-level ELF virtualizer for AArch64 and RV64, with a
smaller ARM/A32 backend. It lifts selected linked functions into an architecture-neutral
IR, encodes them as seed-specific VM bytecode, embeds a native interpreter and encrypted
payload, then installs a safe entry detour in a new ELF. The input is never modified.

## Why myVMP?

- **Binary-level workflow:** protects selected symbols in already linked ELF executables
  and shared objects; no compiler plugin is required.
- **Dynamic per-build VMs:** seeds change opcode maps, operand encoding, block layout,
  encryption, immediate lowering, dispatcher constants, handler variants and trap fill.
- **Real sibling multi-VM:** basic blocks can be owned by two independently seeded VM
  profiles with validated cross-profile bridges; this is not a static second dispatcher.
- **Cross-ISA architecture:** AArch64 and RV64 share IR, bytecode, profiles, manifests and
  quality infrastructure while retaining ISA-specific lifters, runtimes and detours.
- **Fail closed:** unsupported instructions, relocations, malformed ELF ranges and unsafe
  patch plans are rejected before publication.
- **Measurable quality:** reproducible manifests, byte exposure metrics, multi-seed
  diversity gates, QEMU behavior comparison and large-ELF stress tests ship with the tree.

myVMP is a research project, not a claim of irreversible protection. Use it on selected,
well-tested functions and evaluate the security/performance trade-off for your threat model.

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

Build an AArch64 or RV64 ELF:

```sh
aarch64-linux-gnu-gcc -O2 -g -fno-inline -fno-stack-protector app.c -o app.arm64
riscv64-linux-gnu-gcc  -O2 -g -fno-inline -fno-stack-protector app.c -o app.rv64
```

Inspect, plan and protect one symbol. A non-zero seed makes the transformation
reproducible:

```sh
./build/myvmp inspect-elf app.arm64 protected_calculation
./build/myvmp protect-dry-run app.arm64 protected_calculation 1001
./build/myvmp protect app.arm64 protected_calculation app.protected.arm64 1001
```

The output has a companion `app.protected.arm64.myvmp.json` manifest. Compare complete
program behavior before publishing:

```sh
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.arm64 > before.txt
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.protected.arm64 > after.txt
cmp before.txt after.txt
```

For RV64, use `qemu-riscv64 -L /usr/riscv64-linux-gnu`.

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
metrics. Metrics are regression evidence, not a proof of resistance to devirtualization.

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
- [Supported platforms and environment setup](docs/build-and-platforms.md)
- [Security model and limitations](docs/security-model.md)
- [Testing and release validation](docs/testing.md)
- [Research notes and references](docs/research.md)
- [Roadmap](docs/roadmap.md)
- [Contributing](CONTRIBUTING.md), [security policy](SECURITY.md), and
  [release/signing guide](RELEASING.md)

## License

myVMP is licensed under the [GNU General Public License v3.0 only](LICENSE). Contributions
use the same license and Developer Certificate of Origin sign-off described in
[CONTRIBUTING.md](CONTRIBUTING.md).
