volatile long rv64_global = 41;

__attribute__((noinline)) long rv64_loop(long value) {
    long result = 3;
    for (long index = 0; index < value; ++index)
        result = (result * 33) ^ (index + value);
    return result;
}

__attribute__((noinline)) long rv64_global_access(long value) {
    rv64_global += value;
    return rv64_global ^ (value << 9);
}

__attribute__((noinline)) long rv64_switch(long value) {
    switch (value) {
    case 0: return 17;
    case 1: return 91;
    case 2: return -33;
    case 3: return value * 19;
    case 4: return value ^ 0x1234;
    default: return value + 700;
    }
}
