typedef unsigned int u32;

__attribute__((noinline, used))
u32 arm32_scalar(u32 a, u32 b, u32 *values) {
    u32 x = (a + 0x1234u) ^ (b << 3);
    x = (x | 0x55aau) & 0x7fffffffu;
    values[1] = x;
    x += values[0];
    if (x > b) x -= 7;
    return x * (b | 1u);
}
