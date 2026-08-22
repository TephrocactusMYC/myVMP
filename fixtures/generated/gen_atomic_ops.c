#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_atomic_ops(uint64_t *value) {
    const uint64_t before = __atomic_load_n(value, __ATOMIC_ACQUIRE);
    const uint64_t observed = __atomic_fetch_add(value, 7u, __ATOMIC_ACQ_REL);
    __atomic_store_n(value, observed ^ 0x55u, __ATOMIC_RELEASE);
    return before ^ observed ^ __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

int main(void) {
    uint64_t value = 41;
    const uint64_t got = candidate_atomic_ops(&value);
    const uint64_t expected = 41u ^ 41u ^ (41u ^ 0x55u);
    if (got != expected || value != (41u ^ 0x55u)) {
        fprintf(stderr, "candidate_atomic_ops mismatch: %llu/%llu != %llu/%llu\n",
                (unsigned long long)got, (unsigned long long)value,
                (unsigned long long)expected, (unsigned long long)(41u ^ 0x55u));
        return 1;
    }
    printf("candidate_atomic_ops=%llu\n", (unsigned long long)got);
    return 0;
}
