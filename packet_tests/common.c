/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"

volatile struct fault faults[8];
volatile unsigned fault_count;
uint32_t resume_pc;
static unsigned failures;

static void output(const char *s)
{
    while (*s) {
        char c = *s++;
        asm volatile("r0 = #3; r1 = %0; trap0(#0)"
                     : : "r"(&c) : "r0", "r1", "memory");
    }
}

static void hex(unsigned n)
{
    char s[9];
    for (unsigned i = 0; i < 8; i++) {
        s[i] = "0123456789abcdef"[(n >> (28 - i * 4)) & 15];
    }
    s[8] = 0;
    output(s);
}

void check(unsigned actual, unsigned expected, const char *what)
{
    if (actual != expected) {
        failures++;
        output("FAIL ");
        output(what);
        output(" actual=");
        hex(actual);
        output(" expected=");
        hex(expected);
        output("\n");
    }
}

void finish(unsigned failed)
{
    failed |= failures;
    output(failed ? "FAIL " : "PASS ");
    output(test_name);
    output("\n");
    asm volatile("r0 = #0x18; r1 = %0; trap0(#0)"
                 : : "r"(!!failed) : "r0", "r1", "memory");
    for (;;) {
        asm volatile("stop(r0)");
    }
}

void record_fault(void)
{
    unsigned n = fault_count++;
    if (n >= 8 || !resume_pc) {
        finish(1);
    }
    struct fault f;
    asm volatile("%0 = ssr; %1 = elr; %2 = badva0; %3 = badva1; %4 = badva"
                 : "=r"(f.ssr), "=r"(f.pc), "=r"(f.va0), "=r"(f.va1),
                   "=r"(f.badva));
    f.cause = f.ssr & 255;
    faults[n] = f;
    asm volatile("elr = %0" : : "r"(resume_pc));
}

void map_page(unsigned index, uint32_t va, uint32_t pa)
{
    uint64_t entry = ((uint64_t)(0xc3f00000 | (va >> 12)) << 32) |
                     0xf7000001 | ((uint64_t)(pa >> 12) << 1);
    asm volatile("tlbw(%0, %1); isync" : : "r"(entry), "r"(index) : "memory");
}

void enable_mmu(void)
{
    /* Identity-map the low 16 MB, including this image and its stack. */
    uint64_t entry = UINT64_C(0xc3f00000f7000100);
    asm volatile("tlbw(%0, %1)" : : "r"(entry), "r"(0));
    asm volatile("r0 = syscfg; r0 = setbit(r0,#0); syscfg = r0; isync"
                 : : : "r0", "memory");
}
