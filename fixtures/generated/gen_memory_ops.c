#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_memory_ops(uint64_t seed) {
    uint64_t values[4];
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] = seed + i * 13u;
    }

    uint64_t acc = 0;
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] ^= (i << 4);
        acc += values[i];
    }
    return acc;
}

static uint64_t ref_memory_ops(uint64_t seed) {
    uint64_t values[4];
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] = seed + i * 13u;
    }

    uint64_t acc = 0;
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] ^= (i << 4);
        acc += values[i];
    }
    return acc;
}

int main(void) {
    const uint64_t got = candidate_memory_ops(0x3456u);
    const uint64_t expected = ref_memory_ops(0x3456u);
    if (got != expected) {
        fprintf(stderr, "candidate_memory_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_memory_ops=%llu\n", (unsigned long long)got);
    return 0;
}
