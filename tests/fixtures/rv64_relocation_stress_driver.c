#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

uint64_t rv64_relocation_driver(uint64_t);

int main(void) {
    printf("%" PRIx64 "\n", rv64_relocation_driver(0x1122334455667788ULL));
    return 0;
}
