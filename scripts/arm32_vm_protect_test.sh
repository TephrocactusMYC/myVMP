#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$root/scripts/generate_arm32_executable_fixture.py" >/dev/null
input="$root/build/fixtures/arm32_vm_probe"
output="$root/build/fixtures/arm32_vm_probe.protected"
output2="$root/build/fixtures/arm32_vm_probe.seed2.protected"

qemu-arm "$input"
"$root/build/myvmp" protect "$input" candidate_arm32_vm "$output" 32001 \
    >"$root/build/logs/arm32-vm-protect.log"
qemu-arm "$output"
"$root/build/myvmp" protect "$input" candidate_arm32_vm "$output2" 32002 \
    >"$root/build/logs/arm32-vm-protect-seed2.log"
qemu-arm "$output2"
grep -q '^protected=true$' "$root/build/logs/arm32-vm-protect.log"
grep -q '^reason=ARM32 function virtualized with embedded dynamic VM runtime$' \
    "$root/build/logs/arm32-vm-protect.log"
test "$(sha256sum "$output" | cut -d ' ' -f 1)" != \
     "$(sha256sum "$output2" | cut -d ' ' -f 1)"
od -An -tx4 -j 4152 -N 12 "$output" | grep -q 'e7f000f0'
echo "ARM32 VM protected behavior and multi-seed diversity fixture passed"
