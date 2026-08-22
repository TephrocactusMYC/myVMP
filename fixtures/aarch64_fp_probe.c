#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) double candidate_fp_probe(double a, double b) {
    double value = (a * 3.5) + (b / 2.0);
    if (value > 17.0) {
        value -= 4.25;
    }
    return value;
}

__attribute__((noinline)) float candidate_fp32_probe(float a, float b) {
    float value = (a * 3.5f) + (b / 2.0f);
    if (value > 17.0f) {
        value -= 4.25f;
    }
    return value;
}

__attribute__((noinline)) double candidate_fp_extended(float f, double d) {
    const float magnitude = __builtin_fabsf(f);
    const float root = __builtin_sqrtf(magnitude);
    const double negated = -d;
    const double widened = (double)root;
    const float narrowed = (float)negated;
    return widened + __builtin_fabs(negated) + (double)narrowed;
}

__attribute__((noinline)) int64_t candidate_fp_convert_minmax(int64_t s, uint32_t u,
                                                               float f, double d) {
    const float sf = (float)s;
    const double ud = (double)u;
    const int32_t from_f = (int32_t)f;
    const uint64_t from_d = (uint64_t)d;
    const double lo = __builtin_fmin((double)sf, ud);
    const double hi = __builtin_fmax(lo, d);
    return from_f + (int64_t)from_d + (int64_t)hi;
}

int main(void) {
    volatile double result = candidate_fp_probe(8.0, 5.0);
    volatile float result32 = candidate_fp32_probe(8.0f, 5.0f);
    volatile double extended = candidate_fp_extended(-9.0f, 2.5);
    volatile int64_t converted = candidate_fp_convert_minmax(-12, 7, 5.75f, 9.25);
    printf("candidate_fp_probe=%.2f\n", result);
    printf("candidate_fp32_probe=%.2f\n", (double)result32);
    printf("candidate_fp_extended=%.2f\n", extended);
    printf("candidate_fp_convert_minmax=%lld\n", (long long)converted);
    return result == 26.25 && result32 == 26.25f && extended == 3.0 && converted == 23 ? 0 : 1;
}
