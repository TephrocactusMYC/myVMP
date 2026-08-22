    .option norvc
    .option norelax
    .text
    .globl myvmp_riscv64_entry
    .type myvmp_riscv64_entry,@function

/* RV64 position-independent VM. The appended header contains two independent
 * opcode/operand domains followed by {start,end,owner} entries and ciphertext.
 * The current bytecode PC selects the domain at every dispatch.
 */
myvmp_riscv64_entry:
    mv t6,t0
    li t0, 66048
    sub sp, sp, t0
    sd s0, 56(sp)
    sd s1, 64(sp)
    sd s2, 136(sp)
    sd s3, 144(sp)
    sd s4, 152(sp)
    sd s5, 160(sp)
    sd s6, 168(sp)
    sd s7, 176(sp)
    sd s8, 184(sp)
    sd s9, 192(sp)
    sd s10, 200(sp)
    sd s11, 208(sp)
    mv s2, sp
    add s3, sp, 512
    add t1, sp, t0
    sd ra, 0(s2)
    sd t1, 8(s2)
    sd gp, 16(s2)
    sd tp, 24(s2)
    sd t0, 32(s2)
    sd t1, 40(s2)
    sd t2, 48(s2)
    sd a0, 72(s2)
    sd a1, 80(s2)
    sd a2, 88(s2)
    sd a3, 96(s2)
    sd a4, 104(s2)
    sd a5, 112(s2)
    sd a6, 120(s2)
    sd a7, 128(s2)
    sd t3, 216(s2)
    sd t4, 224(s2)
    sd t5, 232(s2)
    sd t6, 240(s2)
    sd zero, 256(s2)
    sd zero, 280(s2)
    sd zero, 440(s2)
    sd zero,456(s2)
    sd zero,464(s2)
    /* JAL returns entry+4; AUIPC/JALR returns entry+8. Detect the latter
     * from the instruction immediately preceding the link address. */
    lwu t5,-4(t6)
    andi t5,t5,0x7f
    li t4,0x67
    beq t5,t4,.Lfar_detour_link
    addi t6,t6,-4
    j .Ldetour_link_ready
.Lfar_detour_link:
    addi t6,t6,-8
.Ldetour_link_ready:
    sd t6,416(s2)

.Lheader_pc:
    auipc s0, %pcrel_hi(myvmp_riscv64_header)
    addi s0, s0, %pcrel_lo(.Lheader_pc)
    lwu s5, 0(s0)
    lwu s6, 4(s0)
    ld t0,8(s0)
    ld t1,416(s2)
    sub t0,t1,t0
    sd t0,424(s2) /* ASLR load bias */
    lwu s11,16(s0)
    addi s4,s0,24
    addi s8,s0,280
    addi s7,s0,536
    addi s9,s0,792
    addi s10,s0,1048
    li t0,12
    mul t0,s11,t0
    add s1,s10,t0
    li t0, 0
    li t5, 0x9e3779b9
.Ldecrypt:
    bgeu t0, s5, .Ldecoded
    lbu t1, 0(s1)
    mulw t2, t0, t5
    xor t2, t2, s6
    slli t2, t2, 32
    srli t2, t2, 32
    andi t3, t0, 3
    slli t3, t3, 3
    srl t2, t2, t3
    xor t1, t1, t2
    sb t1, 0(s3)
    addi t0, t0, 1
    addi s1, s1, 1
    addi s3, s3, 1
    j .Ldecrypt
.Ldecoded:
    addi s3, s2, 512
    mv s1, s3

.Ldispatch:
    /* Re-encode the previous operand window before exposing the next one. */
    ld t1,440(s2); beqz t1,.Loperand_previous_done
    ld t2,432(s2); ld t3,448(s2); li t4,0
.Loperand_reencode:
    lbu t5,0(t2); add t6,t3,t4; xor t5,t5,t6; sb t5,0(t2)
    addi t2,t2,1; addi t4,t4,0xa7; addi t1,t1,-1; bnez t1,.Loperand_reencode
.Loperand_previous_done:
    /* Resolve the active sibling VM from the physical block containing PC. */
    sub a4,s1,s3
    ld t1,456(s2)
    ld t2,464(s2)
    bltu a4,t1,.Lowner_cache_miss
    bgeu a4,t2,.Lowner_cache_miss
    ld a7,472(s2)
    beqz a7,.Lowner_primary
    mv t3,s8
    mv t4,s9
    j .Lowner_selected
.Lowner_cache_miss:
    mv a5,s10
    mv a6,s11
.Lowner_scan:
    beqz a6,.Lbad_dispatch
    lwu t1,0(a5)
    lwu t2,4(a5)
    bltu a4,t1,.Lowner_next
    bgeu a4,t2,.Lowner_next
    lwu a7,8(a5)
    sd t1,456(s2)
    sd t2,464(s2)
    sd a7,472(s2)
    beqz a7,.Lowner_primary
    mv t3,s8
    mv t4,s9
    j .Lowner_selected
.Lowner_primary:
    mv t3,s4
    mv t4,s7
.Lowner_selected:
    lbu t0, 0(s1)
    /* Physical opcodes are independently whitened per CFG block. The mask is
     * reconstructed from encrypted-header state rather than serialized. */
    li a5,0x3d
    mul a5,t1,a5
    li a6,0xc7
    mul a6,t2,a6
    xor a5,a5,a6
    li a6,0x6d
    mul a6,a7,a6
    xor a5,a5,a6
    xor a5,a5,s6
    xor t0,t0,a5
    andi t0,t0,0xff
    addi s1, s1, 1
    add t1,t3,t0
    lbu t6, 0(t1)
    add t1,t4,t0; lbu t1,0(t1)
    sd s1,432(s2); sd t1,440(s2)
    sub t2,s1,s3; addi t2,t2,-1; li t3,0x9b; mul t2,t2,t3
    li t3,0x5d; mul t3,t0,t3; xor t2,t2,t3; xor t2,t2,s6; andi t2,t2,0xff
    li t3,0x6d; mul t3,a7,t3; xor t2,t2,t3
    sd t2,448(s2); mv t3,s1; mv t4,t2; mv t5,t1
.Loperand_decode:
    beqz t5,.Loperand_decoded
    lbu t0,0(t3); xor t0,t0,t4; sb t0,0(t3); addi t3,t3,1; addi t4,t4,0xa7; addi t5,t5,-1; j .Loperand_decode
.Loperand_decoded:
    mv t0,t6
    li t1, 0x11
    beq t0, t1, .Lload_imm
    li t1, 0xe3
    beq t0, t1, .Lload_address
    li t1, 0x22
    beq t0, t1, .Ladd_imm
    li t1, 0x33
    beq t0, t1, .Lsub_imm
    li t1, 0xb2
    beq t0, t1, .Land_imm
    li t1, 0xda
    beq t0, t1, .Lor_imm
    li t1, 0xdb
    beq t0, t1, .Lxor_imm
    li t1, 0x66
    beq t0, t1, .Ladd_reg
    li t1, 0xab
    beq t0, t1, .Lsub_reg
    li t1, 0x77
    beq t0, t1, .Land_reg
    li t1, 0x88
    beq t0, t1, .Lor_reg
    li t1, 0x99
    beq t0, t1, .Lxor_reg
    li t1, 0xb1
    beq t0, t1, .Lshift
    li t1, 0xc9
    beq t0, t1, .Lshift32
    li t1, 0xd8
    beq t0, t1, .Lshift_variable
    li t1, 0xd9
    beq t0, t1, .Ldivide
    li t1, 0xb8
    beq t0, t1, .Lmul_high
    li t1, 0xcf
    beq t0, t1, .Lmul_add
    li t1, 0xad
    beq t0, t1, .Lcompare_imm
    li t1, 0xbe
    beq t0, t1, .Lcompare_reg
    li t1, 0x05
    beq t0, t1, .Lcompare_imm_branch
    li t1, 0x07
    beq t0, t1, .Lcompare_reg_branch
    li t1, 0x06
    beq t0, t1, .Ljump_block
    li t1, 0xaf
    beq t0, t1, .Lbranch
    li t1, 0xb0
    beq t0, t1, .Lselect
    li t1, 0xbf
    beq t0, t1, .Lload_mem
    li t1, 0xc0
    beq t0, t1, .Lstore_mem
    li t1, 0xe1
    beq t0, t1, .Lcall_native
    li t1, 0xe2
    beq t0, t1, .Lcall_native_reg
    li t1, 0xe4
    beq t0, t1, .Lcall_vm
    li t1, 0xd3
    beq t0, t1, .Ldispatch
    li t1, 0x44
    beq t0, t1, .Lreturn
    li t1, 0x55
    beq t0, t1, .Lhalt
.Lbad_dispatch:
    ebreak

.Lowner_next:
    addi a5,a5,12
    addi a6,a6,-1
    j .Lowner_scan

/* Read little-endian u64 from s1 into t0. */
.Lread64:
    li t0, 0
    li t1, 0
.Lread64_loop:
    lbu t2, 0(s1)
    sll t2, t2, t1
    or t0, t0, t2
    addi s1, s1, 1
    addi t1, t1, 8
    li t2, 64
    blt t1, t2, .Lread64_loop
    ret

.Lload_imm:
    lbu t3, 0(s1)
    addi s1, s1, 1
    jal ra, .Lread64
    slli t3, t3, 3
    add t3, s2, t3
    sd t0, 0(t3)
    j .Ldispatch
.Lload_address:
    lbu t3,0(s1); addi s1,s1,1; jal ra,.Lread64
    ld t1,424(s2); add t0,t0,t1; slli t3,t3,3; add t3,s2,t3; sd t0,0(t3); j .Ldispatch

.Ladd_imm: li t6, 0; j .Limm
.Lsub_imm: li t6, 1; j .Limm
.Land_imm: li t6, 2; j .Limm
.Lor_imm:  li t6, 3; j .Limm
.Lxor_imm: li t6, 4
.Limm:
    lbu t3, 0(s1)
    lbu t4, 1(s1)
    addi s1, s1, 2
    jal ra, .Lread64
    slli t4, t4, 3
    add t4, s2, t4
    ld t4, 0(t4)
    beqz t6, .Limm_add
    li t1, 1; beq t6, t1, .Limm_sub
    li t1, 2; beq t6, t1, .Limm_and
    li t1, 3; beq t6, t1, .Limm_or
    xor t4, t4, t0; j .Limm_store
.Limm_add: add t4, t4, t0; j .Limm_store
.Limm_sub: sub t4, t4, t0; j .Limm_store
.Limm_and: and t4, t4, t0; j .Limm_store
.Limm_or:  or t4, t4, t0
.Limm_store:
    slli t3, t3, 3; add t3, s2, t3; sd t4, 0(t3); j .Ldispatch

.Ladd_reg: li t6, 0; j .Lbinary
.Lsub_reg: li t6, 1; j .Lbinary
.Land_reg: li t6, 2; j .Lbinary
.Lor_reg:  li t6, 3; j .Lbinary
.Lxor_reg: li t6, 4
.Lbinary:
    lbu t3, 0(s1); lbu t4, 1(s1); lbu t5, 2(s1); lbu t0, 3(s1); lbu t1, 4(s1)
    addi s1, s1, 5
    slli t4, t4, 3; add t4, s2, t4; ld t4, 0(t4)
    slli t5, t5, 3; add t5, s2, t5; ld t5, 0(t5)
    beqz t0, .Lbin_lsl
    li t2, 1; beq t0, t2, .Lbin_lsr
    li t2, 2; beq t0, t2, .Lbin_asr
    li t2,64; sub t2,t2,t1; srl t0,t5,t1; sll t5,t5,t2; or t5,t5,t0; j .Lbin_shifted
.Lbin_lsl: sll t5, t5, t1; j .Lbin_shifted
.Lbin_lsr: srl t5, t5, t1; j .Lbin_shifted
.Lbin_asr: sra t5, t5, t1
.Lbin_shifted:
    beqz t6, .Lbin_add
    li t0, 1; beq t6, t0, .Lbin_sub
    li t0, 2; beq t6, t0, .Lbin_and
    li t0, 3; beq t6, t0, .Lbin_or
    xor t4, t4, t5; j .Lbin_store
.Lbin_add: add t4, t4, t5; j .Lbin_store
.Lbin_sub: sub t4, t4, t5; j .Lbin_store
.Lbin_and: and t4, t4, t5; j .Lbin_store
.Lbin_or: or t4, t4, t5
.Lbin_store: slli t3, t3, 3; add t3, s2, t3; sd t4, 0(t3); j .Ldispatch

.Lshift:
    lbu t3, 0(s1); lbu t4, 1(s1); lbu t0, 2(s1); lbu t1, 3(s1); addi s1, s1, 4
    slli t4, t4, 3; add t4, s2, t4; ld t4, 0(t4)
    beqz t0, .Lshift_l; li t2, 1; beq t0, t2, .Lshift_r; sra t4, t4, t1; j .Lshift_store
.Lshift_l: sll t4, t4, t1; j .Lshift_store
.Lshift_r: srl t4, t4, t1
.Lshift_store: slli t3, t3, 3; add t3, s2, t3; sd t4, 0(t3); j .Ldispatch
.Lshift32:
    lbu t3, 0(s1); lbu t4, 1(s1); lbu t0, 2(s1); lbu t1, 3(s1); addi s1, s1, 4
    slli t4, t4, 3; add t4, s2, t4; lw t4, 0(t4)
    beqz t0, .Lshift32_l; li t2, 1; beq t0, t2, .Lshift32_r; sraw t4, t4, t1; j .Lshift32_store
.Lshift32_l: sllw t4, t4, t1; j .Lshift32_store
.Lshift32_r: srlw t4, t4, t1
.Lshift32_store: slli t3, t3, 3; add t3, s2, t3; sd t4, 0(t3); j .Ldispatch
.Lshift_variable:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); lbu t0,3(s1); lbu t1,4(s1); addi s1,s1,5
    slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); slli t5,t5,3; add t5,s2,t5; ld t5,0(t5)
    li t2,4; beq t1,t2,.Lvar32
    beqz t0,.Lvar_l; li t2,1; beq t0,t2,.Lvar_r; sra t4,t4,t5; j .Lvar_store
.Lvar_l: sll t4,t4,t5; j .Lvar_store
.Lvar_r: srl t4,t4,t5; j .Lvar_store
.Lvar32: beqz t0,.Lvar32_l; li t2,1; beq t0,t2,.Lvar32_r; sraw t4,t4,t5; j .Lvar_store
.Lvar32_l: sllw t4,t4,t5; j .Lvar_store
.Lvar32_r: srlw t4,t4,t5
.Lvar_store: slli t3,t3,3; add t3,s2,t3; sd t4,0(t3); j .Ldispatch

.Ldivide:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); lbu t0,3(s1); lbu t1,4(s1); addi s1,s1,5
    slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); slli t5,t5,3; add t5,s2,t5; ld t5,0(t5)
    beqz t5,.Ldiv_zero; li t2,4; beq t1,t2,.Ldiv32; beqz t0,.Ldivu64; div t4,t4,t5; j .Ldiv_store
.Ldivu64: divu t4,t4,t5; j .Ldiv_store
.Ldiv32: beqz t0,.Ldivu32; divw t4,t4,t5; j .Ldiv_store
.Ldivu32: divuw t4,t4,t5; j .Ldiv_store
.Ldiv_zero: li t4,0
.Ldiv_store: slli t3,t3,3; add t3,s2,t3; sd t4,0(t3); j .Ldispatch

.Lmul_high:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); addi s1,s1,3
    slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); slli t5,t5,3; add t5,s2,t5; ld t5,0(t5)
    mulhu t4,t4,t5; slli t3,t3,3; add t3,s2,t3; sd t4,0(t3); j .Ldispatch
.Lmul_add:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); lbu t0,3(s1); lbu t1,4(s1); lbu t2,5(s1); addi s1,s1,6
    slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); slli t5,t5,3; add t5,s2,t5; ld t5,0(t5)
    li t6,4; beq t1,t6,.Lmulw; mul t4,t4,t5; j .Lmul_acc
.Lmulw: mulw t4,t4,t5
.Lmul_acc: bnez t2,.Lmul_store; slli t0,t0,3; add t0,s2,t0; ld t0,0(t0); add t4,t4,t0
.Lmul_store: slli t3,t3,3; add t3,s2,t3; sd t4,0(t3); j .Ldispatch

.Lcompare_imm:
    lbu t3,0(s1); addi s1,s1,1; jal ra,.Lread64; slli t3,t3,3; add t3,s2,t3; ld t3,0(t3); sd t3,264(s2); sd t0,272(s2); j .Ldispatch
.Lcompare_reg:
    lbu t3,0(s1); lbu t4,1(s1); addi s1,s1,2; slli t3,t3,3; add t3,s2,t3; ld t3,0(t3); slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); sd t3,264(s2); sd t4,272(s2); j .Ldispatch
.Lcompare_imm_branch:
    lbu t3,0(s1); addi s1,s1,1; jal ra,.Lread64; slli t3,t3,3; add t3,s2,t3; ld t3,0(t3); mv t4,t0; j .Lbranch_tail
.Lcompare_reg_branch:
    lbu t3,0(s1); lbu t4,1(s1); addi s1,s1,2; slli t3,t3,3; add t3,s2,t3; ld t3,0(t3); slli t4,t4,3; add t4,s2,t4; ld t4,0(t4)
.Lbranch_tail:
    lbu t0,0(s1); lwu t1,1(s1); addi s1,s1,5; jal ra,.Lcondition; beqz t2,.Ldispatch; add s1,s3,t1; j .Ldispatch
.Lbranch:
    lbu t0,0(s1); lwu t1,1(s1); addi s1,s1,5; ld t3,264(s2); ld t4,272(s2); jal ra,.Lcondition; beqz t2,.Ldispatch; add s1,s3,t1; j .Ldispatch
.Ljump_block:
    lwu t1,0(s1); add s1,s3,t1; j .Ldispatch
.Lcondition:
    li t2,0; beqz t0,.Lcond_eq; li t5,1; beq t0,t5,.Lcond_ne; li t5,2; beq t0,t5,.Lcond_hs; li t5,3; beq t0,t5,.Lcond_lo; li t5,10; beq t0,t5,.Lcond_ge; li t5,11; beq t0,t5,.Lcond_lt; li t5,14; beq t0,t5,.Lcond_true; ret
.Lcond_eq: bne t3,t4,.Lcond_done; li t2,1; ret
.Lcond_ne: beq t3,t4,.Lcond_done; li t2,1; ret
.Lcond_hs: bltu t3,t4,.Lcond_done; li t2,1; ret
.Lcond_lo: bgeu t3,t4,.Lcond_done; li t2,1; ret
.Lcond_ge: blt t3,t4,.Lcond_done; li t2,1; ret
.Lcond_lt: bge t3,t4,.Lcond_done
.Lcond_true: li t2,1
.Lcond_done: ret

.Lselect:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); lbu t0,3(s1); lbu t6,4(s1); addi s1,s1,5
    ld a4,264(s2); ld a5,272(s2); mv a6,t3; mv t3,a4; mv t4,a5; jal ra,.Lcondition
    andi t1,t6,1; bnez t1,.Lselect_true_zero; slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); j .Lselect_true_ready
.Lselect_true_zero: li t4,0
.Lselect_true_ready: andi t1,t6,2; bnez t1,.Lselect_false_zero; slli t5,t5,3; add t5,s2,t5; ld t5,0(t5); j .Lselect_choose
.Lselect_false_zero: li t5,0
.Lselect_choose: beqz t2,.Lselect_false; mv t5,t4
.Lselect_false: slli a6,a6,3; add a6,s2,a6; sd t5,0(a6); j .Ldispatch

.Lload_mem:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); addi s1,s1,3; jal ra,.Lread64
    slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); add t4,t4,t0; li t0,1; beq t5,t0,.Llm1; li t0,2; beq t5,t0,.Llm2; li t0,4; beq t5,t0,.Llm4; ld t4,0(t4); j .Llm_store
.Llm1: lbu t4,0(t4); j .Llm_store
.Llm2: lhu t4,0(t4); j .Llm_store
.Llm4: lwu t4,0(t4)
.Llm_store: slli t3,t3,3; add t3,s2,t3; sd t4,0(t3); j .Ldispatch
.Lstore_mem:
    lbu t3,0(s1); lbu t4,1(s1); lbu t5,2(s1); addi s1,s1,3; jal ra,.Lread64; mv t6,t0; jal ra,.Lread64
    slli t3,t3,3; add t3,s2,t3; ld t3,0(t3); slli t4,t4,3; add t4,s2,t4; ld t4,0(t4); add t4,t4,t6
    li t0,1; beq t5,t0,.Lsm1; li t0,2; beq t5,t0,.Lsm2; li t0,4; beq t5,t0,.Lsm4; sd t3,0(t4); j .Lsm_post
.Lsm1: sb t3,0(t4); j .Lsm_post
.Lsm2: sh t3,0(t4); j .Lsm_post
.Lsm4: sw t3,0(t4)
.Lsm_post: beqz t0,.Ldispatch; /* post-index value was consumed; RV frontend always emits zero. */ j .Ldispatch

.Lcall_native:
    jal ra,.Lread64
    ld t1,424(s2); add t6,t0,t1
    jal ra,.Lread64 /* link address is metadata; native RA is the runtime continuation. */
    j .Lcall_native_common
.Lcall_native_reg:
    lbu t3,0(s1); addi s1,s1,1; slli t3,t3,3; add t3,s2,t3; ld t6,0(t3)
    jal ra,.Lread64
.Lcall_native_common:
    ld a0,72(s2); ld a1,80(s2); ld a2,88(s2); ld a3,96(s2)
    ld a4,104(s2); ld a5,112(s2); ld a6,120(s2); ld a7,128(s2)
    jalr ra,t6,0
    sd a0,72(s2); sd a1,80(s2); sd a2,88(s2); sd a3,96(s2)
    sd a4,104(s2); sd a5,112(s2); sd a6,120(s2); sd a7,128(s2)
    j .Ldispatch
.Lcall_vm:
    lwu t0,0(s1); addi s1,s1,4
    ld t1,280(s2); li t2,16; bgeu t1,t2,.Lbad
    slli t2,t1,3; addi t2,t2,288; add t2,s2,t2
    sub t3,s1,s3; sd t3,0(t2); addi t1,t1,1; sd t1,280(s2)
    add s1,s3,t0; j .Ldispatch
.Lbad: ebreak

.Lreturn:
    lbu t3,0(s1); slli t3,t3,3; add t3,s2,t3; ld a0,0(t3)
    ld t1,280(s2); beqz t1,.Lexit; addi t1,t1,-1; sd t1,280(s2)
    slli t2,t1,3; addi t2,t2,288; add t2,s2,t2; ld t3,0(t2); add s1,s3,t3; j .Ldispatch
.Lhalt:
    ld a0,72(s2)
.Lexit:
    mv t6,s2
    ld ra,0(t6)
    ld s0,56(t6); ld s1,64(t6); ld s3,144(t6); ld s4,152(t6); ld s5,160(t6); ld s6,168(t6); ld s7,176(t6); ld s8,184(t6); ld s9,192(t6); ld s10,200(t6); ld s11,208(t6)
    ld t0,8(t6)
    ld s2,136(t6)
    mv sp,t0
    ret

    .balign 8
myvmp_riscv64_header:
