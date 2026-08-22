#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
input="$root/build/fixtures/generated/gen_condition_ops.aarch64"
output=$(mktemp /tmp/myvmp-aarch64-patched.XXXXXX)
trap 'rm -f "$output"' EXIT

before=$(qemu-aarch64 "$input")
set -- "$root/build/myvmp" patch-aarch64-stub "$input" candidate_condition_ops "$output"
i=0
while [ "$i" -lt 300 ]; do
    set -- "$@" 0xd503201f
    i=$((i + 1))
done
set -- "$@" 0xd28001e0 0xd65f03c0
"$@" >"$root/build/logs/arm64_elf_patch.log"
after=$(qemu-aarch64 "$output")

test "$before" = "$after"
aarch64-linux-gnu-objdump -d "$output" >"$root/build/asm/gen_condition_ops.patched.asm"
grep -A1 '<candidate_condition_ops>:' "$root/build/asm/gen_condition_ops.patched.asm" | grep -q '[[:space:]]b[[:space:]]'
aarch64-linux-gnu-readelf -lW "$output" >"$root/build/logs/arm64_elf_patch.readelf.log"

echo "ARM64 ELF patch runtime test passed"
