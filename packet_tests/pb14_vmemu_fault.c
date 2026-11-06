/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb14_vmemu_fault";
extern char vector_resume[], vector_second[], vector_store[];
static uint8_t upper[4096] __attribute__((aligned(4096)));
int main(void)
{
    enable_mmu();
    resume_pc = ADDRESS(vector_resume);
    unsigned marker;
    asm volatile("r0 = ssr; r0 = setbit(r0,#31); ssr = r0\n"
                 "r0 = ##0x9b900f81; r4 = #17\n"
                 "{ v1 = vmemu(r0); r4 = add(r4,#1) }\n"
                 ".global vector_resume\nvector_resume: %0 = r4"
                 : "=r"(marker) : : "r0", "r4", "memory");
    check(marker, 17, "vector fault rolls back scalar result");
    check(faults[0].cause, 0x70, "VMEMU upper load miss");
    check((faults[0].ssr >> 20) & 7, 7, "VMEMU upper slot wins");
    check(faults[0].va0, 0x9b900f81, "VMEMU lower BADVA");
    check(faults[0].va1, 0x9b901001, "VMEMU upper BADVA");
    check(faults[0].badva, 0x9b901001, "VMEMU upper BADVA alias");
    map_page(1, 0x9b901000, ADDRESS(upper));
    resume_pc = ADDRESS(vector_second);
    asm volatile("r0 = ##0x9b900f81; v1 = vmemu(r0)\n"
                 ".global vector_second\nvector_second:"
                 : : : "r0", "memory");
    check((faults[1].ssr >> 20) & 7, 3, "VMEMU lower miss after upper mapped");
    check(faults[1].va0, 0x9b900f81, "VMEMU lower address unchanged");
    check(faults[1].va1, 0x9b901001, "VMEMU upper address unchanged");
    resume_pc = ADDRESS(vector_store);
    asm volatile("r0 = ##0x9b900f82; vmemu(r0) = v2\n"
                 ".global vector_store\nvector_store:"
                 : : : "r0", "memory");
    check(fault_count, 3, "VMEMU fault case count");
    check(faults[2].cause, 0x71, "VMEMU store miss");
    check((faults[2].ssr >> 20) & 7, 3, "VMEMU store both addresses valid");
    check(faults[2].va0, 0x9b900f82, "VMEMU store lower BADVA");
    check(faults[2].va1, 0x9b901002, "VMEMU store upper BADVA");
    return 0;
}
