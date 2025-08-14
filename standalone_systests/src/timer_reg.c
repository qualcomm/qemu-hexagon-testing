/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "timer.h"

#if 0
#define DEBUG printf
#else
#define DEBUG(...)
#endif

int main()
{
    timer_init();

    uint64_t start = timer_read();
    for (int i = 0; i < 30; i++) {
        uint64_t val = timer_read();
        DEBUG("\treg:   %llu | %08llx\n", val, val);
        val = timer_read_pair();
        assert(val != 0);
        DEBUG("\tpair:  %llu | %08llx\n", val, val);
        val = utimer_read();
        assert(val != 0);
        DEBUG("\tureg:  %llu | %08llx\n", val, val);
        val = utimer_read_pair();
        DEBUG("\tupair: %llu | %08llx\n", val, val);
        assert(val != 0);
    }
    while (start == timer_read()) {
        ;
    }
    printf("PASS\n");
}
