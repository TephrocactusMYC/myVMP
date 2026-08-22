#include <stdio.h>

long rv64_loop(long value);
long rv64_global_access(long value);
long rv64_switch(long value);

int main(void) {
    printf("%ld %ld %ld\n", rv64_loop(11), rv64_global_access(9), rv64_switch(4));
    printf("%ld %ld\n", rv64_switch(99), rv64_global_access(-3));
    return 0;
}
