#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static uint64_t tail_helper(uint64_t value) {
    return (value * 9u) ^ 0xabcdefu;
}

__attribute__((noinline)) uint64_t candidate_tail_call_ops(uint64_t value) {
    return tail_helper(value + 3u);
}

int main(void) {
    const uint64_t got = candidate_tail_call_ops(0x1234u);
    const uint64_t expected = ((0x1234u + 3u) * 9u) ^ 0xabcdefu;
    if (got != expected) {
        fprintf(stderr, "candidate_tail_call_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_tail_call_ops=%llu\n", (unsigned long long)got);
    return 0;
}
