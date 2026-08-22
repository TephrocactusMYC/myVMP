#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

aarch64-linux-gnu-gcc -O2 -fPIE -pie -march=armv8-a \
    "$repo_dir/tests/fixtures/aarch64_far_target.S" \
    "$repo_dir/tests/fixtures/aarch64_far_driver.c" -o "$work_dir/original"
qemu-aarch64 -L /usr/aarch64-linux-gnu "$work_dir/original" > "$work_dir/expected"
"$repo_dir/build/myvmp" protect "$work_dir/original" aarch64_far_target \
    "$work_dir/protected" 195948557
qemu-aarch64 -L /usr/aarch64-linux-gnu "$work_dir/protected" > "$work_dir/actual"
cmp "$work_dir/expected" "$work_dir/actual"

# The direct B is out of range. Verify the position-independent page veneer:
# ADRP x16; ADD x16,x16,#lo12; BR x16.
aarch64-linux-gnu-objdump -d "$work_dir/protected" | \
    grep -A3 '<aarch64_far_target>:' > "$work_dir/detour.asm"
grep -q 'adrp[[:space:]]*x16' "$work_dir/detour.asm"
grep -q 'add[[:space:]]*x16, x16' "$work_dir/detour.asm"
grep -q 'br[[:space:]]*x16' "$work_dir/detour.asm"

original_size=$(stat -c %s "$work_dir/original")
test "$original_size" -gt $((128 * 1024 * 1024))
echo "AArch64 >128 MiB real-ELF page-detour behavior passed"
