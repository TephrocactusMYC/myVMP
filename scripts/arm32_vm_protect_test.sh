#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/logs"
python3 "$root/scripts/generate_arm32_executable_fixture.py" >/dev/null
input="$root/build/fixtures/arm32_vm_probe"
output="$root/build/fixtures/arm32_vm_probe.protected"
output2="$root/build/fixtures/arm32_vm_probe.seed2.protected"
multi_output="$root/build/fixtures/arm32_vm_probe.multi.protected"
batch_output="$root/build/fixtures/arm32_vm_probe.batch.protected"

rm -f "$batch_output" "$batch_output.myvmp.json" "$batch_output.myvmp.batch.json"

qemu-arm "$input"
"$root/build/myvmp" protect "$input" candidate_arm32_vm "$output" 32001 \
    >"$root/build/logs/arm32-vm-protect.log"
qemu-arm "$output"
"$root/build/myvmp" protect "$input" candidate_arm32_vm "$output2" 32002 \
    >"$root/build/logs/arm32-vm-protect-seed2.log"
qemu-arm "$output2"
"$root/build/myvmp" protect "$input" candidate_arm32_vm "$multi_output" 32003 \
    --multi-vm 52003 >"$root/build/logs/arm32-vm-protect-multi.log"
qemu-arm "$multi_output"
"$root/build/myvmp" protect-many "$input" "$batch_output" 42003 \
    --multi-vm 62003 candidate_arm32_vm \
    >"$root/build/logs/arm32-vm-protect-batch.log"
qemu-arm "$batch_output"
grep -q '^protected=true$' "$root/build/logs/arm32-vm-protect.log"
grep -q '^reason=ARM32 function virtualized with embedded dynamic VM runtime$' \
    "$root/build/logs/arm32-vm-protect.log"
grep -q '^vm_mode=sibling$' "$root/build/logs/arm32-vm-protect-multi.log"
grep -q '^reason=ARM32 function virtualized with two sibling VM profiles$' \
    "$root/build/logs/arm32-vm-protect-multi.log"
grep -q '^protected=true$' "$root/build/logs/arm32-vm-protect-batch.log"
test -s "$batch_output.myvmp.json"
test -s "$batch_output.myvmp.batch.json"
test "$(sha256sum "$output" | cut -d ' ' -f 1)" != \
     "$(sha256sum "$output2" | cut -d ' ' -f 1)"
od -An -tx4 -j 4152 -N 12 "$output" | grep -q 'e7f000f0'
echo "ARM32 VM behavior, multi-seed diversity, sibling multi-VM, and batch fixture passed"
