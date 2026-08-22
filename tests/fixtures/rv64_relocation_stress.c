#include <stdint.h>

__thread uint64_t rv64_tls_counter = 0x123456789abcdef0ULL;
uint64_t rv64_external_state = 0x1020304050607080ULL;

__attribute__((noinline)) uint64_t rv64_relocation_target(uint64_t value) {
    uint64_t mixed = value ^ rv64_external_state;
    for (unsigned index = 0; index != 7; ++index)
        mixed = (mixed << 9) ^ (mixed >> 3) ^ (0x9e3779b97f4a7c15ULL + index);
    return mixed;
}

uint64_t rv64_relocation_driver(uint64_t value) {
    rv64_tls_counter += value;
    return rv64_relocation_target(value) ^ rv64_tls_counter;
}
