/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb10_inactive_badva";
extern char inactive_resume[], inactive_mirror[];
int main(void)
{
    enable_mmu();
    resume_pc = ADDRESS(inactive_resume);
    asm volatile("r0 = ##0xbad10000; badva1 = r0\n"
                 "r0 = ##0x9b900008; r2 = memw(r0)\n"
                 ".global inactive_resume\ninactive_resume:"
                 : : : "r0", "r2", "memory");
    check(faults[0].va1, 0xbad10000, "inactive BADVA1 preserved");
    check((faults[0].ssr >> 20) & 7, 1, "only slot 0 valid");
    resume_pc = ADDRESS(inactive_mirror);
    asm volatile("r0 = ##0xbad00000; badva0 = r0\n"
                 "r0 = ##0x9b900008; r1 = ##0x9b901010\n"
                 "p0 = cmp.eq(r0,r1)\n"
                 "{ r2 = memw(r0); if (p0) memw(r1) = r0 }:mem_noshuf\n"
                 ".global inactive_mirror\ninactive_mirror:"
                 : : : "r0", "r1", "r2", "p0", "memory");
    check(fault_count, 2, "inactive address fault count");
    check(faults[1].va0, 0xbad00000, "inactive BADVA0 preserved");
    check((faults[1].ssr >> 20) & 7, 6, "only slot 1 valid");
    return 0;
}
