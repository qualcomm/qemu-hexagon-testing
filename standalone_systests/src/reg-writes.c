/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define SETVAL 0x12345678

void failed(void)
{
    puts("FAIL");
    exit(1);
    __builtin_trap();
}

void passed(void)
{
    puts("PASS");
    exit(0);
    __builtin_trap();
}

/* naked to avoid having codegen alter the regs we want to test */
void __attribute__((naked, noreturn)) finalize(void)
{
    asm volatile(
        "r0 = p3:0\n"
        "p0 = cmp.eq(r0, #%0)\n"
        "if (!p0) call #failed\n"

        "r0 = r10\n"
        "p0 = cmp.eq(r0, #%0)\n"
        "if (!p0) call #failed\n"

        "r0 = g0\n"
        "p0 = cmp.eq(r0, #%0)\n"
        "if (!p0) call #failed\n"

        "r0 = imask\n"
        "p0 = cmp.eq(r0, #%0)\n"
        "if (!p0) call #failed\n"

        "call #passed\n"
        ".word 0x6fffdffc\n" /* invalid packet to cause an abort */
        :
        : "i"(SETVAL)
        : "r0", "p0"
    );
}

int main()
{
    failed(); /* should never reach here as lldb will change PC */
    return 0;
}
