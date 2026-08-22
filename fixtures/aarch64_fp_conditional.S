.text
.global candidate_fp_conditional
.type candidate_fp_conditional, %function
candidate_fp_conditional:
    // When d0 == d1 the NE condition is false and FCCMP installs fallback C=1.
    fcmp d0, d1
    fccmpe d2, d3, #2, ne
    fcsel d0, d2, d3, hs
    ret
.size candidate_fp_conditional, .-candidate_fp_conditional

.global candidate_fp_conditional_nv
.type candidate_fp_conditional_nv, %function
candidate_fp_conditional_nv:
    // Seed stale upper bits, then verify that an S-register FCSEL clears all of them.
    fmov d0, #-1.0
    fmov s1, #1.0
    fmov s2, #2.0
    fcsel s0, s1, s2, nv
    ret
.size candidate_fp_conditional_nv, .-candidate_fp_conditional_nv
