#!/usr/bin/env sh
set -eu

cc="${CC:-gcc}"
aarch64_cc="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
aarch64_objdump="${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}"
qemu="${QEMU_AARCH64:-qemu-aarch64}"
build_dir="${BUILD_DIR:-build}"
log_dir="$build_dir/logs/generated"
asm_dir="$build_dir/asm/generated"
bin_dir="$build_dir/fixtures/generated"

mkdir -p "$log_dir" "$asm_dir" "$bin_dir"

python3 scripts/generate_fixtures.py > "$log_dir/generate.log"

for src in fixtures/generated/*.c; do
    base="$(basename "$src" .c)"
    host_bin="$bin_dir/$base.host"
    arm64_bin="$bin_dir/$base.aarch64"
    symbol="candidate_${base#gen_}"
    arch_flags="-mno-outline-atomics"
    if [ "$base" = "gen_lse_cas_ops" ]; then
        arch_flags="-march=armv8.1-a"
    fi

    "$cc" -std=c11 -O2 -fno-inline -Wall -Wextra -Werror "$src" -o "$host_bin" \
        > "$log_dir/$base.host.compile.log" 2>&1
    "$host_bin" > "$log_dir/$base.host.run.log" 2>&1

    "$aarch64_cc" -std=c11 -O2 -fno-inline -fno-stack-protector $arch_flags -static -Wall -Wextra -Werror "$src" -o "$arm64_bin" \
        > "$log_dir/$base.aarch64.compile.log" 2>&1
    "$qemu" "$arm64_bin" > "$log_dir/$base.aarch64.run.log" 2>&1
    "$aarch64_objdump" -d --disassemble="$symbol" "$arm64_bin" > "$asm_dir/$base.aarch64.asm"
    ./"$build_dir/myvmp" --log-dir "$log_dir/$base.myvmp" inspect-elf "$arm64_bin" "$symbol" \
        > "$log_dir/$base.aarch64.inspect.log" 2>&1

    printf '%s\n' "generated fixture passed: $base"
done

printf '%s\n' "logs written to $log_dir"
printf '%s\n' "assembly written to $asm_dir"
