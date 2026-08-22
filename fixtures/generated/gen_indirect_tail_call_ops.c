#include <stdint.h>
#include <stdio.h>

typedef uint64_t (*tail_binary_fn)(uint64_t, uint64_t);

__attribute__((noinline)) static uint64_t indirect_tail_helper(uint64_t a, uint64_t b) {
    return (a + 0x7788u) ^ (b * 11u);
}

__attribute__((noinline)) uint64_t candidate_indirect_tail_call_ops(tail_binary_fn fn,
                                                                     uint64_t a,
                                                                     uint64_t b) {
    return fn(a, b);
}

int main(void) {
    const uint64_t got = candidate_indirect_tail_call_ops(indirect_tail_helper, 0x123u, 0x456u);
    const uint64_t expected = indirect_tail_helper(0x123u, 0x456u);
    if (got != expected) {
        fprintf(stderr, "candidate_indirect_tail_call_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_indirect_tail_call_ops=%llu\n", (unsigned long long)got);
    return 0;
}
