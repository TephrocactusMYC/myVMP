#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) uint64_t candidate_narrow_memory_ops(uint64_t seed) {
    volatile uint8_t bytes[4];
    volatile uint16_t halves[2];
    bytes[0] = (uint8_t)seed;
    bytes[1] = (uint8_t)(seed >> 8);
    bytes[2] = (uint8_t)(seed >> 16);
    bytes[3] = (uint8_t)(seed >> 24);
    halves[0] = (uint16_t)((uint16_t)bytes[0] + (uint16_t)bytes[1]);
    halves[1] = (uint16_t)((uint16_t)bytes[2] + (uint16_t)bytes[3]);
    return (uint64_t)halves[0] + ((uint64_t)halves[1] << 16);
}

static uint64_t ref_narrow_memory_ops(uint64_t seed) {
    uint8_t bytes[4];
    uint16_t halves[2];
    bytes[0] = (uint8_t)seed;
    bytes[1] = (uint8_t)(seed >> 8);
    bytes[2] = (uint8_t)(seed >> 16);
    bytes[3] = (uint8_t)(seed >> 24);
    halves[0] = (uint16_t)(bytes[0] + bytes[1]);
    halves[1] = (uint16_t)(bytes[2] + bytes[3]);
    return (uint64_t)halves[0] + ((uint64_t)halves[1] << 16);
}

int main(void) {
    const uint64_t got = candidate_narrow_memory_ops(0x12345678u);
    const uint64_t expected = ref_narrow_memory_ops(0x12345678u);
    if (got != expected) {
        fprintf(stderr, "candidate_narrow_memory_ops mismatch: %llu != %llu\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_narrow_memory_ops=%llu\n", (unsigned long long)got);
    return 0;
}
