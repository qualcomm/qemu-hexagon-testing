/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb12_alignment_priority";
extern char alignment_resume[], alignment_store[], alignment_cross[];
static uint32_t data[8];
int main(void)
{
    enable_mmu();
    resume_pc = ADDRESS(alignment_resume);
    asm volatile("r0 = %0; r1 = ##0x9b901011\n"
                 "{ memw(r0) = r0; r3 = memw(r1) }:mem_noshuf\n"
                 ".global alignment_resume\nalignment_resume:"
                 : : "r"(data) : "r0", "r1", "r3", "memory");
    check(faults[0].cause, 0x20, "misaligned missing load beats TLB miss");
    check(data[0], 0, "store rolled back on alignment fault");
    resume_pc = ADDRESS(alignment_store);
    asm volatile("r0 = ##0x9b900009; r1 = ##0x9b901010\n"
                 "{ memw(r0) = r0; r3 = memw(r1) }:mem_noshuf\n"
                 ".global alignment_store\nalignment_store:"
                 : : : "r0", "r1", "r3", "memory");
    check(faults[1].cause, 0x21, "misaligned store beats load miss");
    check((faults[1].ssr >> 20) & 7, 7, "alignment fault slot 1");
    resume_pc = ADDRESS(alignment_cross);
    asm volatile("r0 = ##0x9b900008; r1 = ##0x9b901011\n"
                 "{ r2 = memw(r0); r3 = memw(r1) }:mem_noshuf\n"
                 ".global alignment_cross\nalignment_cross:"
                 : : : "r0", "r1", "r2", "r3", "memory");
    check(fault_count, 3, "alignment case count");
    check(faults[2].cause, 0x20, "alignment precedes other slot TLB miss");
    check((faults[2].ssr >> 20) & 7, 3, "alignment fault slot 0");
    return 0;
}
