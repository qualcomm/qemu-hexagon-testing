/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stdio.h>

static inline uint32_t read_ccr(void)
{
    uint32_t val;
    asm volatile("%[val] = ccr\n" : [val] "=r"(val));
    return val;
}

static inline void write_ccr(uint32_t val)
{
    asm volatile("ccr = %[val]\n" "isync\n" : : [val] "r"(val));
}

int main(void)
{
    uint32_t ccr;

    ccr = read_ccr();
    ccr |= (1u << 28);
    write_ccr(ccr);

    ccr = read_ccr();
    if (!(ccr & (1u << 28))) {
        puts("FAIL");
        return 1;
    }

    ccr &= ~(1u << 28);
    write_ccr(ccr);

    ccr = read_ccr();
    if (ccr & (1u << 28)) {
        puts("FAIL");
        return 1;
    }

    puts("PASS");
    return 0;
}
