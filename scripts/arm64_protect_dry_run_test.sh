#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$root/build/myvmp"
fixture_dir="$root/build/fixtures/generated"
log_dir="$root/build/logs/generated/protect-dry-run"
mkdir -p "$log_dir"

count=0
for binary in "$fixture_dir"/*.aarch64; do
    base=$(basename "$binary" .aarch64)
    family=${base#gen_}
    symbol="candidate_${family}"
    log="$log_dir/$base.log"
    "$tool" protect-dry-run "$binary" "$symbol" 20260815 >"$log"
    grep -q '^liftable=true$' "$log"
    grep -q '^bytecode_size=' "$log"
    grep -q '^detour_kind=aarch64-' "$log"
    count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
    echo "no generated AArch64 fixtures found" >&2
    exit 1
fi

echo "ARM64 protect dry-run passed for $count generated fixtures"
