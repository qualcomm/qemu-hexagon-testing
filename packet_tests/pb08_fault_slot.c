/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb08_fault_slot";
extern char slot_resume[];
int main(void)
{
    enable_mmu();
    resume_pc = ADDRESS(slot_resume);
    asm volatile("r0 = ##0x9b900008; r1 = ##0x9b901010\n"
                 "p0 = cmp.eq(r0,r1)\n"
                 "{ r2 = memw(r0); if (p0) memw(r1) = r0 }:mem_noshuf\n"
                 ".global slot_resume\nslot_resume:"
                 : : : "r0", "r1", "r2", "p0", "memory");
    check(fault_count, 1, "slot 1 fault count");
    check(faults[0].cause, 0x70, "slot 1 load miss");
    check((faults[0].ssr >> 20) & 7, 6, "slot 1 BVS and validity");
    check(faults[0].va1, 0x9b900008, "slot 1 BADVA1");
    check(faults[0].badva, 0x9b900008, "slot 1 BADVA alias");
    return 0;
}
