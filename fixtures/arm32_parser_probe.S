.syntax unified
.arm
.text
.global candidate_arm32_probe
.type candidate_arm32_probe, %function
candidate_arm32_probe:
    add r0, r0, #5
    eor r0, r0, r1
    bx lr
.size candidate_arm32_probe, .-candidate_arm32_probe
