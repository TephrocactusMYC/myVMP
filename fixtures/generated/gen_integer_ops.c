#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_integer_ops(uint64_t a, uint64_t b) {
    uint64_t x = a + 0x1234u;
    x = x - (b & 0xffu);
    x = x ^ (b << 3);
    x = x | 0x55aa55aa55aa55aaULL;
    x = x & 0x7fffffffffffffffULL;
    x = x + (a >> 2);
    x = x ^ (x << 7);
    return x;
}

static uint64_t ref_integer_ops(uint64_t a, uint64_t b) {
    uint64_t x = a + 0x1234u;
    x = x - (b & 0xffu);
    x = x ^ (b << 3);
    x = x | 0x55aa55aa55aa55aaULL;
    x = x & 0x7fffffffffffffffULL;
    x = x + (a >> 2);
    x = x ^ (x << 7);
    return x;
}

int main(void) {
    const uint64_t got = candidate_integer_ops(0x123456789ULL, 0xdeadbeefULL);
    const uint64_t expected = ref_integer_ops(0x123456789ULL, 0xdeadbeefULL);
    if (got != expected) {
        fprintf(stderr, "candidate_integer_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_integer_ops=%llu\n", (unsigned long long)got);
    return 0;
}
