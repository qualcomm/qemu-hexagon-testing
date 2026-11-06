/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb13_store_rollback";
extern char rollback_resume[];
static uint32_t data[8] __attribute__((aligned(8)));
int main(void)
{
    for (unsigned i = 0; i < 8; i++) {
        data[i] = 0xffe2c5a8;
    }
    resume_pc = ADDRESS(rollback_resume);
    asm volatile("r0 = %0; r1 = add(r0,#9); r2 = ##0xc0de0011\n"
                 "{ memw(r0) = r2; memw(r1) = r2 }:mem_noshuf\n"
                 ".global rollback_resume\nrollback_resume:"
                 : : "r"(data) : "r0", "r1", "r2", "memory");
    check(fault_count, 1, "store rollback fault count");
    check(faults[0].cause, 0x21, "slot 0 store alignment fault");
    for (unsigned i = 0; i < 8; i++) {
        check(data[i], 0xffe2c5a8, "neither store changes memory");
    }
    return 0;
}
