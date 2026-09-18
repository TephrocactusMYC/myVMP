# Usage Guide

[中文](zh-CN/usage.md)

## Prepare the target

myVMP protects a named function in a linked ELF. Protect before stripping symbols, keep
the function out of line, and give it a non-zero symbol size. Start with ordinary scalar
C/C++ and isolate unsupported code in native helpers.

```c
__attribute__((noinline, used))
uint64_t verify_token(uint64_t token, uint64_t secret) {
    return ((token ^ secret) * 0x9e3779b97f4a7c15ULL) + 7;
}
```

Recommended first-pass options are `-O2 -g -fno-inline -fno-stack-protector`. The last
option avoids target-specific TLS canary sequences in the selected function; it is not a
general recommendation to disable stack protection for the rest of an application.

## Inspect a symbol

```sh
./build/myvmp inspect-elf app.arm64 verify_token
```

The output reports ELF class/machine, symbol address and size, relevant relocations,
detour kind and planned payload location. Missing/ambiguous symbols, unsupported machines,
unsafe relocations and patch-space failures are explicit errors.

## Dry run and debug logs

```sh
./build/myvmp --log-dir analysis/logs \
  protect-dry-run app.arm64 verify_token 1001
```

A dry run performs validation, lifting, encoding and planning without writing a protected
ELF. The log directory can contain IR, bytecode, profile, entry bytes and command reports.
With sibling mode:

```sh
./build/myvmp protect-dry-run app.arm64 verify_token 1001 --multi-vm 2002
```

## Protect one function

```sh
./build/myvmp protect INPUT SYMBOL OUTPUT PRIMARY_SEED
./build/myvmp protect INPUT SYMBOL OUTPUT PRIMARY_SEED --multi-vm SECONDARY_SEED
```

Example:

```sh
./build/myvmp protect app.arm64 verify_token app.protected.arm64 1001
```

The command writes a new ELF plus `OUTPUT.myvmp.json`. Do not strip or discard the
manifest until behavior, reproducibility and quality checks are complete. The tool never
modifies `INPUT` in place.

Sibling mode requires two distinct non-zero seeds and a real partition of at least two
CFG blocks. It is opt-in because it costs more code/data and is inappropriate for a tiny
single-block function.

## Protect several functions atomically

```sh
./build/myvmp protect-many INPUT OUTPUT MASTER_SEED [--multi-vm SECONDARY_MASTER] SYMBOL...
```

Example:

```sh
./build/myvmp protect-many service.rv64 service.protected.rv64 123456 \
  verify_license derive_key authorize_request
```

The command derives a deterministic seed for each ordinal symbol, stages transformations,
and publishes only when all functions succeed. It creates `OUTPUT.myvmp.json` and
`OUTPUT.myvmp.batch.json`. Existing outputs/manifests are not overwritten.

Choose a small set of security-critical, computationally bounded functions rather than
virtualizing an entire large program. This improves protection density, keeps performance
measurable and limits exposure to unsupported compiler output.

## Validate behavior

Run the application's normal tests against both files. For cross execution:

```sh
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.arm64 > original.out
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.protected.arm64 > protected.out
cmp original.out protected.out

qemu-riscv64 -L /usr/riscv64-linux-gnu ./app.rv64 > original.out
qemu-riscv64 -L /usr/riscv64-linux-gnu ./app.protected.rv64 > protected.out
cmp original.out protected.out
```

Cover edge values, failure paths, native calls, shared-library loading, concurrency and
floating-point special values relevant to the function. Hash differences alone do not
establish semantic correctness.

## Compare seeds

```sh
./build/myvmp protect app.arm64 verify_token seed-a.arm64 1001
./build/myvmp protect app.arm64 verify_token seed-b.arm64 1002
sha256sum seed-a.arm64 seed-b.arm64
python3 scripts/generate_analysis_report.py \
  --tool build/myvmp --input seed-a.arm64 --protected seed-b.arm64 \
  --output seed-comparison
```

The same seed and same input should reproduce the transformation. Different seeds should
preserve behavior while changing protected content and manifests.

## Lift instructions and symbols

```sh
./build/myvmp lift-arm64 --base-va 0x400770 \
  0xd28000a0 0xd65f03c0
./build/myvmp lift-rv64 \
  0x00500513 0x00700593 0x00b50533 0x00008067
./build/myvmp lift-arm32 \
  0xe2800005 0xe0200001 0xe12fff1e
./build/myvmp lift-elf-symbol app.arm64 verify_token
```

`--base-va` is needed for raw AArch64 instructions that depend on native PC. Symbol
lifting obtains the real VA and file-backed address reader from the ELF automatically.

## Profile manifest

```sh
./build/myvmp profile-manifest 0x20260822
./build/myvmp --log-dir analysis profile-manifest 0x20260822
```

A seed of zero selects the fixed test profile. Production experiments should use explicit
non-zero seeds and store them securely according to build policy.

## Quality report

```sh
./build/myvmp quality-report --json before.bin after.bin
./build/myvmp quality-report --json \
  --max-same-position 0.10 \
  --max-word32-exposure 0.05 --max-word64-exposure 0.01 \
  --max-literal32-exposure 0.05 --max-literal64-exposure 0.01 \
  --max-size-ratio 2.0 before.bin after.bin
```

The command exits with status 1 if any supplied threshold fails. For meaningful
function-level metrics, extract symbol/payload byte ranges rather than comparing two
entire binaries dominated by unchanged application code.

## Error contract

Failures begin on stderr with a stable `error_code=...` line followed by a human-readable
diagnostic. Current categories are `INVALID_ARGUMENT`, `IO_ERROR`, `SYMBOL_NOT_FOUND`,
`UNSUPPORTED_ARCHITECTURE`, `UNSUPPORTED_INSTRUCTION`, `UNSAFE_PATCH`, `OUTPUT_CONFLICT`
and `PROTECTION_ERROR`. Automation should branch on the code instead of matching the
free-form diagnostic.

## ARM32/A32 backend

```sh
arm-linux-gnueabihf-gcc -O2 -g -marm -fno-inline -fno-stack-protector \
  app.c -o app.arm32
./build/myvmp protect app.arm32 verify_token app.protected.arm32 1001
./build/myvmp protect app.arm32 verify_token app.multi.arm32 1001 --multi-vm 2002
qemu-arm -L /usr/arm-linux-gnueabihf ./app.protected.arm32
```

The backend supports little-endian ELF32 ARM-state scalar functions, dynamic opcode and
operand codecs, CFG block shuffling, native/internal calls, direct and non-PIE absolute
far detours, manifests, batch protection and sibling multi-VM. Thumb, VFP/NEON,
privileged instructions and arbitrary compiler output remain fail-closed boundaries.

## Troubleshooting

- **Symbol not found/zero size:** protect before stripping; inspect `readelf -Ws` output.
- **Unsupported instruction:** use `lift-elf-symbol`; simplify or move that operation to
  a native helper rather than bypassing validation.
- **Relocation rejected:** select a smaller function boundary or change code generation;
  never delete relocation metadata manually.
- **Multi-VM rejected:** use a function with multiple CFG blocks and distinct non-zero seeds.
- **QEMU loader failure:** install the target libc sysroot or use a correctly built static
  fixture.
- **Generated runtime mismatch:** run `make generate-<arch>-runtime`, review the assembly
  diff, then `make check-<arch>-runtime`.
