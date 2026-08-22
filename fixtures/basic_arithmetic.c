#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) int64_t protected_candidate(int64_t x) {
    int64_t y = x + 7;
    y -= 2;
    y ^= 0x35;
    y += (x << 1);
    return y;
}

int main(void) {
    const int64_t result = protected_candidate(5);
    const int64_t expected = ((5 + 7 - 2) ^ 0x35) + (5 << 1);
    if (result != expected) {
        fprintf(stderr, "unexpected result: %lld != %lld\n",
                (long long)result,
                (long long)expected);
        return 1;
    }
    printf("fixture_result=%lld\n", (long long)result);
    return 0;
}

