#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) int64_t candidate_branch_ops(int64_t x) {
    int64_t acc = 0;
    for (int i = 0; i < 17; ++i) {
        if (((x + i) & 3) == 0) {
            acc += x ^ i;
        } else if ((i & 1) != 0) {
            acc -= x - i;
        } else {
            acc += (x << 1) + i;
        }
    }
    return acc;
}

static int64_t ref_branch_ops(int64_t x) {
    int64_t acc = 0;
    for (int i = 0; i < 17; ++i) {
        if (((x + i) & 3) == 0) {
            acc += x ^ i;
        } else if ((i & 1) != 0) {
            acc -= x - i;
        } else {
            acc += (x << 1) + i;
        }
    }
    return acc;
}

int main(void) {
    const int64_t got = candidate_branch_ops(23);
    const int64_t expected = ref_branch_ops(23);
    if (got != expected) {
        fprintf(stderr, "candidate_branch_ops mismatch: %lld != %lld\n",
                (long long)got,
                (long long)expected);
        return 1;
    }
    printf("candidate_branch_ops=%lld\n", (long long)got);
    return 0;
}
