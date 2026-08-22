#include <stdio.h>
#include <stdint.h>
#include <string.h>

double candidate_fp_conditional(double, double, double, double);
double candidate_fp_conditional_nv(void);

int main(void) {
    printf("candidate_fp_conditional=%.2f\n",
           candidate_fp_conditional(1.0, 1.0, 7.5, 3.25));
    const double nv = candidate_fp_conditional_nv();
    uint64_t nv_bits;
    memcpy(&nv_bits, &nv, sizeof(nv_bits));
    printf("candidate_fp_conditional_nv=%016llx\n", (unsigned long long)nv_bits);
    return 0;
}
