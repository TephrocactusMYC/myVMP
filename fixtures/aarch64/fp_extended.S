.text
.global fp_extended_probe
.type fp_extended_probe, %function
fp_extended_probe:
    fabs s0, s1
    fabs d2, d3
    fneg s4, s5
    fneg d6, d7
    fsqrt s8, s9
    fsqrt d10, d11
    fcvt d12, s13
    fcvt s14, d15
    scvtf s0, w1
    scvtf d2, w3
    scvtf s4, x5
    scvtf d6, x7
    ucvtf s8, w9
    ucvtf d10, w11
    ucvtf s12, x13
    ucvtf d14, x15
    fcvtzs w16, s17
    fcvtzs w18, d19
    fcvtzs x20, s21
    fcvtzs x22, d23
    fcvtzu w24, s25
    fcvtzu w26, d27
    fcvtzu x28, s29
    fcvtzu x0, d1
    fmin s2, s3, s4
    fmin d5, d6, d7
    fmax s8, s9, s10
    fmax d11, d12, d13
    fminnm s14, s15, s16
    fminnm d17, d18, d19
    fmaxnm s20, s21, s22
    fmaxnm d23, d24, d25
    ret
.size fp_extended_probe, .-fp_extended_probe
