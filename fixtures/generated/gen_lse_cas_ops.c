#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_lse_cas_ops(uint64_t *value, uint64_t expected,
                                                         uint64_t desired) {
    const int exchanged = __atomic_compare_exchange_n(value, &expected, desired, 0,
                                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return expected ^ (uint64_t)exchanged ^ __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

int main(void) {
    uint64_t value = 41;
    const uint64_t success = candidate_lse_cas_ops(&value, 41, 99);
    const uint64_t expected_success = 41u ^ 1u ^ 99u;
    const uint64_t failure = candidate_lse_cas_ops(&value, 17, 123);
    const uint64_t expected_failure = 99u ^ 0u ^ 99u;
    if (success != expected_success || failure != expected_failure || value != 99) {
        fprintf(stderr, "candidate_lse_cas_ops mismatch\n");
        return 1;
    }
    printf("candidate_lse_cas_ops=%llu/%llu\n",
           (unsigned long long)success, (unsigned long long)failure);
    return 0;
}
