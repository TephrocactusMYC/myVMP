#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_bitlogic_ops(uint64_t a, uint64_t b) {
    uint64_t x = ~a;
    uint64_t y = (a & ~b) ^ (b | ~a);
    uint64_t z = (int64_t)y >> 3;
    uint64_t rot = (a >> 13) | (a << 51);
    uint64_t signed_shift = (uint64_t)((int64_t)b >> 7);
    return (x ^ y) + z + rot + signed_shift;
}

static uint64_t ref_bitlogic_ops(uint64_t a, uint64_t b) {
    uint64_t x = ~a;
    uint64_t y = (a & ~b) ^ (b | ~a);
    uint64_t z = (uint64_t)((int64_t)y >> 3);
    uint64_t rot = (a >> 13) | (a << 51);
    uint64_t signed_shift = (uint64_t)((int64_t)b >> 7);
    return (x ^ y) + z + rot + signed_shift;
}

int main(void) {
    const uint64_t got = candidate_bitlogic_ops(0x1234567890ULL, 0xabcdef0123ULL);
    const uint64_t expected = ref_bitlogic_ops(0x1234567890ULL, 0xabcdef0123ULL);
    if (got != expected) {
        fprintf(stderr, "candidate_bitlogic_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_bitlogic_ops=%llu\n", (unsigned long long)got);
    return 0;
}
