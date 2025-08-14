/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "timer.h"
static int err; /* needed by pmu.h */
#include "pmu.h"

/* dummy function to set our breakpoint at */
void end_of_preparation(int arg1)
{
    asm volatile("nop");
}

int main()
{
    pmu_reset();
    toggle_ssr_pe(1);
    pmu_config(0, HVX_PKT);
    pmu_start();
    asm volatile(
        "nop\n"
        "nop\n"
        "{ v0 = vrmpyb(v0, v1); v2 = vrmpyb(v3, v4) }\n"
        "{ v0 = vrmpyb(v0, v1); nop; }\n"
        : : : "v0", "v2");
    timer_init();
    end_of_preparation(0xdeadbeef);
    puts("PASS");
    return 0;
}
