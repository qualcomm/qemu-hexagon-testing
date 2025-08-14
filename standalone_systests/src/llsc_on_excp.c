/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "invalid_opcode.h"

/* Using volatile because we are testing atomics */
volatile int mem;
static void test_interrupt_cleans_llsc(void)
{
    int res = 0;

    asm volatile("1: r1 = memw_locked(%1)\n"
                 "   p0 = cmp.eq(r1,#0)\n"
                 "   if (!p0) jump 1b\n"
                 /* invalid opcode should trigger an exception */
                 "   call invalid_opcode\n"
                 /*
                  * this should return false in p0 as the exception
                  * should clean the llsc state.
                  */
                 "   r1 = #1\n"
                 "   memw_locked(%1, p0) = r1\n"
                 "   %0 = p0\n"
                 : "=r"(res) : "r"(&mem) : "r1", "p0");

    if (res || mem) {
        err++;
    }
}

INVALID_OPCODE_MAIN("LLSC on exception test", test_interrupt_cleans_llsc, 1)
