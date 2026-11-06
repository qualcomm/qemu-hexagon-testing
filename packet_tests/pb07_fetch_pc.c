/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb07_fetch_pc";
static uint32_t code[2048] __attribute__((aligned(4096)));
extern uint32_t straddle_sample[];
extern char straddle_resume[];
asm(".text\n.global straddle_sample\nstraddle_sample:\n"
    "{ r8 = #7; r9 = #8 }\n"
    "{ r8 = #1; r9 = #2; r10 = #3; r11 = #4 }\n");
int main(void)
{
    unsigned marker;
    for (unsigned i = 0; i < 6; i++) {
        code[1020 + i] = straddle_sample[i];
    }
    map_page(1, 0x9ba00000, ADDRESS(code));
    enable_mmu();
    resume_pc = ADDRESS(straddle_resume);
    asm volatile("r8 = #0; r0 = ##0x9ba00ff0; jumpr r0\n"
                 ".global straddle_resume\nstraddle_resume: %0 = r8"
                 : "=r"(marker) : : "r0", "r8", "r9", "r10", "r11", "memory");
    check(fault_count, 1, "straddling fetch exception count");
    check(faults[0].cause, 0x61, "straddling fetch cause");
    check(faults[0].pc, 0x9ba00ff8, "restart at straddling packet");
    check(marker, 7, "preceding packet retired");
    return 0;
}
