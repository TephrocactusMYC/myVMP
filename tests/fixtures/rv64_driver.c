#include <stdio.h>

long rv64_integer(long lhs, long rhs, unsigned char *bytes);

int main(void) {
    const long inputs[][2] = {{7, 3}, {-17, 5}, {123456, 0}, {0x12345, 19}};
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        unsigned char bytes[8] = {0};
        const long value = rv64_integer(inputs[i][0], inputs[i][1], bytes);
        printf("%ld:%u\n", value, bytes[3]);
    }
    return 0;
}
