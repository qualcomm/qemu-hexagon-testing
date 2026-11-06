/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb03_call_priority";
int main(void)
{
    unsigned lr;
    asm volatile("r12 = r31; r31 = ##0x12345678; r0 = #0\n"
                 "p0 = cmp.eq(r0,r0)\n"
                 "{ if (p0) jump 1f; call 2f }\n"
                 "2: r31 = #0\n"
                 "1: %0 = r31; r31 = r12"
                 : "=&r"(lr) : : "r0", "r12", "r31", "p0");
    check(lr, 0x12345678, "losing call preserves LR");
    return 0;
}
