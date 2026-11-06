/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb04_hmx_address";
extern char hmx_resume[];
int main(void)
{
    unsigned marker;
    resume_pc = ADDRESS(hmx_resume);
    asm volatile("r4 = ssr; r4 = setbit(r4,#26); ssr = r4\n"
                 "r0 = cfgbase; r0 = asl(r0,#5); r0 = memw(r0+#56)\n"
                 "r0 = asl(r0,#16); r1 = #0; r2 = #0; r3 = #0; r4 = #17\n"
                 "{ activation.ub = mxmem(r0,r1); weight.b = mxmem(r2,r3);"
                 " r4 = add(r4,#1) }\n"
                 ".global hmx_resume\nhmx_resume: %0 = r4"
                 : "=r"(marker) : : "r0", "r1", "r2", "r3", "r4", "memory");
    check(fault_count, 1, "HMX exception count");
    check(faults[0].cause, 0x26, "HMX address error");
    check(marker, 17, "faulting HMX packet does not commit");
    return 0;
}
