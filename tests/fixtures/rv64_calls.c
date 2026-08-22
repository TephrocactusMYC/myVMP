__attribute__((noinline)) long rv64_helper(long value) {
    return value * 9 + 7;
}

__attribute__((noinline)) long rv64_calls(long lhs, long rhs) {
    const long first = rv64_helper(lhs + 3);
    return first ^ rv64_helper(rhs - 2);
}
