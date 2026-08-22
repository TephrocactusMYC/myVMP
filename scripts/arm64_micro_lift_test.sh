#!/usr/bin/env sh
set -eu

as_tool="${AARCH64_AS:-aarch64-linux-gnu-as}"
objdump_tool="${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}"
build_dir="${BUILD_DIR:-build}"
case_dir="$build_dir/micro"
log_dir="$build_dir/logs/micro"
asm_dir="$build_dir/asm/micro"

if ! command -v "$as_tool" >/dev/null 2>&1; then
    printf '%s\n' "skip: $as_tool not found"
    exit 0
fi

if ! command -v "$objdump_tool" >/dev/null 2>&1; then
    printf '%s\n' "skip: $objdump_tool not found"
    exit 0
fi

mkdir -p "$case_dir" "$log_dir" "$asm_dir"

run_case() {
    name="$1"
    source="$2"
    asm_path="$case_dir/$name.s"
    obj_path="$case_dir/$name.o"
    dump_path="$asm_dir/$name.asm"
    ir_path="$log_dir/$name.ir.log"
    err_path="$log_dir/$name.err.log"

    printf '%s\n' "$source" > "$asm_path"
    "$as_tool" -o "$obj_path" "$asm_path"
    "$objdump_tool" -d "$obj_path" > "$dump_path"
    words="$(python3 - "$dump_path" <<'PY'
import re
import sys
from pathlib import Path
text = Path(sys.argv[1]).read_text(encoding="utf-8", errors="ignore")
print(" ".join("0x" + m.group(1) for m in re.finditer(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+", text, re.M)))
PY
)"
    ./"$build_dir/myvmp" --log-dir "$log_dir/$name.myvmp" lift-arm64 $words > "$ir_path" 2> "$err_path"
    printf '%s\n' "micro lift passed: $name"
}

run_case adr 'adr x0, target
adrp x1, target
ret
target:
ret'

run_case flags 'mov x0, #-5
cmn x0, #5
b.eq ok
mov x0, #99
ok:
adds x1, x0, #7
subs x2, x1, #2
cmn x2, x0
cmn x1, w2, uxtw #2
ands w0, w1, w2, lsl #1
tst w3, w4, lsr #2
csinc x3, x4, x5, eq
csinv x6, x7, x8, ne
csneg x9, x10, x11, gt
cset x12, eq
csetm x13, ne
cinc x14, x15, lt
cinv x16, x17, ge
cneg x18, x19, le
madd x20, x21, x22, x23
msub x24, x25, x26, x27
mul x28, x0, x1
mneg x2, x3, x4
smull x5, w6, w7
umull x8, w9, w10
neg x0, x1
negs x2, x3
mvn x4, x5
neg w6, w7
negs w8, w9
mvn w10, w11
dmb ish
dsb sy
isb
ret'

run_case branches 'mov x0, #32
tbz x0, #5, after_tbz
mov x0, #7
after_tbz:
tbnz x0, #2, after_tbnz
mov x0, #99
after_tbnz:
ret'

run_case memory 'ldr x0, [x1, x2]
ldr x3, [x1, x2, lsl #3]
str x3, [x1, x4, lsl #3]
ldrb w5, [x1, x6]
strb w5, [x1, x7]
ldrsb x0, [x1, x2]
ldrsh x3, [x1, x4, lsl #1]
ldrsw x5, [x1, w6, uxtw #2]
ldrsb x0, [x1, #-1]!
ldrsb x2, [x3], #2
ldrsh x4, [x5, #-2]!
ldrsh x6, [x7], #4
ldrsw x8, [x9, #-4]!
ldrsw x10, [x11], #8
ldr x0, [sp, #8]!
ldr x1, [sp], #16
str x2, [sp, #-8]!
str x3, [sp], #-16
ldar x5, [x1]
stlr x5, [x1]
ldxr x6, [x1]
stxr w7, x6, [x1]
stp x0, x1, [sp, #-16]!
ldp x2, x3, [sp], #16
ldp w0, w1, [sp, #8]
stp w2, w3, [sp, #-8]!
ldp w4, w5, [sp], #8
ldpsw x6, x7, [sp, #16]
ldpsw x8, x9, [sp, #-8]!
ldpsw x10, x11, [sp], #8
ret'

printf '%s\n' "micro lift logs written to $log_dir"
