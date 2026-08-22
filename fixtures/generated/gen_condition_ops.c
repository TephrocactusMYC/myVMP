#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) int64_t candidate_condition_ops(int64_t a, int64_t b) {
    int64_t acc = 0;
    int64_t sentinel = a + 1;
    if (sentinel == 0) {
        acc += 11;
    }
    if (a < b) {
        acc += 3;
    }
    if (a >= 0) {
        acc += 5;
    }
    if (b > 100) {
        acc += 7;
    }
    return acc;
}

static int64_t ref_condition_ops(int64_t a, int64_t b) {
    int64_t acc = 0;
    int64_t sentinel = a + 1;
    if (sentinel == 0) {
        acc += 11;
    }
    if (a < b) {
        acc += 3;
    }
    if (a >= 0) {
        acc += 5;
    }
    if (b > 100) {
        acc += 7;
    }
    return acc;
}

int main(void) {
    const int64_t got = candidate_condition_ops(42, 123);
    const int64_t expected = ref_condition_ops(42, 123);
    if (got != expected) {
        fprintf(stderr, "candidate_condition_ops mismatch: %lld != %lld\n",
                (long long)got,
                (long long)expected);
        return 1;
    }
    printf("candidate_condition_ops=%lld\n", (long long)got);
    return 0;
}
