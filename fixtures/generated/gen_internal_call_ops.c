#include <stdint.h>
#include <stdio.h>

#if defined(__aarch64__)
__asm__(
    ".text\n"
    ".global candidate_internal_call_ops\n"
    ".type candidate_internal_call_ops, %function\n"
    "candidate_internal_call_ops:\n"
    "mov x1, x0\n"
    "mov x2, x30\n"
    "bl 1f\n"
    "add x0, x0, x1\n"
    "mov x30, x2\n"
    "ret\n"
    "1: add x0, x0, #7\n"
    "ret\n"
    ".size candidate_internal_call_ops, .-candidate_internal_call_ops\n");
uint64_t candidate_internal_call_ops(uint64_t value);
#else
__attribute__((noinline)) uint64_t candidate_internal_call_ops(uint64_t value) {
    return (value + 7u) + value;
}
#endif

int main(void) {
    const uint64_t got = candidate_internal_call_ops(0x3456u);
    const uint64_t expected = (0x3456u + 7u) + 0x3456u;
    if (got != expected) {
        fprintf(stderr, "candidate_internal_call_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_internal_call_ops=%llu\n", (unsigned long long)got);
    return 0;
}
