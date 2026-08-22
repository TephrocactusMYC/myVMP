#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
object="$root/build/fixtures/arm32_parser_probe.o"
log="$root/build/logs/arm32-parser.log"
mkdir -p "$root/build/fixtures" "$root/build/logs"

clang --target=armv7-linux-gnueabihf -c "$root/fixtures/arm32_parser_probe.S" -o "$object"
"$root/build/myvmp" inspect-elf "$object" candidate_arm32_probe >"$log"
grep -q '^machine=ARM$' "$log"
grep -q '^class=ELF32$' "$log"
grep -q '^symbol=candidate_arm32_probe$' "$log"
grep -q '^symbol_size=12$' "$log"
echo "ARM32 ELF parser fixture passed"
