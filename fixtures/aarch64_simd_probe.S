    .arch armv8-a+simd
    .text
    .p2align 4
    .global candidate_aarch64_simd
    .type candidate_aarch64_simd, %function
// x0: 64-byte input, x1: 256-byte output, x2: non-zero selects the full path.
candidate_aarch64_simd:
    sub sp, sp, #16
    str x30, [sp]
    ldr q0, [x0]
    ldr q1, [x0, #16]
    cbz x2, 1f
    add v2.4s, v0.4s, v1.4s
    sub v3.8h, v0.8h, v1.8h
    and v4.16b, v0.16b, v1.16b
    orr v5.16b, v0.16b, v1.16b
    eor v6.16b, v0.16b, v1.16b
    cmeq v7.4s, v0.4s, v1.4s
    cmgt v8.2d, v0.2d, v1.2d
    movi v12.2d, #0
    bsl v12.16b, v4.16b, v5.16b
    ushr v9.4s, v0.4s, #7
    shl v10.8h, v1.8h, #3
    ext v11.16b, v0.16b, v1.16b, #5
    str q2, [x1]
    str q3, [x1, #16]
    str q4, [x1, #32]
    str q5, [x1, #48]
    str q6, [x1, #64]
    str q7, [x1, #80]
    str q8, [x1, #96]
    str q9, [x1, #112]
    str q10, [x1, #128]
    str q11, [x1, #144]
1:
    ldr q0, [x0, #32]
    ldr q1, [x0, #48]
    bl aarch64_simd_native_bridge
    fadd v2.4s, v0.4s, v1.4s
    fsub v3.2d, v0.2d, v1.2d
    fmul v4.4s, v0.4s, v1.4s
    fdiv v5.2d, v0.2d, v1.2d
    str q2, [x1, #160]
    str q3, [x1, #176]
    str q4, [x1, #192]
    str q5, [x1, #208]
    movi v0.2d, #0
    str q0, [x1, #224]
    str q12, [x1, #240]
    mov x0, #0x5349
    ldr x30, [sp]
    add sp, sp, #16
    ret
    .size candidate_aarch64_simd, .-candidate_aarch64_simd

    .p2align 2
    .global aarch64_simd_native_bridge
    .type aarch64_simd_native_bridge, %function
aarch64_simd_native_bridge:
    eor v0.16b, v0.16b, v1.16b
    ret
    .size aarch64_simd_native_bridge, .-aarch64_simd_native_bridge
