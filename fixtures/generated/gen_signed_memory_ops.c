#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) int64_t candidate_signed_memory_ops(int64_t seed) {
    volatile int8_t bytes[2];
    volatile int16_t halves[2];
    volatile int32_t words[2];
    bytes[0] = (int8_t)seed;
    bytes[1] = (int8_t)(seed >> 8);
    halves[0] = (int16_t)(seed >> 16);
    halves[1] = (int16_t)(seed >> 24);
    words[0] = (int32_t)(seed >> 7);
    words[1] = (int32_t)(seed >> 13);
    return (int64_t)bytes[0] - (int64_t)bytes[1] +
           (int64_t)halves[0] - (int64_t)halves[1] +
           (int64_t)words[0] - (int64_t)words[1];
}

static int64_t ref_signed_memory_ops(int64_t seed) {
    int8_t bytes[2];
    int16_t halves[2];
    int32_t words[2];
    bytes[0] = (int8_t)seed;
    bytes[1] = (int8_t)(seed >> 8);
    halves[0] = (int16_t)(seed >> 16);
    halves[1] = (int16_t)(seed >> 24);
    words[0] = (int32_t)(seed >> 7);
    words[1] = (int32_t)(seed >> 13);
    return (int64_t)bytes[0] - (int64_t)bytes[1] +
           (int64_t)halves[0] - (int64_t)halves[1] +
           (int64_t)words[0] - (int64_t)words[1];
}

int main(void) {
    const int64_t got = candidate_signed_memory_ops((int64_t)0xfedcba9876543210ULL);
    const int64_t expected = ref_signed_memory_ops((int64_t)0xfedcba9876543210ULL);
    if (got != expected) {
        fprintf(stderr, "candidate_signed_memory_ops mismatch: %lld != %lld\n",
                (long long)got,
                (long long)expected);
        return 1;
    }
    printf("candidate_signed_memory_ops=%lld\n", (long long)got);
    return 0;
}
