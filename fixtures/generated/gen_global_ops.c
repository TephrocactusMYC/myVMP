#include <stdint.h>
#include <stdio.h>

volatile uint64_t myvmp_global_value = 0x12345678u;

__attribute__((noinline)) uint64_t candidate_global_ops(uint64_t value) {
    myvmp_global_value ^= value;
    return myvmp_global_value + 17u;
}

int main(void) {
    const uint64_t expected = (0x12345678u ^ 0x55aau) + 17u;
    const uint64_t got = candidate_global_ops(0x55aau);
    if (got != expected) {
        fprintf(stderr, "candidate_global_ops mismatch: %llu != %llu\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_global_ops=%llu\n", (unsigned long long)got);
    return 0;
}
