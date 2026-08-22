// Freestanding compiler fixture: keep this source independent of a target sysroot.
__attribute__((noinline)) long rv64_integer(long lhs, long rhs, unsigned char *bytes) {
    const unsigned long product = (unsigned long)lhs * (unsigned long)rhs;
    const unsigned long quotient = rhs == 0 ? ~0ul : (unsigned long)lhs / (unsigned long)rhs;
    bytes[3] = (unsigned char)(product ^ quotient);
    return (long)((product >> 7) + quotient + bytes[3]);
}
