#include <stdint.h>
#include <stdio.h>

static uint64_t ref_runtime_extended_ops(uint64_t a, uint64_t b) {
    const uint64_t x2 = a ^ 0xffu;
    const uint64_t x3 = x2 & (b << 1);
    const uint64_t x4 = x3 ^ ~(b >> 2);
    const uint64_t x5 = a * b + x4;
    const uint64_t x6 = x5 - a * b;
    const uint64_t x7 = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
    const uint64_t x8 = (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;
    const uint64_t x9 = (uint32_t)a >> 3;
    const uint64_t x10 = a > b ? x5 : x6 + 1;
    const uint64_t x11 = a <= b ? x7 : ~x8;
    const uint64_t x12 = a == b ? x3 : 0 - x4;
    return x10 + x11 + x12 + x9;
}

__attribute__((noinline)) uint64_t candidate_runtime_extended_ops(uint64_t a, uint64_t b) {
#if defined(__aarch64__)
    uint64_t result;
    __asm__ volatile(
        "eor x2, %x[a], #0xff\n"
        "and x3, x2, %x[b], lsl #1\n"
        "eon x4, x3, %x[b], lsr #2\n"
        "madd x5, %x[a], %x[b], x4\n"
        "msub x6, %x[a], %x[b], x5\n"
        "smull x7, %w[a], %w[b]\n"
        "umull x8, %w[a], %w[b]\n"
        "lsr w9, %w[a], #3\n"
        "cmp %x[a], %x[b]\n"
        "csinc x10, x5, x6, hi\n"
        "csinv x11, x7, x8, ls\n"
        "csneg x12, x3, x4, eq\n"
        "add x10, x10, x11\n"
        "add x10, x10, x12\n"
        "add %x[out], x10, x9\n"
        : [out] "=&r" (result)
        : [a] "r" (a), [b] "r" (b)
        : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "cc");
    return result;
#else
    return ref_runtime_extended_ops(a, b);
#endif
}

int main(void) {
    const uint64_t a = 0x81234567u;
    const uint64_t b = 0x12345u;
    const uint64_t got = candidate_runtime_extended_ops(a, b);
    const uint64_t expected = ref_runtime_extended_ops(a, b);
    if (got != expected) {
        fprintf(stderr, "candidate_runtime_extended_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_runtime_extended_ops=%llu\n", (unsigned long long)got);
    return 0;
}
