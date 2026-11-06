/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb11_store_priority";
extern char priority_resume[], priority_second[];
static uint32_t data[1024] __attribute__((aligned(4096)));
int main(void)
{
    enable_mmu();
    resume_pc = ADDRESS(priority_resume);
    asm volatile("r0 = ##0x9b900008; r1 = ##0x9b901010\n"
                 "{ memw(r0) = r0; r3 = memw(r1) }:mem_noshuf\n"
                 ".global priority_resume\npriority_resume:"
                 : : : "r0", "r1", "r3", "memory");
    check(faults[0].cause, 0x71, "slot 1 store miss wins");
    check((faults[0].ssr >> 20) & 7, 7, "store slot wins with both valid");
    check(faults[0].va1, 0x9b900008, "winning store address");
    map_page(1, 0x9b900000, ADDRESS(data));
    resume_pc = ADDRESS(priority_second);
    asm volatile("r0 = ##0x9b900008; r1 = ##0x9b901010\n"
                 "{ memw(r0) = r0; r3 = memw(r1) }:mem_noshuf\n"
                 ".global priority_second\npriority_second:"
                 : : : "r0", "r1", "r3", "memory");
    check(fault_count, 2, "two priority fault cases");
    check(faults[1].cause, 0x70, "slot 0 load miss after mapping store");
    check(data[2], 0, "store not committed before load fault");
    return 0;
}
