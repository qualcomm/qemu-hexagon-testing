/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#ifndef PACKET_TEST_COMMON_H
#define PACKET_TEST_COMMON_H
#include <stdint.h>

struct fault {
    uint32_t cause, pc, ssr, va0, va1, badva;
};
extern volatile struct fault faults[8];
extern volatile unsigned fault_count;
extern uint32_t resume_pc;
extern const char test_name[];
void check(unsigned actual, unsigned expected, const char *what);
void finish(unsigned failed) __attribute__((noreturn));
void map_page(unsigned index, uint32_t va, uint32_t pa);
void enable_mmu(void);
#define ADDRESS(x) ((uint32_t)(uintptr_t)(x))
#endif
