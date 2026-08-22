#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_branch_zero_ops(uint64_t x) {
    uint64_t acc = 0;
    while (x != 0) {
        acc += x & 7u;
        x >>= 1;
    }
    if (acc == 0) {
        return 99;
    }
    return acc;
}

static uint64_t ref_branch_zero_ops(uint64_t x) {
    uint64_t acc = 0;
    while (x != 0) {
        acc += x & 7u;
        x >>= 1;
    }
    if (acc == 0) {
        return 99;
    }
    return acc;
}

int main(void) {
    const uint64_t got = candidate_branch_zero_ops(0x1234u);
    const uint64_t expected = ref_branch_zero_ops(0x1234u);
    if (got != expected) {
        fprintf(stderr, "candidate_branch_zero_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_branch_zero_ops=%llu\n", (unsigned long long)got);
    return 0;
}
