#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_muldiv_ops(uint64_t a, uint64_t b) {
    uint64_t x = (a * 17u) + (b / 3u);
    x ^= (a >> 5);
    x += (b << 2);
    x -= (a / 7u);
    x += a / (b | 1u);
    x ^= a << (b & 63u);
    x += (uint64_t)((int64_t)a / (int64_t)(b | 1u));
    return x;
}

static uint64_t ref_muldiv_ops(uint64_t a, uint64_t b) {
    uint64_t x = (a * 17u) + (b / 3u);
    x ^= (a >> 5);
    x += (b << 2);
    x -= (a / 7u);
    x += a / (b | 1u);
    x ^= a << (b & 63u);
    x += (uint64_t)((int64_t)a / (int64_t)(b | 1u));
    return x;
}

int main(void) {
    const uint64_t got = candidate_muldiv_ops(0x123456u, 0xabcdefu);
    const uint64_t expected = ref_muldiv_ops(0x123456u, 0xabcdefu);
    if (got != expected) {
        fprintf(stderr, "candidate_muldiv_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_muldiv_ops=%llu\n", (unsigned long long)got);
    return 0;
}
