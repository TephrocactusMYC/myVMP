#!/usr/bin/env sh
set -eu

cc="${CC:-gcc}"
objdump_tool="${OBJDUMP:-objdump}"
build_dir="${BUILD_DIR:-build}"
fixture_bin="$build_dir/fixtures/basic_arithmetic"
asm_dir="$build_dir/asm"

mkdir -p "$build_dir/fixtures" "$asm_dir"

"$cc" -std=c11 -O2 -fno-inline -Wall -Wextra -Werror \
    fixtures/basic_arithmetic.c -o "$fixture_bin"

"$fixture_bin" > "$build_dir/fixtures/basic_arithmetic.out"
./"$build_dir/myvmp_demo" > "$build_dir/myvmp_demo.out"
./"$build_dir/myvmp" --log-dir "$build_dir/logs/integration/myvmp" inspect-elf "$fixture_bin" protected_candidate \
    > "$build_dir/fixtures/basic_arithmetic.inspect"

"$objdump_tool" -d "$fixture_bin" > "$asm_dir/basic_arithmetic.before.asm"
"$objdump_tool" -d "$build_dir/myvmp_demo" > "$asm_dir/myvmp_demo.vm.asm"

./"$build_dir/myvmp" quality-report --json --max-same-position 1.0 \
    --max-word32-exposure 1.0 --max-word64-exposure 1.0 \
    --max-literal32-exposure 1.0 --max-literal64-exposure 1.0 --max-size-ratio 1.0 \
    "$fixture_bin" "$fixture_bin" > "$build_dir/fixtures/quality-threshold-pass.json"
if ./"$build_dir/myvmp" quality-report --max-same-position 0.99 \
    "$fixture_bin" "$fixture_bin" > "$build_dir/fixtures/quality-threshold-fail.txt"; then
    printf '%s\n' "quality-report accepted an exceeded threshold" >&2
    exit 1
fi

printf '%s\n' "integration fixture passed"
printf '%s\n' "fixture output: $(cat "$build_dir/fixtures/basic_arithmetic.out")"
printf '%s\n' "demo output: $(head -n 1 "$build_dir/myvmp_demo.out")"
printf '%s\n' "inspect output: $(head -n 1 "$build_dir/fixtures/basic_arithmetic.inspect")"
printf '%s\n' "assembly written to $asm_dir"
