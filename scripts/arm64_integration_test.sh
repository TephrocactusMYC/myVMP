#!/usr/bin/env sh
set -eu

cc="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
objdump_tool="${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}"
qemu="${QEMU_AARCH64:-qemu-aarch64}"
build_dir="${BUILD_DIR:-build}"
fixture_bin="$build_dir/fixtures/basic_arithmetic.aarch64"
asm_dir="$build_dir/asm"

if ! command -v "$cc" >/dev/null 2>&1; then
    printf '%s\n' "skip: $cc not found"
    printf '%s\n' "install: sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-user"
    exit 0
fi

if ! command -v "$objdump_tool" >/dev/null 2>&1; then
    printf '%s\n' "skip: $objdump_tool not found"
    exit 0
fi

if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "skip: $qemu not found"
    exit 0
fi

mkdir -p "$build_dir/fixtures" "$asm_dir"

"$cc" -std=c11 -O2 -fno-inline -static -Wall -Wextra -Werror \
    fixtures/basic_arithmetic.c -o "$fixture_bin"

"$qemu" "$fixture_bin" > "$build_dir/fixtures/basic_arithmetic.aarch64.out"
"$objdump_tool" -d --disassemble=protected_candidate "$fixture_bin" > "$asm_dir/basic_arithmetic.aarch64.before.asm"
./"$build_dir/myvmp" --log-dir "$build_dir/logs/arm64-integration/myvmp" inspect-elf "$fixture_bin" protected_candidate \
    > "$build_dir/fixtures/basic_arithmetic.aarch64.inspect"

printf '%s\n' "arm64 integration fixture passed"
printf '%s\n' "fixture output: $(cat "$build_dir/fixtures/basic_arithmetic.aarch64.out")"
printf '%s\n' "inspect output: $(head -n 1 "$build_dir/fixtures/basic_arithmetic.aarch64.inspect")"
