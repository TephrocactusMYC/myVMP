#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

uint64_t candidate_aarch64_simd(const void *, void *, uint64_t);

int main(void) {
    _Alignas(16) uint32_t input[16] = {
        0x01020304u, 0x11223344u, 0x89abcdefu, 0x7fffffffu,
        0x10203040u, 0x55667788u, 0x76543210u, 0x80000001u,
        0x3f800000u, 0x40000000u, 0x40800000u, 0x41000000u,
        0x3f000000u, 0x40400000u, 0x3f800000u, 0x40000000u,
    };
    _Alignas(16) uint8_t output[256] = {0};
    const uint64_t result = candidate_aarch64_simd(input, output, 1);
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned index = 0; index < sizeof(output); ++index) {
        hash = (hash ^ output[index]) * UINT64_C(1099511628211);
    }
    printf("%" PRIu64 " %016" PRIx64 "\n", result, hash);
    return 0;
}
