    .text
    .p2align 2
    .global aarch64_far_target
    .type aarch64_far_target,%function
aarch64_far_target:
    add x0,x0,#7
    lsl x0,x0,#1
    mov x1,#0x55
    eor x0,x0,x1
    ret
    .size aarch64_far_target,.-aarch64_far_target

    /* Keep the protected function small while forcing the runtime appended to
     * this executable segment beyond AArch64 B's +/-128 MiB range. */
    .section .text.zz_myvmp_far_padding,"ax",%progbits
    .p2align 4
    .space 0x8100000,0x1f
    .global aarch64_far_padding_end
aarch64_far_padding_end:
    ret
