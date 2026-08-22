    .option norvc
    .text
    .globl rv64_far_target
    .type rv64_far_target,@function
rv64_far_target:
    addi a0,a0,7
    slli a0,a0,1
    xori a0,a0,0x55
    ret
    .size rv64_far_target,.-rv64_far_target

    /* Force the appended runtime outside JAL's +/-1 MiB range without creating
     * millions of decoded instructions in the protected function itself. */
    .balign 16
    .space 0x120000,0x13
    .globl rv64_far_padding_end
rv64_far_padding_end:
    ret
