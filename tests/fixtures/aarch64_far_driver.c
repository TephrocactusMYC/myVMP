#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

uint64_t aarch64_far_target(uint64_t);

int main(void) {
    printf("%" PRIu64 "\n", aarch64_far_target(123));
    return 0;
}
