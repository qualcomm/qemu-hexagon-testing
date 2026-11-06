/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb09_fault_validity";
extern char validity_resume[];
extern char mirror_resume[], predicate_resume[];
static uint32_t data[8];
int main(void)
{
    enable_mmu();
    resume_pc = ADDRESS(validity_resume);
    asm volatile("r0 = %0; r1 = ##0x9b901010\n"
                 "{ r2 = memw(r0); r3 = memw(r1) }:mem_noshuf\n"
                 ".global validity_resume\nvalidity_resume:"
                 : : "r"(data) : "r0", "r1", "r2", "r3", "memory");
    check(fault_count, 1, "dual load fault count");
    check(faults[0].cause, 0x70, "slot 0 miss");
    check((faults[0].ssr >> 20) & 7, 3, "both slot addresses valid");
    check(faults[0].va0, 0x9b901010, "slot 0 BADVA0");
    check(faults[0].va1, ADDRESS(data), "slot 1 BADVA1");
    resume_pc = ADDRESS(mirror_resume);
    asm volatile("r0 = ##0x9b900008; r1 = %0\n"
                 "{ r2 = memw(r0); r3 = memw(r1) }:mem_noshuf\n"
                 ".global mirror_resume\nmirror_resume:"
                 : : "r"(data) : "r0", "r1", "r2", "r3", "memory");
    check((faults[1].ssr >> 20) & 7, 7, "slot 1 fault with both valid");
    check(faults[1].va0, ADDRESS(data), "unexecuted slot 0 address");
    check(faults[1].va1, 0x9b900008, "slot 1 fault address");
    resume_pc = ADDRESS(predicate_resume);
    asm volatile("r0 = %0; r1 = ##0x9b901010\n"
                 "{ p0 = cmp.eq(r0,r1); if (p0.new) r2 = memw(r0++#4);"
                 " r3 = memw(r1) }:mem_noshuf\n"
                 ".global predicate_resume\npredicate_resume:"
                 : : "r"(data) : "r0", "r1", "r2", "r3", "p0", "memory");
    check(fault_count, 3, "all validity cases fault");
    check((faults[2].ssr >> 20) & 7, 1, "cancelled new-predicate load invalid");
    return 0;
}
