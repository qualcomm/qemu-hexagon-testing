/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stdio.h>
#include <hexagon_standalone.h>

#include "interrupts.h"

#define ITERATIONS 100000
#define STACK_SZ 4096

static volatile int stop;
static long long thread1_stack[STACK_SZ / sizeof(long long)];

static uint32_t read_imask(void)
{
    uint32_t value;
    asm volatile("%0 = imask" : "=r"(value) :: "memory");
    return value;
}

/* Set IMASK bit 0 of thread 0 and leave all other bits unchanged. */
static void assign_worker(void *unused)
{
    while (!stop) {
        asm volatile("iassignw(%0)" :: "r"(1) : "memory");
    }
}

int main(void)
{
    uint32_t mask = 2;

    thread_create(assign_worker,
                  &thread1_stack[STACK_SZ / sizeof(long long) - 2], 1, NULL);
    for (unsigned n = 0; n < ITERATIONS && (mask & 2); n++) {
        set_thread_imask(0);
        delay(n % 8);
        set_thread_imask(2);
        delay(1);
        mask = read_imask();
    }
    stop = 1;
    thread_join(1 << 1);
    if (!(mask & 2)) {
        printf("FAIL: IMASK=%08x, the local write was lost\n", (unsigned)mask);
        return 1;
    }
    puts("PASS");
    return 0;
}
