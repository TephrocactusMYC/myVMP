#include <stdio.h>

long rv64_calls(long lhs, long rhs);

int main(void) {
    printf("%ld\n", rv64_calls(17, 91));
    printf("%ld\n", rv64_calls(-4, 8));
    return 0;
}
