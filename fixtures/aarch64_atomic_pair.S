.text
.global candidate_atomic_pair
.type candidate_atomic_pair, %function
candidate_atomic_pair:
1:  ldaxp x1, x2, [x0]
    add x1, x1, #1
    add x2, x2, #2
    stlxp w3, x1, x2, [x0]
    cbnz w3, 1b
    eor x0, x1, x2
    ret
.size candidate_atomic_pair, .-candidate_atomic_pair
