#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
mkdir -p build/logs build/tmp
tmpdir=$(mktemp -d build/tmp/aarch64-simd.XXXXXX)
input="$tmpdir/input"
single="$tmpdir/single"
sibling="$tmpdir/sibling"
trap 'rm -r "$tmpdir"' EXIT

aarch64-linux-gnu-gcc -static -fno-stack-protector \
    "$root/fixtures/aarch64_simd_driver.c" "$root/fixtures/aarch64_simd_probe.S" -o "$input"
before=$(qemu-aarch64 "$input")

"$root/build/myvmp" protect "$input" candidate_aarch64_simd "$single" 31001 \
    >"build/logs/aarch64-simd-single.log"
single_after=$(qemu-aarch64 "$single")
test "$before" = "$single_after"

"$root/build/myvmp" protect "$input" candidate_aarch64_simd "$sibling" 31003 \
    --multi-vm 31009 >"build/logs/aarch64-simd-sibling.log"
sibling_after=$(qemu-aarch64 "$sibling")
test "$before" = "$sibling_after"
grep -q '^vm_mode=sibling$' "build/logs/aarch64-simd-sibling.log"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["mode"] == "sibling-vm"; assert len({b["profile_id"] for b in d["blocks"]}) == 2; names=[n for n in d["profile"]["opcodes"] if n.startswith("Vector")]; assert len(names)==9; assert all(d["profile"]["opcodes"][n] != d["secondary_profile"]["opcodes"][n] for n in names)' "$sibling.myvmp.json"

printf 'AArch64 SIMD single/sibling differential passed: %s\n' "$before"
