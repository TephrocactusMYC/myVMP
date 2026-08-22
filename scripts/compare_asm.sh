#!/usr/bin/env sh
set -eu

build_dir="${BUILD_DIR:-build}"
before="$build_dir/asm/basic_arithmetic.before.asm"
after="$build_dir/asm/myvmp_demo.vm.asm"
out="$build_dir/asm/compare.diff"

if [ ! -f "$before" ] || [ ! -f "$after" ]; then
    printf '%s\n' "assembly files are missing; run make integration-test first" >&2
    exit 1
fi

if diff -u "$before" "$after" > "$out"; then
    printf '%s\n' "assembly files are identical"
else
    printf '%s\n' "assembly differs; diff written to $out"
fi

