#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
input="$root/build/fixtures/generated/gen_condition_ops.aarch64"
mkdir -p build/tmp
tmpdir=$(mktemp -d build/tmp/arm64-elf-patch.XXXXXX)
output="$tmpdir/patched"
trap 'rm -r "$tmpdir"' EXIT

before=$(qemu-aarch64 "$input")
set -- "$root/build/myvmp" patch-aarch64-stub "$input" candidate_condition_ops "$output"
i=0
while [ "$i" -lt 300 ]; do
    set -- "$@" 0xd503201f
    i=$((i + 1))
done
set -- "$@" 0xd28001e0 0xd65f03c0
"$@" >"build/logs/arm64_elf_patch.log"
after=$(qemu-aarch64 "$output")

test "$before" = "$after"
aarch64-linux-gnu-objdump -d "$output" >"build/asm/gen_condition_ops.patched.asm"
grep -A1 '<candidate_condition_ops>:' "$root/build/asm/gen_condition_ops.patched.asm" | grep -q '[[:space:]]b[[:space:]]'
aarch64-linux-gnu-readelf -lW "$output" >"build/logs/arm64_elf_patch.readelf.log"

echo "ARM64 ELF patch runtime test passed"
