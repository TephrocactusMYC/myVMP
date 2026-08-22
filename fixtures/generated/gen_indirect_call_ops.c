#include <stdint.h>
#include <stdio.h>

typedef uint64_t (*binary_fn)(uint64_t, uint64_t);

__attribute__((noinline)) static uint64_t indirect_helper(uint64_t a, uint64_t b) {
    return (a * 5u) ^ (b + 0x1234u);
}

__attribute__((noinline)) uint64_t candidate_indirect_call_ops(binary_fn fn,
                                                                uint64_t a,
                                                                uint64_t b) {
#if defined(__aarch64__)
    uint64_t result;
    __asm__ volatile(
        "mov x0, %x[a]\n"
        "mov x1, %x[b]\n"
        "blr %x[fn]\n"
        "add %x[out], x0, #9\n"
        : [out] "=&r" (result)
        : [fn] "r" (fn), [a] "r" (a), [b] "r" (b)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
          "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30", "cc", "memory");
    return result;
#else
    return fn(a, b) + 9u;
#endif
}

int main(void) {
    const uint64_t got = candidate_indirect_call_ops(indirect_helper, 0x4567u, 0x89abu);
    const uint64_t expected = indirect_helper(0x4567u, 0x89abu) + 9u;
    if (got != expected) {
        fprintf(stderr, "candidate_indirect_call_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_indirect_call_ops=%llu\n", (unsigned long long)got);
    return 0;
}
