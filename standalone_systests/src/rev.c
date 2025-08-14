/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>

int main()
{
    unsigned int rev;
    asm volatile(
            "r0 = rev\n"
            "r1 = #255\n"
            "%0 = and(r0, r1)\n"
            : "=r"(rev)
            :
            : "r0", "r1");
    printf("0x%x\n", rev);
    return 0;
}
