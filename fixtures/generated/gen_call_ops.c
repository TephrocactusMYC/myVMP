#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static uint64_t call_helper(uint64_t a, uint64_t b) {
    return (a ^ 0x55aa55aaULL) + b * 3u;
}

__attribute__((noinline)) uint64_t candidate_call_ops(uint64_t a, uint64_t b) {
    const uint64_t called = call_helper(a + 11u, b ^ 7u);
    return (called << 1) + a;
}

int main(void) {
    const uint64_t got = candidate_call_ops(0x12345u, 0x6789u);
    const uint64_t expected = ((0x12345u + 11u) ^ 0x55aa55aaULL) + (0x6789u ^ 7u) * 3u;
    const uint64_t final_expected = (expected << 1) + 0x12345u;
    if (got != final_expected) {
        fprintf(stderr, "candidate_call_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)final_expected);
        return 1;
    }
    printf("candidate_call_ops=%llu\n", (unsigned long long)got);
    return 0;
}
