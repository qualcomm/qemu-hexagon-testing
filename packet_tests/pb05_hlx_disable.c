/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb05_hlx_disable";
extern char hlx_resume[];
static uint16_t input[1024] __attribute__((aligned(2048)));
static uint16_t output[1024] __attribute__((aligned(2048)));
int main(void)
{
    for (unsigned i = 0; i < 1024; i++) {
        input[i] = i;
    }
    resume_pc = ADDRESS(hlx_resume);
    asm volatile("r0 = ccr; r0 = setbit(r0,#28); ccr = r0\n"
                 "r0 = %0; r26 = ccr; r26 = clrbit(r26,#28)\n"
                 "{ ccr = r26; L0.tmp = mem(r0); L1.h = add(L0.h,L0.h) }\n"
                 "{ L0.tmp = mem(r0); L1.h = add(L0.h,L0.h) }\n"
                 ".global hlx_resume\nhlx_resume:\n"
                 "r26 = setbit(r26,#28); ccr = r26\n"
                 "r1 = %1\n{ L2.h = add(L1.h,L1.h); mem(r1) = L2.new }"
                 : : "r"(input), "r"(output) : "r0", "r1", "r26", "memory");
    check(fault_count, 1, "HLX exception count");
    check(faults[0].cause, 0x19, "HLX disabled in next packet");
    for (unsigned i = 0; i < 1024; i++) {
        check(output[i], i * 4, "first packet HLX result committed");
    }
    return 0;
}
