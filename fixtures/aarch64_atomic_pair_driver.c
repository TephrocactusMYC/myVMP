#include <inttypes.h>
#include <stdio.h>

uint64_t candidate_atomic_pair(uint64_t *);

int main(void) {
    _Alignas(16) uint64_t values[2] = {0x1122334455667788ULL, 0x8877665544332211ULL};
    const uint64_t result = candidate_atomic_pair(values);
    printf("candidate_atomic_pair=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
           result, values[0], values[1]);
    return 0;
}
