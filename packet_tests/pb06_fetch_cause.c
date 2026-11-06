/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb06_fetch_cause";
static uint32_t code[2048] __attribute__((aligned(4096)));
extern uint32_t fetch_sample[];
extern char fetch_resume[];
asm(".text\n.global fetch_sample\nfetch_sample:\n"
    "{ r8 = #1; r9 = #2 }\n{ r8 = #3; r9 = #4 }\n");
int main(void)
{
    for (unsigned i = 0; i < 4; i++) {
        code[1022 + i] = fetch_sample[i];
    }
    map_page(1, 0x9ba00000, ADDRESS(code));
    enable_mmu();
    resume_pc = ADDRESS(fetch_resume);
    asm volatile("r0 = ##0x9ba00ff8; jumpr r0\n"
                 ".global fetch_resume\nfetch_resume:"
                 : : : "r0", "r8", "r9", "memory");
    check(fault_count, 1, "fetch exception count");
    check(faults[0].cause, 0x60, "packet starts on missing page");
    check(faults[0].pc, 0x9ba01000, "fetch restart PC");
    return 0;
}
