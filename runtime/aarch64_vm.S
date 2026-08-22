// Reference source for the position-independent AArch64 VM runtime template.
// The C++ runtime builder emits the same instruction words and patches ADR to the
// bytecode appended immediately after this code. Keep this source as the readable
// specification and validate it in the runtime integration test.

.arch armv8.1-a
.text
.global myvmp_aarch64_entry
.type myvmp_aarch64_entry, %function
myvmp_aarch64_entry:
    // Keep a 64 KiB virtual/native-call stack window above the 64 KiB bytecode
    // buffer so SP-relative locals cannot overwrite the dispatcher state.
    sub sp, sp, #32, lsl #12
    sub sp, sp, #1, lsl #12
    sub sp, sp, #272
    sub sp, sp, #512
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    add x11, sp, #32, lsl #12
    add x11, x11, #1, lsl #12
    add x11, x11, #784
    str x11, [sp, #248]
    str xzr, [sp, #256]
    str xzr, [sp, #264]
    stp q0, q1, [sp, #272]
    stp q2, q3, [sp, #304]
    stp q4, q5, [sp, #336]
    stp q6, q7, [sp, #368]
    stp q8, q9, [sp, #400]
    stp q10, q11, [sp, #432]
    stp q12, q13, [sp, #464]
    stp q14, q15, [sp, #496]
    stp q16, q17, [sp, #528]
    stp q18, q19, [sp, #560]
    stp q20, q21, [sp, #592]
    stp q22, q23, [sp, #624]
    stp q24, q25, [sp, #656]
    stp q26, q27, [sp, #688]
    stp q28, q29, [sp, #720]
    stp q30, q31, [sp, #752]
    // The protected image stores only encrypted bytecode. Decrypt it into the
    // private 64 KiB portion of this frame before entering the dispatcher.
    adr x16, myvmp_bytecode
    ldr w10, [x16], #4
    ldr w11, [x16], #4
    ldr w28, [x16], #4
    ldr w20, [x16], #4
    // Seeded profiles compare opcodes in an XOR-transformed domain. This produces a
    // second dispatcher organization without changing the encoded payload format.
    ldr w29, [x16], #4
    ldr x25, [x16], #8
    // Multi-VM mode stores a dynamic profile count. Metadata then contains one
    // encrypted owner word per block, one handler mask and one encrypted 256-byte
    // physical-to-transport opcode map per profile.
    ldr w6, [x16], #4
    adr x24, myvmp_bytecode
    add x25, x24, x25
    mov x21, x16
    add x16, x16, x20, lsl #2
    cbz w6, 0f
    add x16, x16, x20, lsl #2
    add x16, x16, x6, lsl #2
    add x16, x16, x6, lsl #8
0:
    add x16, x16, #256
    add x26, sp, #16, lsl #12
    add x26, x26, #784
    mov x27, #0
    add x8, sp, #784
    // Tail scratch tracks the currently decoded operand window. Payload capacity
    // excludes these 32 bytes.
    add x7, x8, #15, lsl #12
    add x7, x7, #4064
    stp xzr, xzr, [x7]
    stp xzr, xzr, [x7, #16]
    mov w12, #0
    // 0x9e3779b9 is the per-byte stream index multiplier shared with
    // bytecode::crypt_payload. Keep these halves synchronized with the C++ encoder.
    mov w13, #0x79b9
    movk w13, #0x9e37, lsl #16
    // 0x6a09e667 seeds the CFG rolling-key layer. Only block end offsets are stored;
    // individual block keys are intentionally reconstructed here rather than serialized.
    mov w19, #0xe667
    movk w19, #0x6a09, lsl #16
    eor w19, w19, w11
    mov w22, #0
    mov w23, #0
    cbz w6, 9f
    adr x17, myvmp_bytecode
    add x17, x17, #32
    add x17, x17, x20, lsl #2
    ldr w18, [x17]
    eor w18, w18, w11
    mov w17, #0xeb2d
    movk w17, #0x27d4, lsl #16
    mul w18, w18, w17
    eor w19, w19, w18
9:
    cbnz w20, 1f
    mov w19, #0
    mov w24, w10
    b 2f
1:
    ldr w24, [x21], #4
2:  cmp w12, w10
    b.hs 3f
    cmp w12, w24
    b.lo 4f
    mov w23, w24
    add w22, w22, #1
    cmp w22, w20
    b.hs 4f
    ldr w24, [x21], #4
    eor w19, w19, w19, lsl #13
    eor w19, w19, w19, lsr #17
    eor w19, w19, w19, lsl #5
    // Mix the physical block ordinal with 0x85ebca6b after the xorshift step.
    mov w17, #0xca6b
    movk w17, #0x85eb, lsl #16
    mul w17, w22, w17
    eor w19, w19, w17
    cbz w6, 8f
    adr x17, myvmp_bytecode
    add x17, x17, #32
    add x17, x17, x20, lsl #2
    ldr w18, [x17, x22, lsl #2]
    eor w18, w18, w11
    mov w17, #0x9e37
    mul w17, w22, w17
    eor w18, w18, w17
    mov w17, #0xeb2d
    movk w17, #0x27d4, lsl #16
    mul w18, w18, w17
    eor w19, w19, w18
8:
4:
    ldrb w17, [x16, x12]
    mul w14, w12, w13
    eor w14, w14, w11
    and w15, w12, #3
    lsl w15, w15, #3
    lsr w14, w14, w15
    eor w17, w17, w14
    sub w18, w12, w23
    and w18, w18, #3
    lsl w18, w18, #3
    lsr w18, w19, w18
    eor w17, w17, w18
    strb w17, [x8, x12]
    add w12, w12, #1
    b 2b
3:  mov x9, x8

myvmp_dispatch:
    // Re-encode the previous instruction's operands before fetching the next opcode.
    add x7, x8, #15, lsl #12
    add x7, x7, #4064
    ldr x11, [x7]
    cbz x11, 20f
    ldr w12, [x7, #8]
    ldr w13, [x7, #12]
    mov w14, #0
21: mov w15, #0xa7
    madd w15, w14, w15, w13
    ldrb w16, [x11, x14]
    eor w16, w16, w15
    strb w16, [x11, x14]
    add w14, w14, #1
    cmp w14, w12
    b.lo 21b
    str xzr, [x7]
20:
    ldrb w10, [x9], #1
    mov w17, w10
    sub x11, x9, x8
    sub x11, x11, #1
    str w11, [x7, #16]
    str w17, [x7, #20]
    adr x16, myvmp_bytecode
    ldr w6, [x16, #28]
    cbz w6, 10f
    // Resolve the physical CFG block containing the just-fetched opcode. Block
    // boundaries and owners are immutable runtime metadata, so handlers need no
    // persistent scratch-register convention.
    sub x11, x9, x8
    sub x11, x11, #1
    ldr w12, [x16, #12]
    add x13, x16, #32
    mov w14, #0
11: ldr w15, [x13, x14, lsl #2]
    cmp w11, w15
    b.lo 12f
    add w14, w14, #1
    cmp w14, w12
    b.lo 11b
    b myvmp_bad_opcode
12: add x13, x13, x12, lsl #2
    ldr w15, [x13, x14, lsl #2]
    ldr w17, [x16, #4]
    eor w15, w15, w17
    mov w17, #0x9e37
    mul w17, w14, w17
    eor w15, w15, w17
    add x13, x13, x12, lsl #2
    ldr w28, [x13, x15, lsl #2]
    add x13, x13, x6, lsl #2
    add x13, x13, x15, lsl #8
    // Remove the per-CFG-block opcode whitening before profile translation.
    ldr w12, [x16, #4]
    eor w10, w10, w12
    mov w12, #0x3d
    mul w12, w14, w12
    eor w10, w10, w12
    mov w12, #0x6d
    mul w12, w15, w12
    eor w10, w10, w12
    and w10, w10, #0xff
    mov w17, w10
    ldrb w10, [x13, x17]
    // Translation tables are encrypted at rest with the payload master key,
    // profile index, and physical opcode. Only the fetched entry is decoded.
    ldr w12, [x16, #4]
    eor w10, w10, w12
    mov w12, #0x5d
    mul w12, w15, w12
    eor w10, w10, w12
    mov w12, #0x9b
    mul w12, w17, w12
    eor w10, w10, w12
    and w10, w10, #0xff
    b 13f
10:
    // Reload from the immutable runtime header: handler code deliberately has no
    // callee-saved scratch-register contract.
    ldr w29, [x16, #16]
    eor w10, w10, w29
13:
    // Locate the profile-domain operand-size table and decode only this
    // instruction's operand window. The next dispatch re-encodes it.
    adr x16, myvmp_bytecode
    ldr w6, [x16, #28]
    ldr w12, [x16, #12]
    add x13, x16, #32
    add x13, x13, x12, lsl #2
    cbz w6, 22f
    add x13, x13, x12, lsl #2
    add x13, x13, x6, lsl #2
    add x13, x13, x6, lsl #8
22: ldrb w12, [x13, x10]
    cbz w12, 25f
    ldr w14, [x7, #16]
    ldr w15, [x7, #20]
    ldr w13, [x16, #4]
    mov w11, #0x9b
    mul w14, w14, w11
    eor w13, w13, w14
    mov w11, #0x5d
    mul w15, w15, w11
    eor w13, w13, w15
    mov w14, #0
23: mov w15, #0xa7
    madd w15, w14, w15, w13
    ldrb w16, [x9, x14]
    eor w16, w16, w15
    strb w16, [x9, x14]
    add w14, w14, #1
    cmp w14, w12
    b.lo 23b
    str x9, [x7]
    str w12, [x7, #8]
    str w13, [x7, #12]
25:
    mov w15, w10
    cmp w15, #0x07
    b.eq myvmp_compare_reg_branch
    mov w14, w10
    cmp w14, #0x08
    b.eq myvmp_test_imm_branch
    mov w16, w10
    cmp w16, #0x05
    b.eq myvmp_compare_imm_branch
    mov w17, w10
    cmp w17, #0x01
    b.eq myvmp_load_imm
    cmp w10, #0x11
    b.eq myvmp_load_imm
    cmp w10, #0xe3
    b.eq myvmp_load_address
    cmp w17, #0x02
    b.eq myvmp_add_imm
    cmp w10, #0x22
    b.eq myvmp_add_imm
    cmp w10, #0x99
    b.eq myvmp_xor_shifted
    cmp w10, #0x66
    b.eq myvmp_add_shifted
    cmp w17, #0x03
    b.eq myvmp_sub_imm
    cmp w10, #0x33
    b.eq myvmp_sub_imm
    cmp w10, #0xb2
    b.eq myvmp_and_imm
    cmp w17, #0x09
    b.eq myvmp_and_imm
    cmp w17, #0x04
    b.eq myvmp_xor_imm
    cmp w10, #0xdb
    b.eq myvmp_xor_imm
    cmp w10, #0xaa
    b.eq myvmp_move_keep
    cmp w10, #0x88
    b.eq myvmp_or_shifted
    cmp w10, #0xb3
    b.eq myvmp_sub_extended
    cmp w10, #0xda
    b.eq myvmp_or_imm
    cmp w17, #0x0a
    b.eq myvmp_or_imm
    cmp w10, #0xba
    b.eq myvmp_add_extended
    cmp w10, #0xb7
    b.eq myvmp_bitfield_extract
    cmp w10, #0xd8
    b.eq myvmp_shift_variable
    cmp w10, #0xd9
    b.eq myvmp_divide
    cmp w10, #0xb8
    b.eq myvmp_unsigned_mul_high
    cmp w10, #0xcf
    b.eq myvmp_mul_add
    cmp w10, #0xd0
    b.eq myvmp_mul_sub
    cmp w10, #0xd1
    b.eq myvmp_signed_mul_long
    cmp w10, #0xd2
    b.eq myvmp_unsigned_mul_long
    cmp w10, #0x77
    b.eq myvmp_and_shifted
    cmp w10, #0xd6
    b.eq myvmp_not_shifted
    cmp w10, #0xbd
    b.eq myvmp_xor_not_shifted
    cmp w10, #0xbc
    b.eq myvmp_or_not_shifted
    cmp w10, #0xbb
    b.eq myvmp_bit_clear_shifted
    cmp w10, #0xb1
    b.eq myvmp_shift_imm
    cmp w10, #0xc9
    b.eq myvmp_shift_imm32
    cmp w10, #0xab
    b.eq myvmp_sub_shifted
    cmp w10, #0xc3
    b.eq myvmp_add_imm_flags
    cmp w10, #0xc4
    b.eq myvmp_sub_imm_flags
    cmp w10, #0xc6
    b.eq myvmp_sub_shifted_flags
    cmp w10, #0xc5
    b.eq myvmp_add_shifted_flags
    cmp w10, #0xcb
    b.eq myvmp_add_extended_flags
    cmp w10, #0xd7
    b.eq myvmp_sub_extended_flags
    cmp w10, #0xd4
    b.eq myvmp_neg
    cmp w10, #0xd5
    b.eq myvmp_neg_flags
    cmp w10, #0xac
    b.eq myvmp_move_reg
    cmp w17, #0x0b
    b.eq myvmp_move_reg
    cmp w10, #0xad
    b.eq myvmp_compare_imm
    cmp w10, #0xbe
    b.eq myvmp_compare_reg
    cmp w10, #0xae
    b.eq myvmp_test_imm
    cmp w10, #0xc1
    b.eq myvmp_test_shifted
    cmp w10, #0xca
    b.eq myvmp_test_shifted32
    cmp w10, #0xaf
    b.eq myvmp_branch_if
    cmp w10, #0xb9
    b.eq myvmp_branch_zero
    cmp w10, #0xc2
    b.eq myvmp_branch_bit
    cmp w10, #0xb0
    b.eq myvmp_select
    cmp w10, #0xcc
    b.eq myvmp_select_inc
    cmp w10, #0xcd
    b.eq myvmp_select_inv
    cmp w10, #0xce
    b.eq myvmp_select_neg
    cmp w10, #0xbf
    b.eq myvmp_load_mem
    cmp w10, #0xc0
    b.eq myvmp_store_mem
    cmp w10, #0xb4
    b.eq myvmp_load64
    cmp w10, #0xb5
    b.eq myvmp_store64
    cmp w10, #0xc7
    b.eq myvmp_load_reg_offset
    cmp w10, #0xc8
    b.eq myvmp_store_reg_offset
    cmp w10, #0xb6
    b.eq myvmp_store_pair
    cmp w10, #0xe1
    b.eq myvmp_call_native
    cmp w10, #0xe2
    b.eq myvmp_call_native_reg
    cmp w10, #0xe4
    b.eq myvmp_call_vm
    cmp w10, #0xe5
    b.eq myvmp_load_fp_imm64
    cmp w10, #0xe6
    b.eq myvmp_fp_add64
    cmp w10, #0xe7
    b.eq myvmp_fp_sub64
    cmp w10, #0xe8
    b.eq myvmp_fp_mul64
    cmp w10, #0xe9
    b.eq myvmp_fp_div64
    cmp w10, #0xea
    b.eq myvmp_fp_madd64
    cmp w10, #0xeb
    b.eq myvmp_fp_compare64
    cmp w10, #0xec
    b.eq myvmp_load_fp_imm32
    cmp w10, #0xed
    b.eq myvmp_fp_add32
    cmp w10, #0xee
    b.eq myvmp_fp_sub32
    cmp w10, #0xef
    b.eq myvmp_fp_mul32
    cmp w10, #0xf0
    b.eq myvmp_fp_div32
    cmp w10, #0xf1
    b.eq myvmp_fp_madd32
    cmp w10, #0xf2
    b.eq myvmp_fp_compare32
    cmp w10, #0xf3
    b.eq myvmp_load_acquire
    cmp w10, #0xf4
    b.eq myvmp_store_release
    cmp w10, #0xf5
    b.eq myvmp_load_exclusive
    cmp w10, #0xf6
    b.eq myvmp_store_exclusive
    cmp w10, #0xf7
    b.eq myvmp_compare_swap
    cmp w10, #0xf8
    b.eq myvmp_fp_unary
    cmp w10, #0xf9
    b.eq myvmp_fp_convert
    cmp w10, #0xfa
    b.eq myvmp_fp_min_max
    cmp w10, #0x0c
    b.eq myvmp_fp_conditional_compare
    cmp w10, #0x0d
    b.eq myvmp_fp_select
    cmp w10, #0x0e
    b.eq myvmp_load_exclusive_pair
    cmp w10, #0x0f
    b.eq myvmp_store_exclusive_pair
    cmp w10, #0xd3
    b.eq myvmp_barrier
    cmp w10, #0x44
    b.eq myvmp_return
    cmp w10, #0x06
    b.eq myvmp_jump_block
    cmp w10, #0x55
    b.eq myvmp_halt
    b myvmp_bad_opcode

// Barrier IR currently shares the operand-free opcode with semantic no-op IR.
// Use a conservative full-system completion + instruction barrier: this is
// stronger than DMB/DSB/ISB individually and therefore preserves ordering.
myvmp_barrier:
    dsb sy
    isb
    b myvmp_dispatch

myvmp_load_imm:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    str x12, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_load_address:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    add x12, x25, x12
    str x12, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_add_imm:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #0, 221f
    add x13, x14, x13
    b 222f
221: neg x13, x13
    sub x13, x14, x13
222:
    str x13, [sp, x11, lsl #3]
    b myvmp_dispatch

// Common shifted-register decoding. The individual handlers duplicate the small
// shift selector so the template remains branch-local and position-independent.
myvmp_xor_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 2f
    cmp w14, #1
    b.eq 3f
    cmp w14, #2
    b.eq 4f
    rorv x17, x17, x15
    b 5f
2:  lslv x17, x17, x15
    b 5f
3:  lsrv x17, x17, x15
    b 5f
4:  asrv x17, x17, x15
5:  tbnz w28, #14, 242f
    eor x16, x16, x17
    b 243f
242: mvn x17, x17
    eon x16, x16, x17
243:
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_add_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 6f
    cmp w14, #1
    b.eq 7f
    cmp w14, #2
    b.eq 8f
    rorv x17, x17, x15
    b 9f
6:  lslv x17, x17, x15
    b 9f
7:  lsrv x17, x17, x15
    b 9f
8:  asrv x17, x17, x15
9:  tbnz w28, #11, 244f
    add x16, x16, x17
    b 245f
244: neg x17, x17
    sub x16, x16, x17
245:
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_sub_imm:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #1, 223f
    sub x13, x14, x13
    b 224f
223: neg x13, x13
    add x13, x14, x13
224:
    str x13, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_and_imm:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #3, 227f
    and x13, x14, x13
    b 228f
227: mvn x13, x13
    bic x13, x14, x13
228:
    str x13, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_xor_imm:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #2, 225f
    eor x14, x14, x13
    b 226f
225: mvn x13, x13
    eon x14, x14, x13
226:
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_move_keep:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x11, lsl #3]
    mov x15, #0xffff
    lslv x13, x13, x12
    lslv x15, x15, x12
    bic x14, x14, x15
    orr x13, x14, x13
    str x13, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_or_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 10f
    cmp w14, #1
    b.eq 11f
    cmp w14, #2
    b.eq 12f
    rorv x17, x17, x15
    b 13f
10: lslv x17, x17, x15
    b 13f
11: lsrv x17, x17, x15
    b 13f
12: asrv x17, x17, x15
13: orr x16, x16, x17
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_sub_extended:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 14f
    cmp w14, #1
    b.eq 15f
    cmp w14, #2
    b.eq 16f
    cmp w14, #3
    b.eq 20f
    cmp w14, #4
    b.eq 17f
    cmp w14, #5
    b.eq 18f
    cmp w14, #6
    b.eq 19f
    b 20f
14: and x17, x17, #0xff
    b 20f
15: and x17, x17, #0xffff
    b 20f
16: mov w17, w17
    b 20f
17: sxtb x17, w17
    b 20f
18: sxth x17, w17
    b 20f
19: sxtw x17, w17
20: lslv x17, x17, x15
    sub x16, x16, x17
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_or_imm:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #4, 229f
    orr x14, x14, x13
    b 230f
229: mvn x13, x13
    orn x14, x14, x13
230:
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_add_extended:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 83f
    cmp w14, #1
    b.eq 84f
    cmp w14, #2
    b.eq 85f
    cmp w14, #3
    b.eq 89f
    cmp w14, #4
    b.eq 86f
    cmp w14, #5
    b.eq 87f
    cmp w14, #6
    b.eq 88f
    b 89f
83: and x17, x17, #0xff
    b 89f
84: and x17, x17, #0xffff
    b 89f
85: mov w17, w17
    b 89f
86: sxtb x17, w17
    b 89f
87: sxth x17, w17
    b 89f
88: sxtw x17, w17
89: lslv x17, x17, x15
    add x16, x16, x17
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_bitfield_extract:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldr x15, [sp, x12, lsl #3]
    lsrv x15, x15, x13
    mov x16, #-1
    cmp w14, #64
    b.eq 91f
    mov x17, #1
    lslv x17, x17, x14
    sub x16, x17, #1
91: and x15, x15, x16
    str x15, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_shift_variable:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cmp w15, #4
    b.eq 92f
    cbz w14, 96f
    cmp w14, #1
    b.eq 97f
    cmp w14, #2
    b.eq 98f
    rorv x16, x16, x17
    b 99f
92: cbz w14, 93f
    cmp w14, #1
    b.eq 94f
    cmp w14, #2
    b.eq 95f
    rorv w16, w16, w17
    b 99f
93: lslv w16, w16, w17
    b 99f
94: lsrv w16, w16, w17
    b 99f
95: asrv w16, w16, w17
    b 99f
96: lslv x16, x16, x17
    b 99f
97: lsrv x16, x16, x17
    b 99f
98: asrv x16, x16, x17
99: str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_divide:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cmp w15, #4
    b.eq 100f
    cbnz w14, 101f
    udiv x16, x16, x17
    b 104f
100: cbnz w14, 102f
    udiv w16, w16, w17
    b 104f
101: sdiv x16, x16, x17
    b 104f
102: sdiv w16, w16, w17
104: str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_unsigned_mul_high:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    ldr x15, [sp, x13, lsl #3]
    umulh x14, x14, x15
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_mul_add:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    ldr x14, [sp, x14, lsl #3]
    tbz w6, #0, 105f
    mov x14, #0
105: cmp w15, #4
    b.eq 106f
    madd x16, x16, x17, x14
    b 107f
106: madd w16, w16, w17, w14
107: str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_mul_sub:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    ldr x14, [sp, x14, lsl #3]
    tbz w6, #0, 108f
    mov x14, #0
108: cmp w15, #4
    b.eq 109f
    msub x16, x16, x17, x14
    b 110f
109: msub w16, w16, w17, w14
110: str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_signed_mul_long:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    ldr x15, [sp, x13, lsl #3]
    smull x14, w14, w15
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_unsigned_mul_long:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    ldr x15, [sp, x13, lsl #3]
    umull x14, w14, w15
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_and_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_shift_x17
    tbnz w28, #13, 246f
    and x16, x16, x17
    b 247f
246: mvn x17, x17
    bic x16, x16, x17
247:
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_not_shifted:
    ldrb w11, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x17, [sp, x13, lsl #3]
    cmp w6, #4
    b.eq 121f
    bl myvmp_shift_x17
    mvn x17, x17
    b 125f
121: cbz w14, 122f
    cmp w14, #1
    b.eq 123f
    cmp w14, #2
    b.eq 124f
    rorv w17, w17, w15
    b 126f
122: lslv w17, w17, w15
    b 126f
123: lsrv w17, w17, w15
    b 126f
124: asrv w17, w17, w15
126: mvn w17, w17
125:
    str x17, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_xor_not_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_shift_x17
    eon x16, x16, x17
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

// Input: x17 value, w14 shift kind, w15 amount. Output: shifted x17.
// w6 selects the 32-bit form when it is 4.
myvmp_shift_x17_width:
    cmp w6, #4
    b.ne myvmp_shift_x17
    cbz w14, 152f
    cmp w14, #1
    b.eq 153f
    cmp w14, #2
    b.eq 154f
    rorv w17, w17, w15
    ret
152: lslv w17, w17, w15
    ret
153: lsrv w17, w17, w15
    ret
154: asrv w17, w17, w15
    ret

myvmp_shift_x17:
    cbz w14, 111f
    cmp w14, #1
    b.eq 112f
    cmp w14, #2
    b.eq 113f
    rorv x17, x17, x15
    ret
111: lslv x17, x17, x15
    ret
112: lsrv x17, x17, x15
    ret
113: asrv x17, x17, x15
    ret

myvmp_or_not_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 21f
    cmp w14, #1
    b.eq 22f
    cmp w14, #2
    b.eq 23f
    rorv x17, x17, x15
    b 24f
21: lslv x17, x17, x15
    b 24f
22: lsrv x17, x17, x15
    b 24f
23: asrv x17, x17, x15
24: orn x16, x16, x17
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_bit_clear_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 25f
    cmp w14, #1
    b.eq 26f
    cmp w14, #2
    b.eq 27f
    rorv x17, x17, x15
    b 28f
25: lslv x17, x17, x15
    b 28f
26: lsrv x17, x17, x15
    b 28f
27: asrv x17, x17, x15
28: bic x16, x16, x17
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_shift_imm:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    cbz w14, 29f
    cmp w14, #1
    b.eq 30f
    cmp w14, #2
    b.eq 31f
    rorv x16, x16, x15
    b 32f
29: lslv x16, x16, x15
    b 32f
30: lsrv x16, x16, x15
    b 32f
31: asrv x16, x16, x15
32: str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_sub_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 33f
    cmp w14, #1
    b.eq 34f
    cmp w14, #2
    b.eq 35f
    rorv x17, x17, x15
    b 36f
33: lslv x17, x17, x15
    b 36f
34: lsrv x17, x17, x15
    b 36f
35: asrv x17, x17, x15
36: tbnz w28, #12, 248f
    sub x16, x16, x17
    b 249f
248: neg x17, x17
    add x16, x16, x17
249:
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_add_imm_flags:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldrb w15, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    cmp w15, #4
    b.eq 65f
    adds x14, x14, x13
    b 66f
65: adds w14, w14, w13
66: mrs x16, nzcv
    str x16, [sp, #264]
    cmp w11, #31
    b.hs myvmp_dispatch
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_sub_imm_flags:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldrb w15, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    cmp w15, #4
    b.eq 67f
    subs x14, x14, x13
    b 68f
67: subs w14, w14, w13
68: mrs x16, nzcv
    str x16, [sp, #264]
    cmp w11, #31
    b.hs myvmp_dispatch
    str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_sub_shifted_flags:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    cbz w14, 69f
    cmp w14, #1
    b.eq 70f
    cmp w14, #2
    b.eq 71f
    rorv x17, x17, x15
    b 72f
69: lslv x17, x17, x15
    b 72f
70: lsrv x17, x17, x15
    b 72f
71: asrv x17, x17, x15
72: cmp w6, #4
    b.eq 73f
    subs x16, x16, x17
    b 74f
73: subs w16, w16, w17
74: mrs x14, nzcv
    str x14, [sp, #264]
    cmp w11, #31
    b.hs myvmp_dispatch
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_add_shifted_flags:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_shift_x17_width
    cmp w6, #4
    b.eq 141f
    adds x16, x16, x17
    b 142f
141: adds w16, w16, w17
142: mrs x14, nzcv
    str x14, [sp, #264]
    cmp w11, #31
    b.hs myvmp_dispatch
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_add_extended_flags:
    mov w7, #0
    b myvmp_extended_flags_common
myvmp_sub_extended_flags:
    mov w7, #1
myvmp_extended_flags_common:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_extend_x17
    lslv x17, x17, x15
    cbnz w7, 143f
    cmp w6, #4
    b.eq 144f
    adds x16, x16, x17
    b 146f
144: adds w16, w16, w17
    b 146f
143: cmp w6, #4
    b.eq 145f
    subs x16, x16, x17
    b 146f
145: subs w16, w16, w17
146: mrs x14, nzcv
    str x14, [sp, #264]
    cmp w11, #31
    b.hs myvmp_dispatch
    str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_neg:
    mov w7, #0
    b myvmp_neg_common
myvmp_neg_flags:
    mov w7, #1
myvmp_neg_common:
    ldrb w11, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_shift_x17_width
    cbnz w7, 147f
    cmp w6, #4
    b.eq 148f
    neg x17, x17
    b 151f
148: neg w17, w17
    b 151f
147: cmp w6, #4
    b.eq 149f
    negs x17, x17
    b 150f
149: negs w17, w17
150: mrs x14, nzcv
    str x14, [sp, #264]
    cmp w11, #31
    b.hs myvmp_dispatch
151: str x17, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_move_reg:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [sp, x12, lsl #3]
    tbz w28, #5, 231f
    eor x13, x13, xzr
231:
    str x13, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_shift_imm32:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x13, [sp, x12, lsl #3]
    cbz w14, 114f
    cmp w14, #1
    b.eq 115f
    cmp w14, #2
    b.eq 116f
    rorv w13, w13, w15
    b 117f
114: lslv w13, w13, w15
    b 117f
115: lsrv w13, w13, w15
    b 117f
116: asrv w13, w13, w15
117: str x13, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_compare_imm:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    ldr x13, [sp, x11, lsl #3]
    tbnz w28, #6, 232f
    cmp x13, x12
    b 233f
232: subs xzr, x13, x12
233:
    mrs x14, nzcv
    str x14, [sp, #264]
    b myvmp_dispatch

myvmp_compare_imm_branch:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    ldrb w15, [x9], #1
    ldr w16, [x9], #4
    ldr x13, [sp, x11, lsl #3]
    cmp x13, x12
    mrs x14, nzcv
    str x14, [sp, #264]
    mov w11, w15
    bl myvmp_condition_true
    cbz w0, myvmp_dispatch
    add x9, x8, x16
    b myvmp_dispatch

myvmp_compare_reg_branch:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w15, [x9], #1
    ldr w16, [x9], #4
    ldr x13, [sp, x11, lsl #3]
    ldr x14, [sp, x12, lsl #3]
    cmp x13, x14
    mrs x14, nzcv
    str x14, [sp, #264]
    mov w11, w15
    bl myvmp_condition_true
    cbz w0, myvmp_dispatch
    add x9, x8, x16
    b myvmp_dispatch

myvmp_test_imm_branch:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    ldrb w15, [x9], #1
    ldr w16, [x9], #4
    ldr x13, [sp, x11, lsl #3]
    tst x13, x12
    mrs x14, nzcv
    str x14, [sp, #264]
    mov w11, w15
    bl myvmp_condition_true
    cbz w0, myvmp_dispatch
    add x9, x8, x16
    b myvmp_dispatch

myvmp_compare_reg:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [sp, x11, lsl #3]
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #7, 234f
    cmp x13, x14
    b 235f
234: subs xzr, x13, x14
235:
    mrs x14, nzcv
    str x14, [sp, #264]
    b myvmp_dispatch

myvmp_test_imm:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    ldr x13, [sp, x11, lsl #3]
    tbnz w28, #8, 236f
    tst x13, x12
    b 237f
236: ands xzr, x13, x12
237:
    mrs x14, nzcv
    str x14, [sp, #264]
    b myvmp_dispatch

myvmp_test_shifted:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x11, lsl #3]
    ldr x17, [sp, x12, lsl #3]
    cbz w14, 37f
    cmp w14, #1
    b.eq 38f
    cmp w14, #2
    b.eq 39f
    rorv x17, x17, x15
    b 40f
37: lslv x17, x17, x15
    b 40f
38: lsrv x17, x17, x15
    b 40f
39: asrv x17, x17, x15
40: tbnz w28, #15, 250f
    tst x16, x17
    b 251f
250: ands xzr, x16, x17
251:
    mrs x14, nzcv
    str x14, [sp, #264]
    b myvmp_dispatch

myvmp_test_shifted32:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x11, lsl #3]
    ldr x17, [sp, x12, lsl #3]
    cbz w14, 41f
    cmp w14, #1
    b.eq 42f
    cmp w14, #2
    b.eq 43f
    rorv w17, w17, w15
    b 44f
41: lslv w17, w17, w15
    b 44f
42: lsrv w17, w17, w15
    b 44f
43: asrv w17, w17, w15
44: tbnz w28, #16, 252f
    tst w16, w17
    b 253f
252: ands wzr, w16, w17
253:
    mrs x14, nzcv
    str x14, [sp, #264]
    b myvmp_dispatch

myvmp_branch_if:
    ldrb w11, [x9], #1
    ldr w12, [x9], #4
    bl myvmp_condition_true
    tbnz w28, #17, 254f
    cbz w0, myvmp_dispatch
    b 255f
254: cmp w0, #0
    b.eq myvmp_dispatch
255:
    add x9, x8, x12
    b myvmp_dispatch

myvmp_branch_zero:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr w13, [x9], #4
    ldr x14, [sp, x11, lsl #3]
    tbnz w28, #18, 256f
    cbnz w12, 45f
    cbnz x14, 46f
    b myvmp_dispatch
45: cbz x14, 46f
    b myvmp_dispatch
256: cmp x14, #0
    cset w15, eq
    cmp w15, w12
    b.ne myvmp_dispatch
46: add x9, x8, x13
    b myvmp_dispatch

myvmp_branch_bit:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr w14, [x9], #4
    ldr x15, [sp, x11, lsl #3]
    lsrv x15, x15, x12
    and x15, x15, #1
    tbnz w28, #19, 257f
    cbnz w13, 47f
    cbnz x15, 48f
    b myvmp_dispatch
47: cbz x15, 48f
    b myvmp_dispatch
257: eor w15, w15, w13
    cbz w15, myvmp_dispatch
48: add x9, x8, x14
    b myvmp_dispatch

myvmp_select:
    mov w6, #0
    b myvmp_select_common
myvmp_select_inc:
    mov w6, #1
    b myvmp_select_common
myvmp_select_inv:
    mov w6, #2
    b myvmp_select_common
myvmp_select_neg:
    mov w6, #3
myvmp_select_common:
    ldrb w7, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    mov w11, w14
    bl myvmp_condition_true
    mov x16, #0
    tbnz w15, #0, 49f
    ldr x16, [sp, x12, lsl #3]
49: mov x17, #0
    tbnz w15, #1, 50f
    ldr x17, [sp, x13, lsl #3]
50: cmp w0, #0
    b.ne 118f
    cmp w6, #1
    b.eq 119f
    cmp w6, #2
    b.eq 120f
    cmp w6, #3
    b.ne 118f
    neg x17, x17
    b 118f
119: add x17, x17, #1
    b 118f
120: mvn x17, x17
118: tbnz w28, #20, 258f
    cmp w0, #0
    csel x16, x16, x17, ne
    b 259f
258: cbnz w0, 259f
    mov x16, x17
259:
    str x16, [sp, x7, lsl #3]
    b myvmp_dispatch

myvmp_load_mem:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [x9], #8
    ldr x15, [sp, x12, lsl #3]
    add x15, x15, x14
    cmp w13, #1
    b.eq 75f
    cmp w13, #2
    b.eq 76f
    cmp w13, #4
    b.eq 77f
    ldr x16, [x15]
    b 78f
75: ldrb w16, [x15]
    b 78f
76: ldrh w16, [x15]
    b 78f
77: ldr w16, [x15]
78: str x16, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_store_mem:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [x9], #8
    ldr x15, [x9], #8
    ldr x16, [sp, x11, lsl #3]
    ldr x17, [sp, x12, lsl #3]
    add x14, x17, x14
    cmp w13, #1
    b.eq 79f
    cmp w13, #2
    b.eq 80f
    cmp w13, #4
    b.eq 81f
    str x16, [x14]
    b 82f
79: strb w16, [x14]
    b 82f
80: strh w16, [x14]
    b 82f
81: str w16, [x14]
82: add x17, x17, x15
    str x17, [sp, x12, lsl #3]
    b myvmp_dispatch

myvmp_load64:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [sp, x12, lsl #3]
    tbnz w28, #9, 238f
    ldr x15, [x14, x13]
    b 239f
238: add x14, x14, x13
    ldr x15, [x14]
239:
    str x15, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_store64:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldr x13, [x9], #8
    ldr x14, [x9], #8
    ldr x15, [sp, x11, lsl #3]
    ldr x16, [sp, x12, lsl #3]
    tbnz w28, #10, 240f
    str x15, [x16, x13]
    b 241f
240: add x17, x16, x13
    str x15, [x17]
241:
    add x16, x16, x14
    str x16, [sp, x12, lsl #3]
    b myvmp_dispatch

myvmp_load_reg_offset:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_extend_x17
    lslv x17, x17, x15
    add x16, x16, x17
    cmp w6, #1
    b.eq 127f
    cmp w6, #2
    b.eq 128f
    cmp w6, #4
    b.eq 129f
    ldr x17, [x16]
    b 130f
127: ldrb w17, [x16]
    b 130f
128: ldrh w17, [x16]
    b 130f
129: ldr w17, [x16]
130: str x17, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_store_reg_offset:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldrb w6, [x9], #1
    ldr x7, [sp, x11, lsl #3]
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    bl myvmp_extend_x17
    lslv x17, x17, x15
    add x16, x16, x17
    cmp w6, #1
    b.eq 131f
    cmp w6, #2
    b.eq 132f
    cmp w6, #4
    b.eq 133f
    str x7, [x16]
    b myvmp_dispatch
131: strb w7, [x16]
    b myvmp_dispatch
132: strh w7, [x16]
    b myvmp_dispatch
133: str w7, [x16]
    b myvmp_dispatch

myvmp_store_pair:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [x9], #8
    ldr x15, [sp, x11, lsl #3]
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    add x17, x17, x14
    stp x15, x16, [x17]
    b myvmp_dispatch

myvmp_call_native:
    ldr x22, [x9], #8
    ldr x10, [x9], #8
    add x22, x25, x22
    cbz x10, myvmp_call_native_common
    add x10, x25, x10
    str x10, [sp, #240]
    b myvmp_call_native_common
myvmp_call_native_reg:
    ldrb w10, [x9], #1
    ldr x22, [sp, x10, lsl #3]
    ldr x10, [x9], #8
    cbz x10, myvmp_call_native_common
    add x10, x25, x10
    str x10, [sp, #240]
myvmp_call_native_common:
    mov x19, sp
    mov x20, x8
    mov x21, x9
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp q0, q1, [sp, #272]
    ldp q2, q3, [sp, #304]
    ldp q4, q5, [sp, #336]
    ldp q6, q7, [sp, #368]
    ldr x10, [sp, #248]
    mov sp, x10
    blr x22
    mov x23, x8
    mov x24, x9
    mov sp, x19
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp q0, q1, [sp, #272]
    stp q2, q3, [sp, #304]
    stp q4, q5, [sp, #336]
    stp q6, q7, [sp, #368]
    stp x23, x24, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    str x18, [sp, #144]
    mrs x10, nzcv
    str x10, [sp, #264]
    mov x8, x20
    mov x9, x21
    b myvmp_dispatch

myvmp_load_acquire:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    cmp w13, #1
    b.eq 260f
    cmp w13, #2
    b.eq 261f
    cmp w13, #4
    b.eq 262f
    ldar x15, [x14]
    b 263f
260: ldarb w15, [x14]
    b 263f
261: ldarh w15, [x14]
    b 263f
262: ldar w15, [x14]
263: str x15, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_store_release:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [sp, x11, lsl #3]
    ldr x15, [sp, x12, lsl #3]
    cmp w13, #1
    b.eq 264f
    cmp w13, #2
    b.eq 265f
    cmp w13, #4
    b.eq 266f
    stlr x14, [x15]
    b myvmp_dispatch
264: stlrb w14, [x15]
    b myvmp_dispatch
265: stlrh w14, [x15]
    b myvmp_dispatch
266: stlr w14, [x15]
    b myvmp_dispatch

myvmp_load_exclusive:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldr x14, [sp, x12, lsl #3]
    tbnz w13, #7, 271f
    cmp w13, #1
    b.eq 267f
    cmp w13, #2
    b.eq 268f
    cmp w13, #4
    b.eq 269f
    ldxr x15, [x14]
    b 270f
267: ldxrb w15, [x14]
    b 270f
268: ldxrh w15, [x14]
    b 270f
269: ldxr w15, [x14]
    b 270f
271: and w13, w13, #0x0f
    cmp w13, #1
    b.eq 272f
    cmp w13, #2
    b.eq 273f
    cmp w13, #4
    b.eq 274f
    ldaxr x15, [x14]
    b 270f
272: ldaxrb w15, [x14]
    b 270f
273: ldaxrh w15, [x14]
    b 270f
274: ldaxr w15, [x14]
270: str x15, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_store_exclusive:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldr x15, [sp, x12, lsl #3]
    ldr x16, [sp, x13, lsl #3]
    tbnz w14, #7, 279f
    cmp w14, #1
    b.eq 275f
    cmp w14, #2
    b.eq 276f
    cmp w14, #4
    b.eq 277f
    stxr w17, x15, [x16]
    b 278f
275: stxrb w17, w15, [x16]
    b 278f
276: stxrh w17, w15, [x16]
    b 278f
277: stxr w17, w15, [x16]
    b 278f
279: and w14, w14, #0x0f
    cmp w14, #1
    b.eq 280f
    cmp w14, #2
    b.eq 281f
    cmp w14, #4
    b.eq 282f
    stlxr w17, x15, [x16]
    b 278f
280: stlxrb w17, w15, [x16]
    b 278f
281: stlxrh w17, w15, [x16]
    b 278f
282: stlxr w17, w15, [x16]
278: str x17, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_load_exclusive_pair:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldr x15, [sp, x13, lsl #3]
    and w6, w14, #0x0f
    tbnz w14, #7, 407f
    cmp w6, #4
    b.eq 408f
    ldxp x16, x17, [x15]
    b 410f
408: ldxp w16, w17, [x15]
    b 410f
407: cmp w6, #4
    b.eq 409f
    ldaxp x16, x17, [x15]
    b 410f
409: ldaxp w16, w17, [x15]
410: str x16, [sp, x11, lsl #3]
    str x17, [sp, x12, lsl #3]
    b myvmp_dispatch

myvmp_store_exclusive_pair:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w15, [x9], #1
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    ldr x18, [sp, x14, lsl #3]
    and w6, w15, #0x0f
    tbnz w15, #7, 411f
    cmp w6, #4
    b.eq 412f
    stxp w7, x16, x17, [x18]
    b 414f
412: stxp w7, w16, w17, [x18]
    b 414f
411: cmp w6, #4
    b.eq 413f
    stlxp w7, x16, x17, [x18]
    b 414f
413: stlxp w7, w16, w17, [x18]
414: str x7, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_compare_swap:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldr x15, [sp, x11, lsl #3]
    ldr x16, [sp, x12, lsl #3]
    ldr x17, [sp, x13, lsl #3]
    and w6, w14, #0x0f
    lsr w7, w14, #6
    and w7, w7, #3
    cmp w6, #1
    b.eq 283f
    cmp w6, #2
    b.eq 288f
    cmp w6, #4
    b.eq 293f
    cbz w7, 298f
    cmp w7, #1
    b.eq 299f
    cmp w7, #2
    b.eq 300f
    casal x15, x16, [x17]
    b 302f
298: cas x15, x16, [x17]
    b 302f
299: casa x15, x16, [x17]
    b 302f
300: casl x15, x16, [x17]
    b 302f
283: cbz w7, 284f
    cmp w7, #1
    b.eq 285f
    cmp w7, #2
    b.eq 286f
    casalb w15, w16, [x17]
    b 302f
284: casb w15, w16, [x17]
    b 302f
285: casab w15, w16, [x17]
    b 302f
286: caslb w15, w16, [x17]
    b 302f
288: cbz w7, 289f
    cmp w7, #1
    b.eq 290f
    cmp w7, #2
    b.eq 291f
    casalh w15, w16, [x17]
    b 302f
289: cash w15, w16, [x17]
    b 302f
290: casah w15, w16, [x17]
    b 302f
291: caslh w15, w16, [x17]
    b 302f
293: cbz w7, 294f
    cmp w7, #1
    b.eq 295f
    cmp w7, #2
    b.eq 296f
    casal w15, w16, [x17]
    b 302f
294: cas w15, w16, [x17]
    b 302f
295: casa w15, w16, [x17]
    b 302f
296: casl w15, w16, [x17]
302: str x15, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_load_fp_imm64:
    ldrb w11, [x9], #1
    ldr x12, [x9], #8
    add x13, sp, #272
    fmov d16, x12
    str q16, [x13, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_unary:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    cbz w13, 303f
    cmp w13, #1
    b.eq 304f
    cmp w13, #2
    b.eq 305f
    cmp w13, #3
    b.eq 306f
    cmp w13, #4
    b.eq 307f
    cmp w13, #5
    b.eq 308f
    cmp w13, #6
    b.eq 309f
    fcvt s16, d16
    b 310f
303: tbnz w28, #21, 311f
    fabs s16, s16
    b 310f
311: fmov w6, s16
    and w6, w6, #0x7fffffff
    fmov s16, w6
    b 310f
304: tbnz w28, #21, 312f
    fabs d16, d16
    b 310f
312: fmov x6, d16
    and x6, x6, #0x7fffffffffffffff
    fmov d16, x6
    b 310f
305: tbnz w28, #21, 313f
    fneg s16, s16
    b 310f
313: fmov w6, s16
    eor w6, w6, #0x80000000
    fmov s16, w6
    b 310f
306: tbnz w28, #21, 314f
    fneg d16, d16
    b 310f
314: fmov x6, d16
    eor x6, x6, #0x8000000000000000
    fmov d16, x6
    b 310f
307: fsqrt s16, s16
    b 310f
308: fsqrt d16, d16
    b 310f
309: fcvt d16, s16
310: str q16, [x15, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_convert:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    cmp w13, #8
    b.hs 323f
    ldr x14, [sp, x12, lsl #3]
    cbz w13, 315f
    cmp w13, #1
    b.eq 316f
    cmp w13, #2
    b.eq 317f
    cmp w13, #3
    b.eq 318f
    cmp w13, #4
    b.eq 319f
    cmp w13, #5
    b.eq 320f
    cmp w13, #6
    b.eq 321f
    ucvtf d16, x14
    b 322f
315: scvtf s16, w14
    b 322f
316: scvtf d16, w14
    b 322f
317: scvtf s16, x14
    b 322f
318: scvtf d16, x14
    b 322f
319: ucvtf s16, w14
    b 322f
320: ucvtf d16, w14
    b 322f
321: ucvtf s16, x14
322: add x15, sp, #272
    str q16, [x15, x11, lsl #4]
    b myvmp_dispatch
323: add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    cmp w13, #8
    b.eq 324f
    cmp w13, #9
    b.eq 325f
    cmp w13, #10
    b.eq 326f
    cmp w13, #11
    b.eq 327f
    cmp w13, #12
    b.eq 328f
    cmp w13, #13
    b.eq 329f
    cmp w13, #14
    b.eq 330f
    fcvtzu x14, d16
    b 331f
324: fcvtzs w14, s16
    b 331f
325: fcvtzs w14, d16
    b 331f
326: fcvtzs x14, s16
    b 331f
327: fcvtzs x14, d16
    b 331f
328: fcvtzu w14, s16
    b 331f
329: fcvtzu w14, d16
    b 331f
330: fcvtzu x14, s16
331: str x14, [sp, x11, lsl #3]
    b myvmp_dispatch

myvmp_fp_min_max:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    ldr q17, [x15, x13, lsl #4]
    cbz w14, 332f
    cmp w14, #1
    b.eq 333f
    cmp w14, #2
    b.eq 334f
    cmp w14, #3
    b.eq 336f
    cmp w14, #4
    b.eq 337f
    cmp w14, #5
    b.eq 338f
    cmp w14, #6
    b.eq 339f
    fmaxnm d16, d16, d17
    b 335f
336: fmax d16, d16, d17
    b 335f
337: fminnm s16, s16, s17
    b 335f
338: fminnm d16, d16, d17
    b 335f
339: fmaxnm s16, s16, s17
    b 335f
332: fmin s16, s16, s17
    b 335f
333: fmin d16, d16, d17
    b 335f
334: fmax s16, s16, s17
335: str q16, [x15, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_conditional_compare:
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w11, [x9], #1
    ldrb w14, [x9], #1
    mov w18, w14
    ldrb w15, [x9], #1
    ldrb w16, [x9], #1
    // Evaluate against VM NZCV before executing the FP comparison. FCCMPE differs only
    // in exception signaling; both forms produce identical flags for non-trapping code.
    bl myvmp_condition_true
    cbz w0, 400f
    add x17, sp, #272
    ldr q16, [x17, x12, lsl #4]
    ldr q17, [x17, x13, lsl #4]
    cmp w15, #8
    b.eq 401f
    cbnz w16, 402f
    fcmp s16, s17
    b 403f
402: fcmpe s16, s17
    b 403f
401: cbnz w16, 404f
    fcmp d16, d17
    b 403f
404: fcmpe d16, d17
403: mrs x14, nzcv
    str x14, [sp, #264]
    b myvmp_dispatch
400: lsl x18, x18, #28
    str x18, [sp, #264]
    b myvmp_dispatch

myvmp_fp_select:
    ldrb w12, [x9], #1
    mov w19, w12
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    ldrb w11, [x9], #1
    ldrb w15, [x9], #1
    bl myvmp_condition_true
    cmp w0, #0
    csel w12, w13, w14, ne
    add x17, sp, #272
    add x16, x17, x12, lsl #4
    add x18, x17, x19, lsl #4
    movi v16.2d, #0
    cmp w15, #8
    b.eq 405f
    ldr s16, [x16]
    str q16, [x18]
    b myvmp_dispatch
405: ldr d16, [x16]
    str q16, [x18]
    b myvmp_dispatch

myvmp_fp_add64:
    mov w14, #0
    b myvmp_fp_binary64
myvmp_fp_sub64:
    mov w14, #1
    b myvmp_fp_binary64
myvmp_fp_mul64:
    mov w14, #2
    b myvmp_fp_binary64
myvmp_fp_div64:
    mov w14, #3
myvmp_fp_binary64:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    ldr q17, [x15, x13, lsl #4]
    cbz w14, 201f
    cmp w14, #1
    b.eq 202f
    cmp w14, #2
    b.eq 203f
    fdiv d16, d16, d17
    b 204f
201: fadd d16, d16, d17
    b 204f
202: fsub d16, d16, d17
    b 204f
203: fmul d16, d16, d17
204: str q16, [x15, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_madd64:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    ldr q17, [x15, x13, lsl #4]
    ldr q18, [x15, x14, lsl #4]
    fmadd d16, d16, d17, d18
    str q16, [x15, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_compare64:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x11, lsl #4]
    ldr q17, [x15, x12, lsl #4]
    fcmpe d16, d17
    mrs x10, nzcv
    str x10, [sp, #264]
    b myvmp_dispatch

myvmp_load_fp_imm32:
    ldrb w11, [x9], #1
    ldr w12, [x9], #8
    add x13, sp, #272
    fmov s16, w12
    str q16, [x13, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_add32:
    mov w14, #0
    b myvmp_fp_binary32
myvmp_fp_sub32:
    mov w14, #1
    b myvmp_fp_binary32
myvmp_fp_mul32:
    mov w14, #2
    b myvmp_fp_binary32
myvmp_fp_div32:
    mov w14, #3
myvmp_fp_binary32:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    ldr q17, [x15, x13, lsl #4]
    cbz w14, 211f
    cmp w14, #1
    b.eq 212f
    cmp w14, #2
    b.eq 213f
    fdiv s16, s16, s17
    b 214f
211: fadd s16, s16, s17
    b 214f
212: fsub s16, s16, s17
    b 214f
213: fmul s16, s16, s17
214: str q16, [x15, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_madd32:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    ldrb w13, [x9], #1
    ldrb w14, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x12, lsl #4]
    ldr q17, [x15, x13, lsl #4]
    ldr q18, [x15, x14, lsl #4]
    fmadd s16, s16, s17, s18
    str q16, [x15, x11, lsl #4]
    b myvmp_dispatch

myvmp_fp_compare32:
    ldrb w11, [x9], #1
    ldrb w12, [x9], #1
    add x15, sp, #272
    ldr q16, [x15, x11, lsl #4]
    ldr q17, [x15, x12, lsl #4]
    fcmpe s16, s17
    mrs x10, nzcv
    str x10, [sp, #264]
    b myvmp_dispatch

myvmp_call_vm:
    ldr w10, [x9], #4
    cmp x27, #256
    b.hs myvmp_bad_opcode
    str x9, [x26, x27, lsl #3]
    add x27, x27, #1
    add x9, x8, x10
    b myvmp_dispatch

// Input: x17 value, w14 extend kind. Output: extended x17.
myvmp_extend_x17:
    cbz w14, 134f
    cmp w14, #1
    b.eq 135f
    cmp w14, #2
    b.eq 136f
    cmp w14, #3
    b.eq 140f
    cmp w14, #4
    b.eq 137f
    cmp w14, #5
    b.eq 138f
    cmp w14, #6
    b.eq 139f
    ret
134: and x17, x17, #0xff
    ret
135: and x17, x17, #0xffff
    ret
136: mov w17, w17
    ret
137: sxtb x17, w17
    ret
138: sxth x17, w17
    ret
139: sxtw x17, w17
140: ret

// Input: w11 condition id. Output: w0 boolean. NZCV is restored from VM state only
// after the selector comparisons, so dispatch itself cannot corrupt the result.
myvmp_condition_true:
    cmp w11, #0
    b.eq 51f
    cmp w11, #1
    b.eq 52f
    cmp w11, #2
    b.eq 53f
    cmp w11, #3
    b.eq 54f
    cmp w11, #4
    b.eq 55f
    cmp w11, #5
    b.eq 56f
    cmp w11, #6
    b.eq 57f
    cmp w11, #7
    b.eq 58f
    cmp w11, #8
    b.eq 59f
    cmp w11, #9
    b.eq 60f
    cmp w11, #10
    b.eq 61f
    cmp w11, #11
    b.eq 62f
    cmp w11, #12
    b.eq 63f
    cmp w11, #13
    b.eq 64f
    mov w0, #1
    ret
51: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, eq
    ret
52: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, ne
    ret
53: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, hs
    ret
54: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, lo
    ret
55: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, mi
    ret
56: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, pl
    ret
57: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, vs
    ret
58: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, vc
    ret
59: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, hi
    ret
60: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, ls
    ret
61: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, ge
    ret
62: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, lt
    ret
63: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, gt
    ret
64: ldr x14, [sp, #264]
    msr nzcv, x14
    cset w0, le
    ret

myvmp_return:
    ldrb w11, [x9], #1
    cbz x27, 155f
    sub x27, x27, #1
    ldr x9, [x26, x27, lsl #3]
    b myvmp_dispatch
155:
    ldr x0, [sp, x11, lsl #3]
    ldr q0, [sp, #272]
    ldp q8, q9, [sp, #400]
    ldp q10, q11, [sp, #432]
    ldp q12, q13, [sp, #464]
    ldp q14, q15, [sp, #496]
    ldp x19, x20, [sp, #152]
    ldp x21, x22, [sp, #168]
    ldp x23, x24, [sp, #184]
    ldp x25, x26, [sp, #200]
    ldp x27, x28, [sp, #216]
    ldr x29, [sp, #232]
    ldr x30, [sp, #240]
    add sp, sp, #784
    add sp, sp, #32, lsl #12
    add sp, sp, #1, lsl #12
    ret

myvmp_jump_block:
    ldr w11, [x9], #4
    add x9, x8, x11
    b myvmp_dispatch

myvmp_halt:
    ldr x0, [sp, #0]
    ldr q0, [sp, #272]
    ldp q8, q9, [sp, #400]
    ldp q10, q11, [sp, #432]
    ldp q12, q13, [sp, #464]
    ldp q14, q15, [sp, #496]
    ldp x19, x20, [sp, #152]
    ldp x21, x22, [sp, #168]
    ldp x23, x24, [sp, #184]
    ldp x25, x26, [sp, #200]
    ldp x27, x28, [sp, #216]
    ldr x29, [sp, #232]
    ldr x30, [sp, #240]
    add sp, sp, #784
    add sp, sp, #32, lsl #12
    add sp, sp, #1, lsl #12
    ret

myvmp_bad_opcode:
    mov x0, #0
    ldp x19, x20, [sp, #152]
    ldp x21, x22, [sp, #168]
    ldp x23, x24, [sp, #184]
    ldp x25, x26, [sp, #200]
    ldp x27, x28, [sp, #216]
    ldr x29, [sp, #232]
    ldr x30, [sp, #240]
    add sp, sp, #784
    add sp, sp, #32, lsl #12
    add sp, sp, #1, lsl #12
    ret

.p2align 2
myvmp_bytecode:
.size myvmp_aarch64_entry, .-myvmp_aarch64_entry
