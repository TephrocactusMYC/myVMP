#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_dir"
mkdir -p build/tmp
work_dir=$(mktemp -d build/tmp/rv64-fixture.XXXXXX)
trap 'rm -r "$work_dir"' EXIT

readelf_bin=${READELF:-readelf}

if command -v "${CLANG:-clang}" >/dev/null 2>&1; then
    "${CLANG:-clang}" --target=riscv64-linux-gnu -march=rv64im -mabi=lp64 \
        -ffreestanding -fno-pic -fno-stack-protector -O2 \
        -c "$repo_dir/tests/fixtures/rv64_integer.c" -o "$work_dir/rv64_integer.o"
else
    riscv64-linux-gnu-gcc -march=rv64im -mabi=lp64 -ffreestanding -fno-pic \
        -fno-stack-protector -O2 -c "$repo_dir/tests/fixtures/rv64_integer.c" \
        -o "$work_dir/rv64_integer.o"
fi

"$readelf_bin" -h "$work_dir/rv64_integer.o" | grep -Eq 'Machine:[[:space:]]+RISC-V'
"$readelf_bin" -S "$work_dir/rv64_integer.o" | grep -Eq '[[:space:]]\.text[[:space:]]'

echo "RV64 compiler fixture passed"
