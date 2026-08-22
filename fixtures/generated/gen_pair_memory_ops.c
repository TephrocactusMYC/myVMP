#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_pair_memory_ops(uint64_t a, uint64_t b) {
    volatile uint64_t values[2];
    values[0] = a + 11u;
    values[1] = b + 17u;
    uint64_t first = values[0];
    uint64_t second = values[1];
    return first ^ second;
}

static uint64_t ref_pair_memory_ops(uint64_t a, uint64_t b) {
    uint64_t values[2];
    values[0] = a + 11u;
    values[1] = b + 17u;
    return values[0] ^ values[1];
}

int main(void) {
    const uint64_t got = candidate_pair_memory_ops(0x3456u, 0x789au);
    const uint64_t expected = ref_pair_memory_ops(0x3456u, 0x789au);
    if (got != expected) {
        fprintf(stderr, "candidate_pair_memory_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_pair_memory_ops=%llu\n", (unsigned long long)got);
    return 0;
}
