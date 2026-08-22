#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
outputs=""
trap 'rm -f $outputs' EXIT

protect_and_compare() {
    input=$1
    symbol=$2
    name=$3
    seed=${4:-0}
    output=$(mktemp /tmp/myvmp-aarch64-vm-protected.XXXXXX)
    outputs="$outputs $output $output.myvmp.json"
    before=$(qemu-aarch64 "$input")
    "$root/build/myvmp" protect "$input" "$symbol" "$output" "$seed" \
        >"$root/build/logs/$name.vm-protect.log"
    after=$(qemu-aarch64 "$output")
    test "$before" = "$after"
    grep -q '^protected=true$' "$root/build/logs/$name.vm-protect.log"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["profile"]["opcodes"]; assert d["blocks"]' "$output.myvmp.json"
    aarch64-linux-gnu-objdump -d "$output" >"$root/build/asm/$name.protected.asm"
    grep -A1 "<$symbol>:" "$root/build/asm/$name.protected.asm" | grep -q '[[:space:]]b[[:space:]]'
    grep -A2 "<$symbol>:" "$root/build/asm/$name.protected.asm" | grep -q '[[:space:]]brk[[:space:]]'
    digest=$(sha256sum "$output" | cut -d ' ' -f 1)
    echo "ARM64 VM protected $name (seed=$seed, sha256=$digest): $after"
}

protect_multi_and_compare() {
    input=$1
    symbol=$2
    name=$3
    primary_seed=$4
    secondary_seed=$5
    output=$(mktemp /tmp/myvmp-aarch64-multi-vm-protected.XXXXXX)
    outputs="$outputs $output $output.myvmp.json"
    before=$(qemu-aarch64 "$input")
    "$root/build/myvmp" protect "$input" "$symbol" "$output" "$primary_seed" \
        --multi-vm "$secondary_seed" >"$root/build/logs/$name.vm-protect.log"
    after=$(qemu-aarch64 "$output")
    test "$before" = "$after"
    grep -q '^vm_mode=sibling$' "$root/build/logs/$name.vm-protect.log"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["mode"]=="sibling-vm"; assert d["secondary_profile"]["seed"] != d["profile"]["seed"]; assert d["bridges"]; assert len({b["profile_id"] for b in d["blocks"]}) == 2' "$output.myvmp.json"
    echo "ARM64 sibling VMs protected $name (seeds=$primary_seed/$secondary_seed): $after"
}

protect_and_compare "$root/build/fixtures/basic_arithmetic.aarch64" protected_candidate basic_arithmetic
protect_and_compare "$root/build/fixtures/basic_arithmetic.aarch64" protected_candidate basic_arithmetic_seed1 1
protect_and_compare "$root/build/fixtures/basic_arithmetic.aarch64" protected_candidate basic_arithmetic_seed2 2
protect_and_compare "$root/build/fixtures/basic_arithmetic.aarch64" protected_candidate basic_arithmetic_seed3 3
protect_and_compare "$root/build/fixtures/generated/gen_integer_ops.aarch64" candidate_integer_ops gen_integer_ops
protect_and_compare "$root/build/fixtures/generated/gen_bitlogic_ops.aarch64" candidate_bitlogic_ops gen_bitlogic_ops
protect_and_compare "$root/build/fixtures/generated/gen_branch_ops.aarch64" candidate_branch_ops gen_branch_ops 1
protect_and_compare "$root/build/fixtures/generated/gen_branch_zero_ops.aarch64" candidate_branch_zero_ops gen_branch_zero_ops 2
protect_and_compare "$root/build/fixtures/generated/gen_condition_ops.aarch64" candidate_condition_ops gen_condition_ops 3
protect_and_compare "$root/build/fixtures/generated/gen_memory_ops.aarch64" candidate_memory_ops gen_memory_ops 4
protect_and_compare "$root/build/fixtures/generated/gen_narrow_memory_ops.aarch64" candidate_narrow_memory_ops gen_narrow_memory_ops 5
protect_and_compare "$root/build/fixtures/generated/gen_pair_memory_ops.aarch64" candidate_pair_memory_ops gen_pair_memory_ops 6
protect_and_compare "$root/build/fixtures/generated/gen_signed_memory_ops.aarch64" candidate_signed_memory_ops gen_signed_memory_ops 7
protect_and_compare "$root/build/fixtures/generated/gen_muldiv_ops.aarch64" candidate_muldiv_ops gen_muldiv_ops 8
protect_and_compare "$root/build/fixtures/generated/gen_runtime_extended_ops.aarch64" candidate_runtime_extended_ops gen_runtime_extended_ops 9
protect_and_compare "$root/build/fixtures/generated/gen_call_ops.aarch64" candidate_call_ops gen_call_ops 10
protect_and_compare "$root/build/fixtures/generated/gen_indirect_call_ops.aarch64" candidate_indirect_call_ops gen_indirect_call_ops 12
protect_and_compare "$root/build/fixtures/generated/gen_global_ops.aarch64" candidate_global_ops gen_global_ops 15

fp_input=$(mktemp /tmp/myvmp-aarch64-fp.XXXXXX)
outputs="$outputs $fp_input"
aarch64-linux-gnu-gcc -std=c11 -O2 -static -fno-inline -fno-stack-protector -fno-math-errno \
    "$root/fixtures/aarch64_fp_probe.c" -o "$fp_input"
protect_and_compare "$fp_input" candidate_fp_probe aarch64_fp_probe 18
protect_and_compare "$fp_input" candidate_fp32_probe aarch64_fp32_probe 21
protect_and_compare "$fp_input" candidate_fp_extended aarch64_fp_extended 24
protect_and_compare "$fp_input" candidate_fp_convert_minmax aarch64_fp_convert_minmax 25

fp_conditional_input=$(mktemp /tmp/myvmp-aarch64-fp-conditional.XXXXXX)
outputs="$outputs $fp_conditional_input"
aarch64-linux-gnu-gcc -static -fno-stack-protector \
    "$root/fixtures/aarch64_fp_conditional_driver.c" \
    "$root/fixtures/aarch64_fp_conditional.S" -o "$fp_conditional_input"
protect_and_compare "$fp_conditional_input" candidate_fp_conditional aarch64_fp_conditional 26
protect_and_compare "$fp_conditional_input" candidate_fp_conditional_nv aarch64_fp_conditional_nv 27

atomic_pair_input=$(mktemp /tmp/myvmp-aarch64-atomic-pair.XXXXXX)
outputs="$outputs $atomic_pair_input"
aarch64-linux-gnu-gcc -std=c11 -static -fno-stack-protector \
    "$root/fixtures/aarch64_atomic_pair_driver.c" \
    "$root/fixtures/aarch64_atomic_pair.S" -o "$atomic_pair_input"
protect_and_compare "$atomic_pair_input" candidate_atomic_pair aarch64_atomic_pair 27
protect_and_compare "$root/build/fixtures/generated/gen_tail_call_ops.aarch64" candidate_tail_call_ops gen_tail_call_ops 18
protect_and_compare "$root/build/fixtures/generated/gen_indirect_tail_call_ops.aarch64" candidate_indirect_tail_call_ops gen_indirect_tail_call_ops 19
protect_and_compare "$root/build/fixtures/generated/gen_internal_call_ops.aarch64" candidate_internal_call_ops gen_internal_call_ops 20
protect_and_compare "$root/build/fixtures/generated/gen_atomic_ops.aarch64" candidate_atomic_ops gen_atomic_ops 22
protect_and_compare "$root/build/fixtures/generated/gen_lse_cas_ops.aarch64" candidate_lse_cas_ops gen_lse_cas_ops 23
protect_multi_and_compare "$root/build/fixtures/generated/gen_branch_ops.aarch64" candidate_branch_ops multi_branch 1001 2002
protect_multi_and_compare "$root/build/fixtures/generated/gen_internal_call_ops.aarch64" candidate_internal_call_ops multi_internal_call 1003 2004
protect_multi_and_compare "$root/build/fixtures/generated/gen_memory_ops.aarch64" candidate_memory_ops multi_memory 1005 2006

pie_input=$(mktemp /tmp/myvmp-aarch64-call-pie.XXXXXX)
pie_output=$(mktemp /tmp/myvmp-aarch64-call-protected-pie.XXXXXX)
outputs="$outputs $pie_input $pie_output"
aarch64-linux-gnu-gcc -std=c11 -O2 -fno-inline -fno-stack-protector \
    "$root/fixtures/generated/gen_call_ops.c" -o "$pie_input"
pie_before=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$pie_input")
"$root/build/myvmp" protect "$pie_input" candidate_call_ops "$pie_output" 11 \
    >"$root/build/logs/gen_call_ops_pie.vm-protect.log"
pie_after=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$pie_output")
test "$pie_before" = "$pie_after"
grep -q '^protected=true$' "$root/build/logs/gen_call_ops_pie.vm-protect.log"
echo "ARM64 VM protected PIE call gate (seed=11): $pie_after"

pie_indirect_input=$(mktemp /tmp/myvmp-aarch64-indirect-pie.XXXXXX)
pie_indirect_output=$(mktemp /tmp/myvmp-aarch64-indirect-protected-pie.XXXXXX)
outputs="$outputs $pie_indirect_input $pie_indirect_output"
aarch64-linux-gnu-gcc -std=c11 -O2 -fno-inline -fno-stack-protector \
    "$root/fixtures/generated/gen_indirect_call_ops.c" -o "$pie_indirect_input"
pie_indirect_before=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$pie_indirect_input")
"$root/build/myvmp" protect "$pie_indirect_input" candidate_indirect_call_ops \
    "$pie_indirect_output" 13 >"$root/build/logs/gen_indirect_call_ops_pie.vm-protect.log"
pie_indirect_after=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$pie_indirect_output")
test "$pie_indirect_before" = "$pie_indirect_after"
grep -q '^protected=true$' "$root/build/logs/gen_indirect_call_ops_pie.vm-protect.log"
echo "ARM64 VM protected PIE indirect call gate (seed=13): $pie_indirect_after"

shared_input=$(mktemp /tmp/myvmp-aarch64-call-shared.XXXXXX.so)
shared_output=$(mktemp /tmp/myvmp-aarch64-call-protected-shared.XXXXXX.so)
shared_driver=$(mktemp /tmp/myvmp-aarch64-shared-driver.XXXXXX)
outputs="$outputs $shared_input $shared_output $shared_driver"
aarch64-linux-gnu-gcc -std=c11 -O2 -fPIC -fno-inline -fno-stack-protector -shared \
    "$root/fixtures/generated/gen_call_ops.c" -o "$shared_input"
aarch64-linux-gnu-gcc -std=c11 -O2 "$root/fixtures/aarch64_shared_driver.c" \
    -ldl -o "$shared_driver"
shared_before=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$shared_driver" "$shared_input")
"$root/build/myvmp" protect "$shared_input" candidate_call_ops "$shared_output" 14 \
    >"$root/build/logs/gen_call_ops_shared.vm-protect.log"
shared_after=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$shared_driver" "$shared_output")
test "$shared_before" = "$shared_after"
grep -q '^protected=true$' "$root/build/logs/gen_call_ops_shared.vm-protect.log"
echo "ARM64 VM protected shared object call gate (seed=14): $shared_after"

global_pie_input=$(mktemp /tmp/myvmp-aarch64-global-pie.XXXXXX)
global_pie_output=$(mktemp /tmp/myvmp-aarch64-global-protected-pie.XXXXXX)
outputs="$outputs $global_pie_input $global_pie_output"
aarch64-linux-gnu-gcc -std=c11 -O2 -fno-inline -fno-stack-protector \
    "$root/fixtures/generated/gen_global_ops.c" -o "$global_pie_input"
global_pie_before=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$global_pie_input")
"$root/build/myvmp" protect "$global_pie_input" candidate_global_ops \
    "$global_pie_output" 16 >"$root/build/logs/gen_global_ops_pie.vm-protect.log"
global_pie_after=$(qemu-aarch64 -L /usr/aarch64-linux-gnu "$global_pie_output")
test "$global_pie_before" = "$global_pie_after"
echo "ARM64 VM protected PIE global access (seed=16): $global_pie_after"

global_shared_input=$(mktemp /tmp/myvmp-aarch64-global-shared.XXXXXX.so)
global_shared_output=$(mktemp /tmp/myvmp-aarch64-global-protected-shared.XXXXXX.so)
global_shared_driver=$(mktemp /tmp/myvmp-aarch64-global-driver.XXXXXX)
outputs="$outputs $global_shared_input $global_shared_output $global_shared_driver"
aarch64-linux-gnu-gcc -std=c11 -O2 -fPIC -fno-inline -fno-stack-protector -shared \
    "$root/fixtures/generated/gen_global_ops.c" -o "$global_shared_input"
aarch64-linux-gnu-gcc -std=c11 -O2 "$root/fixtures/aarch64_global_shared_driver.c" \
    -ldl -o "$global_shared_driver"
global_shared_before=$(qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$global_shared_driver" "$global_shared_input")
"$root/build/myvmp" protect "$global_shared_input" candidate_global_ops \
    "$global_shared_output" 17 >"$root/build/logs/gen_global_ops_shared.vm-protect.log"
global_shared_after=$(qemu-aarch64 -L /usr/aarch64-linux-gnu \
    "$global_shared_driver" "$global_shared_output")
test "$global_shared_before" = "$global_shared_after"
echo "ARM64 VM protected shared-object global access (seed=17): $global_shared_after"
