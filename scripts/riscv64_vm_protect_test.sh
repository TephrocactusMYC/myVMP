#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

riscv64-linux-gnu-gcc -O2 -static -march=rv64imafd -mabi=lp64d -mno-relax \
    -fno-pic -fno-stack-protector -Wl,--no-relax \
    "$repo_dir/tests/fixtures/rv64_integer.c" "$repo_dir/tests/fixtures/rv64_driver.c" \
    -o "$work_dir/original"

qemu-riscv64 "$work_dir/original" > "$work_dir/expected"
for seed in 0 1 305419896 3735928559; do
    "$repo_dir/build/myvmp" protect "$work_dir/original" rv64_integer \
        "$work_dir/protected-$seed" "$seed"
    qemu-riscv64 "$work_dir/protected-$seed" > "$work_dir/actual-$seed"
    cmp "$work_dir/expected" "$work_dir/actual-$seed"
done

"$repo_dir/build/myvmp" protect "$work_dir/original" rv64_integer \
    "$work_dir/protected-multi" 4660 --multi-vm 22136
qemu-riscv64 "$work_dir/protected-multi" > "$work_dir/actual-multi"
cmp "$work_dir/expected" "$work_dir/actual-multi"
grep -q '"mode": "sibling-vm"' "$work_dir/protected-multi.myvmp.json"

riscv64-linux-gnu-gcc -O2 -march=rv64imafd -mabi=lp64d -mno-relax \
    -fPIE -pie -Wl,--no-relax \
    "$repo_dir/tests/fixtures/rv64_calls.c" "$repo_dir/tests/fixtures/rv64_calls_driver.c" \
    -o "$work_dir/calls-pie"
qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/calls-pie" > "$work_dir/calls-expected"
"$repo_dir/build/myvmp" protect "$work_dir/calls-pie" rv64_calls \
    "$work_dir/calls-protected" 270544960
qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/calls-protected" > "$work_dir/calls-actual"
cmp "$work_dir/calls-expected" "$work_dir/calls-actual"

riscv64-linux-gnu-gcc -O2 -march=rv64imafd -mabi=lp64d -mno-relax \
    -fPIE -pie -Wl,--no-relax \
    "$repo_dir/tests/fixtures/rv64_corpus.c" "$repo_dir/tests/fixtures/rv64_corpus_driver.c" \
    -o "$work_dir/corpus-pie"
qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/corpus-pie" > "$work_dir/corpus-expected"
for symbol in rv64_loop rv64_global_access rv64_switch; do
    "$repo_dir/build/myvmp" protect "$work_dir/corpus-pie" "$symbol" \
        "$work_dir/corpus-$symbol" 1432778632
    qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/corpus-$symbol" \
        > "$work_dir/corpus-$symbol.actual"
    cmp "$work_dir/corpus-expected" "$work_dir/corpus-$symbol.actual"
done

mkdir -p "$work_dir/shared-original" "$work_dir/shared-protected"
riscv64-linux-gnu-gcc -O2 -march=rv64imafd -mabi=lp64d -fPIC -shared \
    "$repo_dir/tests/fixtures/rv64_corpus.c" -o "$work_dir/shared-original/libcorpus.so"
riscv64-linux-gnu-gcc -O2 -march=rv64imafd -mabi=lp64d \
    "$repo_dir/tests/fixtures/rv64_corpus_driver.c" -L"$work_dir/shared-original" -lcorpus \
    -o "$work_dir/shared-driver"
"$repo_dir/build/myvmp" protect "$work_dir/shared-original/libcorpus.so" \
    rv64_global_access "$work_dir/shared-protected/libcorpus.so" 826366246
QEMU_LD_PREFIX=/usr/riscv64-linux-gnu LD_LIBRARY_PATH="$work_dir/shared-original" \
    qemu-riscv64 "$work_dir/shared-driver" > "$work_dir/shared-expected"
QEMU_LD_PREFIX=/usr/riscv64-linux-gnu LD_LIBRARY_PATH="$work_dir/shared-protected" \
    qemu-riscv64 "$work_dir/shared-driver" > "$work_dir/shared-actual"
cmp "$work_dir/shared-expected" "$work_dir/shared-actual"

# Default RV64GC validates 16/32-bit mixed instruction boundaries and compressed
# register/immediate/load/store aliases in the same PIE/GOT corpus.
riscv64-linux-gnu-gcc -O2 -march=rv64gc -mabi=lp64d \
    -fPIE -pie "$repo_dir/tests/fixtures/rv64_corpus.c" \
    "$repo_dir/tests/fixtures/rv64_corpus_driver.c" -o "$work_dir/corpus-gc"
qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/corpus-gc" > "$work_dir/gc-expected"
for symbol in rv64_loop rv64_global_access rv64_switch; do
    "$repo_dir/build/myvmp" protect "$work_dir/corpus-gc" "$symbol" \
        "$work_dir/gc-$symbol" 2004322440
    qemu-riscv64 -L /usr/riscv64-linux-gnu "$work_dir/gc-$symbol" > "$work_dir/gc-$symbol.actual"
    cmp "$work_dir/gc-expected" "$work_dir/gc-$symbol.actual"
done

echo "RV64 protected-ELF VM behavior passed"
