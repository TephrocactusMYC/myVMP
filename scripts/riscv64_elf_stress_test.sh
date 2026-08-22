#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

source_file="$repo_dir/tests/fixtures/rv64_relocation_stress.c"
driver_file="$repo_dir/tests/fixtures/rv64_relocation_stress_driver.c"

run_case() {
    local name=$1
    shift
    local original="$work_dir/$name"
    local protected="$work_dir/$name.protected"
    riscv64-linux-gnu-gcc -O2 -march=rv64gc -mabi=lp64d "$@" \
        "$source_file" "$driver_file" -o "$original"
    qemu-riscv64 -L /usr/riscv64-linux-gnu "$original" > "$work_dir/$name.expected"
    "$repo_dir/build/myvmp" protect "$original" rv64_relocation_target "$protected" \
        324508639 --multi-vm 610839776
    qemu-riscv64 -L /usr/riscv64-linux-gnu "$protected" > "$work_dir/$name.actual"
    cmp "$work_dir/$name.expected" "$work_dir/$name.actual"
    readelf -lW "$protected" | grep -q 'LOAD.*R E'
    local before after
    before=$(readelf -rW "$original" | grep -c 'R_RISCV_' || true)
    after=$(readelf -rW "$protected" | grep -c 'R_RISCV_' || true)
    test "$before" -eq "$after"
}

run_case pie-now -fPIE -pie -Wl,-z,now,-z,relro
run_case pie-lazy -fPIE -pie -Wl,-z,lazy,-z,relro
run_case no-pie -no-pie -Wl,-z,now

# Shared-object layout exercises the dedicated RX PT_LOAD path while the driver
# exercises TLS and dynamic relocations through the protected DSO.
mkdir -p "$work_dir/shared-original" "$work_dir/shared-protected"
riscv64-linux-gnu-gcc -O2 -march=rv64gc -mabi=lp64d -fPIC -shared \
    -Wl,-z,now,-z,relro "$source_file" -o "$work_dir/shared-original/libstress.so"
riscv64-linux-gnu-gcc -O2 -march=rv64gc -mabi=lp64d "$driver_file" \
    -L"$work_dir/shared-original" -lstress -o "$work_dir/shared-driver"
"$repo_dir/build/myvmp" protect "$work_dir/shared-original/libstress.so" \
    rv64_relocation_target "$work_dir/shared-protected/libstress.so" 270544960 --multi-vm 1432778632
QEMU_LD_PREFIX=/usr/riscv64-linux-gnu LD_LIBRARY_PATH="$work_dir/shared-original" \
    qemu-riscv64 "$work_dir/shared-driver" > "$work_dir/shared.expected"
QEMU_LD_PREFIX=/usr/riscv64-linux-gnu LD_LIBRARY_PATH="$work_dir/shared-protected" \
    qemu-riscv64 "$work_dir/shared-driver" > "$work_dir/shared.actual"
cmp "$work_dir/shared.expected" "$work_dir/shared.actual"
readelf -lW "$work_dir/shared-protected/libstress.so" | grep -q 'LOAD.*R E'

# A text segment larger than JAL range forces the position-independent 8-byte
# AUIPC/JALR entry detour. This is the common large-ELF failure mode.
riscv64-linux-gnu-gcc -O2 -static -march=rv64imafd -mabi=lp64d -mno-relax \
    -Wl,--no-relax "$repo_dir/tests/fixtures/rv64_far_target.S" \
    "$repo_dir/tests/fixtures/rv64_far_driver.c" -o "$work_dir/far-original"
qemu-riscv64 "$work_dir/far-original" > "$work_dir/far.expected"
"$repo_dir/build/myvmp" protect "$work_dir/far-original" rv64_far_target \
    "$work_dir/far-protected" 195948557
qemu-riscv64 "$work_dir/far-protected" > "$work_dir/far.actual"
cmp "$work_dir/far.expected" "$work_dir/far.actual"
# AUIPC t0 has opcode 0x17 and JALR t0,...(t0) has opcode 0x67.
riscv64-linux-gnu-objdump -d "$work_dir/far-protected" | \
    grep -A2 '<rv64_far_target>:' | grep -q 'jalr.*t0'

# A realistic large-project workflow protects a selected set of critical functions.
# All three transformations are staged and published atomically with independent seeds.
riscv64-linux-gnu-gcc -O2 -march=rv64gc -mabi=lp64d -fPIE -pie \
    "$repo_dir/tests/fixtures/rv64_corpus.c" "$repo_dir/tests/fixtures/rv64_corpus_driver.c" \
    -o "$work_dir/batch-original"
qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/batch-original" > "$work_dir/batch.expected"
"$repo_dir/build/myvmp" protect-many "$work_dir/batch-original" "$work_dir/batch-protected" \
    305419896 rv64_loop rv64_global_access rv64_switch
qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/batch-protected" > "$work_dir/batch.actual"
cmp "$work_dir/batch.expected" "$work_dir/batch.actual"
python3 -m json.tool "$work_dir/batch-protected.myvmp.batch.json" >/dev/null
grep -q '"function_count": 3' "$work_dir/batch-protected.myvmp.batch.json"

echo "RV64 ELF relocation/TLS/RELRO stress passed"
