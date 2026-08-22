#!/usr/bin/env python3
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get("MYVMP_FIXTURE_OUT", ROOT / "fixtures" / "generated"))


HEADER = """\
#include <stdint.h>
#include <stdio.h>

"""


def write(name: str, body: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    path.write_text(HEADER + body, encoding="ascii")


def integer_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_integer_ops(uint64_t a, uint64_t b) {
    uint64_t x = a + 0x1234u;
    x = x - (b & 0xffu);
    x = x ^ (b << 3);
    x = x | 0x55aa55aa55aa55aaULL;
    x = x & 0x7fffffffffffffffULL;
    x = x + (a >> 2);
    x = x ^ (x << 7);
    return x;
}

static uint64_t ref_integer_ops(uint64_t a, uint64_t b) {
    uint64_t x = a + 0x1234u;
    x = x - (b & 0xffu);
    x = x ^ (b << 3);
    x = x | 0x55aa55aa55aa55aaULL;
    x = x & 0x7fffffffffffffffULL;
    x = x + (a >> 2);
    x = x ^ (x << 7);
    return x;
}

int main(void) {
    const uint64_t got = candidate_integer_ops(0x123456789ULL, 0xdeadbeefULL);
    const uint64_t expected = ref_integer_ops(0x123456789ULL, 0xdeadbeefULL);
    if (got != expected) {
        fprintf(stderr, "candidate_integer_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_integer_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def branch_ops() -> str:
    return """\
__attribute__((noinline)) int64_t candidate_branch_ops(int64_t x) {
    int64_t acc = 0;
    for (int i = 0; i < 17; ++i) {
        if (((x + i) & 3) == 0) {
            acc += x ^ i;
        } else if ((i & 1) != 0) {
            acc -= x - i;
        } else {
            acc += (x << 1) + i;
        }
    }
    return acc;
}

static int64_t ref_branch_ops(int64_t x) {
    int64_t acc = 0;
    for (int i = 0; i < 17; ++i) {
        if (((x + i) & 3) == 0) {
            acc += x ^ i;
        } else if ((i & 1) != 0) {
            acc -= x - i;
        } else {
            acc += (x << 1) + i;
        }
    }
    return acc;
}

int main(void) {
    const int64_t got = candidate_branch_ops(23);
    const int64_t expected = ref_branch_ops(23);
    if (got != expected) {
        fprintf(stderr, "candidate_branch_ops mismatch: %lld != %lld\\n",
                (long long)got,
                (long long)expected);
        return 1;
    }
    printf("candidate_branch_ops=%lld\\n", (long long)got);
    return 0;
}
"""


def memory_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_memory_ops(uint64_t seed) {
    uint64_t values[4];
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] = seed + i * 13u;
    }

    uint64_t acc = 0;
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] ^= (i << 4);
        acc += values[i];
    }
    return acc;
}

static uint64_t ref_memory_ops(uint64_t seed) {
    uint64_t values[4];
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] = seed + i * 13u;
    }

    uint64_t acc = 0;
    for (uint64_t i = 0; i < 4; ++i) {
        values[i] ^= (i << 4);
        acc += values[i];
    }
    return acc;
}

int main(void) {
    const uint64_t got = candidate_memory_ops(0x3456u);
    const uint64_t expected = ref_memory_ops(0x3456u);
    if (got != expected) {
        fprintf(stderr, "candidate_memory_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_memory_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def muldiv_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_muldiv_ops(uint64_t a, uint64_t b) {
    uint64_t x = (a * 17u) + (b / 3u);
    x ^= (a >> 5);
    x += (b << 2);
    x -= (a / 7u);
    x += a / (b | 1u);
    x ^= a << (b & 63u);
    x += (uint64_t)((int64_t)a / (int64_t)(b | 1u));
    return x;
}

static uint64_t ref_muldiv_ops(uint64_t a, uint64_t b) {
    uint64_t x = (a * 17u) + (b / 3u);
    x ^= (a >> 5);
    x += (b << 2);
    x -= (a / 7u);
    x += a / (b | 1u);
    x ^= a << (b & 63u);
    x += (uint64_t)((int64_t)a / (int64_t)(b | 1u));
    return x;
}

int main(void) {
    const uint64_t got = candidate_muldiv_ops(0x123456u, 0xabcdefu);
    const uint64_t expected = ref_muldiv_ops(0x123456u, 0xabcdefu);
    if (got != expected) {
        fprintf(stderr, "candidate_muldiv_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_muldiv_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def pair_memory_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_pair_memory_ops(uint64_t a, uint64_t b) {
    volatile uint64_t values[2];
    values[0] = a + 11u;
    values[1] = b + 17u;
    uint64_t first = values[0];
    uint64_t second = values[1];
    return first ^ second;
}

static uint64_t ref_pair_memory_ops(uint64_t a, uint64_t b) {
    uint64_t values[2];
    values[0] = a + 11u;
    values[1] = b + 17u;
    return values[0] ^ values[1];
}

int main(void) {
    const uint64_t got = candidate_pair_memory_ops(0x3456u, 0x789au);
    const uint64_t expected = ref_pair_memory_ops(0x3456u, 0x789au);
    if (got != expected) {
        fprintf(stderr, "candidate_pair_memory_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_pair_memory_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def narrow_memory_ops() -> str:
    return """\
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
        fprintf(stderr, "candidate_narrow_memory_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_narrow_memory_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def signed_memory_ops() -> str:
    return """\
__attribute__((noinline)) int64_t candidate_signed_memory_ops(int64_t seed) {
    volatile int8_t bytes[2];
    volatile int16_t halves[2];
    volatile int32_t words[2];
    bytes[0] = (int8_t)seed;
    bytes[1] = (int8_t)(seed >> 8);
    halves[0] = (int16_t)(seed >> 16);
    halves[1] = (int16_t)(seed >> 24);
    words[0] = (int32_t)(seed >> 7);
    words[1] = (int32_t)(seed >> 13);
    return (int64_t)bytes[0] - (int64_t)bytes[1] +
           (int64_t)halves[0] - (int64_t)halves[1] +
           (int64_t)words[0] - (int64_t)words[1];
}

static int64_t ref_signed_memory_ops(int64_t seed) {
    int8_t bytes[2];
    int16_t halves[2];
    int32_t words[2];
    bytes[0] = (int8_t)seed;
    bytes[1] = (int8_t)(seed >> 8);
    halves[0] = (int16_t)(seed >> 16);
    halves[1] = (int16_t)(seed >> 24);
    words[0] = (int32_t)(seed >> 7);
    words[1] = (int32_t)(seed >> 13);
    return (int64_t)bytes[0] - (int64_t)bytes[1] +
           (int64_t)halves[0] - (int64_t)halves[1] +
           (int64_t)words[0] - (int64_t)words[1];
}

int main(void) {
    const int64_t got = candidate_signed_memory_ops((int64_t)0xfedcba9876543210ULL);
    const int64_t expected = ref_signed_memory_ops((int64_t)0xfedcba9876543210ULL);
    if (got != expected) {
        fprintf(stderr, "candidate_signed_memory_ops mismatch: %lld != %lld\\n",
                (long long)got,
                (long long)expected);
        return 1;
    }
    printf("candidate_signed_memory_ops=%lld\\n", (long long)got);
    return 0;
}
"""


def branch_zero_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_branch_zero_ops(uint64_t x) {
    uint64_t acc = 0;
    while (x != 0) {
        acc += x & 7u;
        x >>= 1;
    }
    if (acc == 0) {
        return 99;
    }
    return acc;
}

static uint64_t ref_branch_zero_ops(uint64_t x) {
    uint64_t acc = 0;
    while (x != 0) {
        acc += x & 7u;
        x >>= 1;
    }
    if (acc == 0) {
        return 99;
    }
    return acc;
}

int main(void) {
    const uint64_t got = candidate_branch_zero_ops(0x1234u);
    const uint64_t expected = ref_branch_zero_ops(0x1234u);
    if (got != expected) {
        fprintf(stderr, "candidate_branch_zero_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_branch_zero_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def bitlogic_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_bitlogic_ops(uint64_t a, uint64_t b) {
    uint64_t x = ~a;
    uint64_t y = (a & ~b) ^ (b | ~a);
    uint64_t z = (int64_t)y >> 3;
    uint64_t rot = (a >> 13) | (a << 51);
    uint64_t signed_shift = (uint64_t)((int64_t)b >> 7);
    return (x ^ y) + z + rot + signed_shift;
}

static uint64_t ref_bitlogic_ops(uint64_t a, uint64_t b) {
    uint64_t x = ~a;
    uint64_t y = (a & ~b) ^ (b | ~a);
    uint64_t z = (uint64_t)((int64_t)y >> 3);
    uint64_t rot = (a >> 13) | (a << 51);
    uint64_t signed_shift = (uint64_t)((int64_t)b >> 7);
    return (x ^ y) + z + rot + signed_shift;
}

int main(void) {
    const uint64_t got = candidate_bitlogic_ops(0x1234567890ULL, 0xabcdef0123ULL);
    const uint64_t expected = ref_bitlogic_ops(0x1234567890ULL, 0xabcdef0123ULL);
    if (got != expected) {
        fprintf(stderr, "candidate_bitlogic_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got,
                (unsigned long long)expected);
        return 1;
    }
    printf("candidate_bitlogic_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def condition_ops() -> str:
    return """\
__attribute__((noinline)) int64_t candidate_condition_ops(int64_t a, int64_t b) {
    int64_t acc = 0;
    int64_t sentinel = a + 1;
    if (sentinel == 0) {
        acc += 11;
    }
    if (a < b) {
        acc += 3;
    }
    if (a >= 0) {
        acc += 5;
    }
    if (b > 100) {
        acc += 7;
    }
    return acc;
}

static int64_t ref_condition_ops(int64_t a, int64_t b) {
    int64_t acc = 0;
    int64_t sentinel = a + 1;
    if (sentinel == 0) {
        acc += 11;
    }
    if (a < b) {
        acc += 3;
    }
    if (a >= 0) {
        acc += 5;
    }
    if (b > 100) {
        acc += 7;
    }
    return acc;
}

int main(void) {
    const int64_t got = candidate_condition_ops(42, 123);
    const int64_t expected = ref_condition_ops(42, 123);
    if (got != expected) {
        fprintf(stderr, "candidate_condition_ops mismatch: %lld != %lld\\n",
                (long long)got,
                (long long)expected);
        return 1;
    }
    printf("candidate_condition_ops=%lld\\n", (long long)got);
    return 0;
}
"""


def runtime_extended_ops() -> str:
    return """\
static uint64_t ref_runtime_extended_ops(uint64_t a, uint64_t b) {
    const uint64_t x2 = a ^ 0xffu;
    const uint64_t x3 = x2 & (b << 1);
    const uint64_t x4 = x3 ^ ~(b >> 2);
    const uint64_t x5 = a * b + x4;
    const uint64_t x6 = x5 - a * b;
    const uint64_t x7 = (uint64_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
    const uint64_t x8 = (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;
    const uint64_t x9 = (uint32_t)a >> 3;
    const uint64_t x10 = a > b ? x5 : x6 + 1;
    const uint64_t x11 = a <= b ? x7 : ~x8;
    const uint64_t x12 = a == b ? x3 : 0 - x4;
    return x10 + x11 + x12 + x9;
}

__attribute__((noinline)) uint64_t candidate_runtime_extended_ops(uint64_t a, uint64_t b) {
#if defined(__aarch64__)
    uint64_t result;
    __asm__ volatile(
        "eor x2, %x[a], #0xff\\n"
        "and x3, x2, %x[b], lsl #1\\n"
        "eon x4, x3, %x[b], lsr #2\\n"
        "madd x5, %x[a], %x[b], x4\\n"
        "msub x6, %x[a], %x[b], x5\\n"
        "smull x7, %w[a], %w[b]\\n"
        "umull x8, %w[a], %w[b]\\n"
        "lsr w9, %w[a], #3\\n"
        "cmp %x[a], %x[b]\\n"
        "csinc x10, x5, x6, hi\\n"
        "csinv x11, x7, x8, ls\\n"
        "csneg x12, x3, x4, eq\\n"
        "add x10, x10, x11\\n"
        "add x10, x10, x12\\n"
        "add %x[out], x10, x9\\n"
        : [out] "=&r" (result)
        : [a] "r" (a), [b] "r" (b)
        : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "cc");
    return result;
#else
    return ref_runtime_extended_ops(a, b);
#endif
}

int main(void) {
    const uint64_t a = 0x81234567u;
    const uint64_t b = 0x12345u;
    const uint64_t got = candidate_runtime_extended_ops(a, b);
    const uint64_t expected = ref_runtime_extended_ops(a, b);
    if (got != expected) {
        fprintf(stderr, "candidate_runtime_extended_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_runtime_extended_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def call_ops() -> str:
    return """\
__attribute__((noinline)) static uint64_t call_helper(uint64_t a, uint64_t b) {
    return (a ^ 0x55aa55aaULL) + b * 3u;
}

__attribute__((noinline)) uint64_t candidate_call_ops(uint64_t a, uint64_t b) {
    const uint64_t called = call_helper(a + 11u, b ^ 7u);
    return (called << 1) + a;
}

int main(void) {
    const uint64_t got = candidate_call_ops(0x12345u, 0x6789u);
    const uint64_t expected = ((0x12345u + 11u) ^ 0x55aa55aaULL) + (0x6789u ^ 7u) * 3u;
    const uint64_t final_expected = (expected << 1) + 0x12345u;
    if (got != final_expected) {
        fprintf(stderr, "candidate_call_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)final_expected);
        return 1;
    }
    printf("candidate_call_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def indirect_call_ops() -> str:
    return """\
typedef uint64_t (*binary_fn)(uint64_t, uint64_t);

__attribute__((noinline)) static uint64_t indirect_helper(uint64_t a, uint64_t b) {
    return (a * 5u) ^ (b + 0x1234u);
}

__attribute__((noinline)) uint64_t candidate_indirect_call_ops(binary_fn fn,
                                                                uint64_t a,
                                                                uint64_t b) {
#if defined(__aarch64__)
    uint64_t result;
    __asm__ volatile(
        "mov x0, %x[a]\\n"
        "mov x1, %x[b]\\n"
        "blr %x[fn]\\n"
        "add %x[out], x0, #9\\n"
        : [out] "=&r" (result)
        : [fn] "r" (fn), [a] "r" (a), [b] "r" (b)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
          "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30", "cc", "memory");
    return result;
#else
    return fn(a, b) + 9u;
#endif
}

int main(void) {
    const uint64_t got = candidate_indirect_call_ops(indirect_helper, 0x4567u, 0x89abu);
    const uint64_t expected = indirect_helper(0x4567u, 0x89abu) + 9u;
    if (got != expected) {
        fprintf(stderr, "candidate_indirect_call_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_indirect_call_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def global_ops() -> str:
    return """\
volatile uint64_t myvmp_global_value = 0x12345678u;

__attribute__((noinline)) uint64_t candidate_global_ops(uint64_t value) {
    myvmp_global_value ^= value;
    return myvmp_global_value + 17u;
}

int main(void) {
    const uint64_t expected = (0x12345678u ^ 0x55aau) + 17u;
    const uint64_t got = candidate_global_ops(0x55aau);
    if (got != expected) {
        fprintf(stderr, "candidate_global_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_global_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def tail_call_ops() -> str:
    return """\
__attribute__((noinline)) static uint64_t tail_helper(uint64_t value) {
    return (value * 9u) ^ 0xabcdefu;
}

__attribute__((noinline)) uint64_t candidate_tail_call_ops(uint64_t value) {
    return tail_helper(value + 3u);
}

int main(void) {
    const uint64_t got = candidate_tail_call_ops(0x1234u);
    const uint64_t expected = ((0x1234u + 3u) * 9u) ^ 0xabcdefu;
    if (got != expected) {
        fprintf(stderr, "candidate_tail_call_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_tail_call_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def indirect_tail_call_ops() -> str:
    return """\
typedef uint64_t (*tail_binary_fn)(uint64_t, uint64_t);

__attribute__((noinline)) static uint64_t indirect_tail_helper(uint64_t a, uint64_t b) {
    return (a + 0x7788u) ^ (b * 11u);
}

__attribute__((noinline)) uint64_t candidate_indirect_tail_call_ops(tail_binary_fn fn,
                                                                     uint64_t a,
                                                                     uint64_t b) {
    return fn(a, b);
}

int main(void) {
    const uint64_t got = candidate_indirect_tail_call_ops(indirect_tail_helper, 0x123u, 0x456u);
    const uint64_t expected = indirect_tail_helper(0x123u, 0x456u);
    if (got != expected) {
        fprintf(stderr, "candidate_indirect_tail_call_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_indirect_tail_call_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def internal_call_ops() -> str:
    return """\
#if defined(__aarch64__)
__asm__(
    ".text\\n"
    ".global candidate_internal_call_ops\\n"
    ".type candidate_internal_call_ops, %function\\n"
    "candidate_internal_call_ops:\\n"
    "mov x1, x0\\n"
    "mov x2, x30\\n"
    "bl 1f\\n"
    "add x0, x0, x1\\n"
    "mov x30, x2\\n"
    "ret\\n"
    "1: add x0, x0, #7\\n"
    "ret\\n"
    ".size candidate_internal_call_ops, .-candidate_internal_call_ops\\n");
uint64_t candidate_internal_call_ops(uint64_t value);
#else
__attribute__((noinline)) uint64_t candidate_internal_call_ops(uint64_t value) {
    return (value + 7u) + value;
}
#endif

int main(void) {
    const uint64_t got = candidate_internal_call_ops(0x3456u);
    const uint64_t expected = (0x3456u + 7u) + 0x3456u;
    if (got != expected) {
        fprintf(stderr, "candidate_internal_call_ops mismatch: %llu != %llu\\n",
                (unsigned long long)got, (unsigned long long)expected);
        return 1;
    }
    printf("candidate_internal_call_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def atomic_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_atomic_ops(uint64_t *value) {
    const uint64_t before = __atomic_load_n(value, __ATOMIC_ACQUIRE);
    const uint64_t observed = __atomic_fetch_add(value, 7u, __ATOMIC_ACQ_REL);
    __atomic_store_n(value, observed ^ 0x55u, __ATOMIC_RELEASE);
    return before ^ observed ^ __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

int main(void) {
    uint64_t value = 41;
    const uint64_t got = candidate_atomic_ops(&value);
    const uint64_t expected = 41u ^ 41u ^ (41u ^ 0x55u);
    if (got != expected || value != (41u ^ 0x55u)) {
        fprintf(stderr, "candidate_atomic_ops mismatch: %llu/%llu != %llu/%llu\\n",
                (unsigned long long)got, (unsigned long long)value,
                (unsigned long long)expected, (unsigned long long)(41u ^ 0x55u));
        return 1;
    }
    printf("candidate_atomic_ops=%llu\\n", (unsigned long long)got);
    return 0;
}
"""


def lse_cas_ops() -> str:
    return """\
__attribute__((noinline)) uint64_t candidate_lse_cas_ops(uint64_t *value, uint64_t expected,
                                                         uint64_t desired) {
    const int exchanged = __atomic_compare_exchange_n(value, &expected, desired, 0,
                                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return expected ^ (uint64_t)exchanged ^ __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

int main(void) {
    uint64_t value = 41;
    const uint64_t success = candidate_lse_cas_ops(&value, 41, 99);
    const uint64_t expected_success = 41u ^ 1u ^ 99u;
    const uint64_t failure = candidate_lse_cas_ops(&value, 17, 123);
    const uint64_t expected_failure = 99u ^ 0u ^ 99u;
    if (success != expected_success || failure != expected_failure || value != 99) {
        fprintf(stderr, "candidate_lse_cas_ops mismatch\\n");
        return 1;
    }
    printf("candidate_lse_cas_ops=%llu/%llu\\n",
           (unsigned long long)success, (unsigned long long)failure);
    return 0;
}
"""


def main() -> None:
    write("gen_integer_ops.c", integer_ops())
    write("gen_branch_ops.c", branch_ops())
    write("gen_memory_ops.c", memory_ops())
    write("gen_muldiv_ops.c", muldiv_ops())
    write("gen_pair_memory_ops.c", pair_memory_ops())
    write("gen_narrow_memory_ops.c", narrow_memory_ops())
    write("gen_signed_memory_ops.c", signed_memory_ops())
    write("gen_branch_zero_ops.c", branch_zero_ops())
    write("gen_bitlogic_ops.c", bitlogic_ops())
    write("gen_condition_ops.c", condition_ops())
    write("gen_runtime_extended_ops.c", runtime_extended_ops())
    write("gen_call_ops.c", call_ops())
    write("gen_indirect_call_ops.c", indirect_call_ops())
    write("gen_global_ops.c", global_ops())
    write("gen_tail_call_ops.c", tail_call_ops())
    write("gen_indirect_tail_call_ops.c", indirect_tail_call_ops())
    write("gen_internal_call_ops.c", internal_call_ops())
    write("gen_atomic_ops.c", atomic_ops())
    write("gen_lse_cas_ops.c", lse_cas_ops())
    print(f"generated fixtures in {OUT}")


if __name__ == "__main__":
    main()
