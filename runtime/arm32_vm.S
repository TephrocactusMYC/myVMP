.syntax unified
.arm
.text
.global myvmp_arm32_entry
.type myvmp_arm32_entry, %function
myvmp_arm32_entry:
    sub sp, sp, #0x10000
    sub sp, sp, #160
    stmia sp, {r0-r12}
    add r0, sp, #160
    add r0, r0, #0x10000
    str r0, [sp, #124]
    str lr, [sp, #120]
    mov r0, #0
    str r0, [sp, #128]

    adr r4, .Lbytecode_delta
    ldr r5, [r4]
    add r4, r4, r5
    b .Lbytecode_ready
.Lbytecode_delta:
    .word myvmp_bytecode - .Lbytecode_delta
.Lbytecode_ready:
    ldr r5, [r4], #4
    ldr r6, [r4], #4
    add r8, sp, #160
    mov r0, #0
    movw r7, #0x79b9
    movt r7, #0x9e37
1:
    cmp r0, r5
    bhs 2f
    ldrb r1, [r4, r0]
    mul r2, r0, r7
    eor r2, r2, r6
    and r3, r0, #3
    lsl r3, r3, #3
    lsr r2, r2, r3
    eor r1, r1, r2
    strb r1, [r8, r0]
    add r0, r0, #1
    b 1b
2:
    mov r9, r8

myvmp_dispatch:
    ldrb r10, [r9], #1
    cmp r10, #0x11
    beq myvmp_load_imm
    cmp r10, #0x22
    beq myvmp_add_imm
    cmp r10, #0x33
    beq myvmp_sub_imm
    cmp r10, #0x99
    beq myvmp_xor_reg
    cmp r10, #0x77
    beq myvmp_and_reg
    cmp r10, #0x88
    beq myvmp_or_reg
    cmp r10, #0x66
    beq myvmp_add_reg
    cmp r10, #0xab
    beq myvmp_sub_reg
    cmp r10, #0xac
    beq myvmp_move_reg
    cmp r10, #0xca
    beq myvmp_shift32
    cmp r10, #0xcf
    beq myvmp_mul_add
    cmp r10, #0xad
    beq myvmp_compare_imm
    cmp r10, #0xbe
    beq myvmp_compare_reg
    cmp r10, #0xaf
    beq myvmp_branch_if
    cmp r10, #0xbf
    beq myvmp_load_mem
    cmp r10, #0xc0
    beq myvmp_store_mem
    cmp r10, #0x44
    beq myvmp_return
    cmp r10, #0x55
    beq myvmp_halt
    b myvmp_bad_opcode

myvmp_load_imm:
    ldrb r0, [r9], #1
    ldr r1, [r9], #8
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_add_imm:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldr r2, [r9], #8
    ldr r1, [sp, r1, lsl #2]
    add r1, r1, r2
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_sub_imm:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldr r2, [r9], #8
    ldr r1, [sp, r1, lsl #2]
    sub r1, r1, r2
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_xor_reg:
    mov r5, #0
    b myvmp_binary_reg
myvmp_and_reg:
    mov r5, #1
    b myvmp_binary_reg
myvmp_or_reg:
    mov r5, #2
    b myvmp_binary_reg
myvmp_add_reg:
    mov r5, #3
    b myvmp_binary_reg
myvmp_sub_reg:
    mov r5, #4
myvmp_binary_reg:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldrb r2, [r9], #1
    ldrb r3, [r9], #1
    ldrb r4, [r9], #1
    ldr r1, [sp, r1, lsl #2]
    ldr r2, [sp, r2, lsl #2]
    cmp r3, #0
    moveq r2, r2, lsl r4
    cmp r3, #1
    moveq r2, r2, lsr r4
    cmp r3, #2
    moveq r2, r2, asr r4
    cmp r3, #3
    moveq r2, r2, ror r4
    cmp r5, #0
    eoreq r1, r1, r2
    cmp r5, #1
    andeq r1, r1, r2
    cmp r5, #2
    orreq r1, r1, r2
    cmp r5, #3
    addeq r1, r1, r2
    cmp r5, #4
    subeq r1, r1, r2
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_move_reg:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldr r1, [sp, r1, lsl #2]
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_shift32:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldrb r2, [r9], #1
    ldrb r3, [r9], #1
    ldr r1, [sp, r1, lsl #2]
    cmp r2, #0
    moveq r1, r1, lsl r3
    cmp r2, #1
    moveq r1, r1, lsr r3
    cmp r2, #2
    moveq r1, r1, asr r3
    cmp r2, #3
    moveq r1, r1, ror r3
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_mul_add:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldrb r2, [r9], #1
    ldrb r3, [r9], #1
    ldrb r4, [r9], #1
    ldrb r5, [r9], #1
    ldr r1, [sp, r1, lsl #2]
    ldr r2, [sp, r2, lsl #2]
    mul r1, r1, r2
    cmp r5, #0
    ldreq r2, [sp, r3, lsl #2]
    addeq r1, r1, r2
    str r1, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_compare_imm:
    ldrb r0, [r9], #1
    ldr r1, [r9], #8
    ldr r0, [sp, r0, lsl #2]
    cmp r0, r1
    mrs r0, apsr
    str r0, [sp, #128]
    b myvmp_dispatch

myvmp_compare_reg:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldr r0, [sp, r0, lsl #2]
    ldr r1, [sp, r1, lsl #2]
    cmp r0, r1
    mrs r0, apsr
    str r0, [sp, #128]
    b myvmp_dispatch

myvmp_branch_if:
    ldrb r0, [r9], #1
    ldr r7, [r9], #4
    ldr r1, [sp, #128]
.Lcondition_call:
    adr lr, .Lcondition_return
    b myvmp_condition_true
.Lcondition_return:
    cmp r0, #0
    addne r9, r8, r7
    b myvmp_dispatch

// r0 condition, r1 saved APSR; returns r0 boolean.
myvmp_condition_true:
    mov r2, r1, lsr #28
    and r3, r2, #1          // V
    mov r4, r2, lsr #1
    and r4, r4, #1          // C
    mov r5, r2, lsr #2
    and r5, r5, #1          // Z
    mov r6, r2, lsr #3
    and r6, r6, #1          // N
    cmp r0, #0
    moveq r0, r5
    bxeq lr
    cmp r0, #1
    eoreq r0, r5, #1
    bxeq lr
    cmp r0, #2
    moveq r0, r4
    bxeq lr
    cmp r0, #3
    eoreq r0, r4, #1
    bxeq lr
    cmp r0, #4
    moveq r0, r6
    bxeq lr
    cmp r0, #5
    eoreq r0, r6, #1
    bxeq lr
    cmp r0, #6
    moveq r0, r3
    bxeq lr
    cmp r0, #7
    eoreq r0, r3, #1
    bxeq lr
    cmp r0, #8
    eoreq r0, r5, #1
    andeq r0, r4, r0
    bxeq lr
    cmp r0, #9
    eoreq r0, r4, #1
    orreq r0, r0, r5
    bxeq lr
    cmp r0, #10
    eoreq r0, r6, r3
    eoreq r0, r0, #1
    bxeq lr
    cmp r0, #11
    eoreq r0, r6, r3
    bxeq lr
    cmp r0, #12
    eor r2, r6, r3
    orr r2, r2, r5
    eoreq r0, r2, #1
    bxeq lr
    cmp r0, #13
    eor r2, r6, r3
    orr r2, r2, r5
    moveq r0, r2
    bxeq lr
    cmp r0, #14
    moveq r0, #1
    movne r0, #0
    bx lr

myvmp_load_mem:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldrb r2, [r9], #1
    ldr r3, [r9], #8
    ldr r1, [sp, r1, lsl #2]
    add r1, r1, r3
    cmp r2, #1
    ldrbeq r3, [r1]
    ldrne r3, [r1]
    str r3, [sp, r0, lsl #2]
    b myvmp_dispatch

myvmp_store_mem:
    ldrb r0, [r9], #1
    ldrb r1, [r9], #1
    ldrb r2, [r9], #1
    ldr r3, [r9], #8
    add r9, r9, #8
    ldr r0, [sp, r0, lsl #2]
    ldr r1, [sp, r1, lsl #2]
    add r1, r1, r3
    cmp r2, #1
    strbeq r0, [r1]
    strne r0, [r1]
    b myvmp_dispatch

myvmp_return:
    ldrb r0, [r9], #1
    ldr r0, [sp, r0, lsl #2]
    ldr r4, [sp, #16]
    ldr r5, [sp, #20]
    ldr r6, [sp, #24]
    ldr r7, [sp, #28]
    ldr r8, [sp, #32]
    ldr r9, [sp, #36]
    ldr r10, [sp, #40]
    ldr r11, [sp, #44]
    ldr lr, [sp, #120]
    ldr sp, [sp, #124]
    bx lr

myvmp_halt:
    mov r0, #0
    b myvmp_return_common
myvmp_bad_opcode:
    mov r0, #0
myvmp_return_common:
    ldr r4, [sp, #16]
    ldr r5, [sp, #20]
    ldr r6, [sp, #24]
    ldr r7, [sp, #28]
    ldr r8, [sp, #32]
    ldr r9, [sp, #36]
    ldr r10, [sp, #40]
    ldr r11, [sp, #44]
    ldr lr, [sp, #120]
    ldr sp, [sp, #124]
    bx lr

.p2align 2
myvmp_bytecode:
.size myvmp_arm32_entry, .-myvmp_arm32_entry
