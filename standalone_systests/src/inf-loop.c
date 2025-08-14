/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cfgtable.h"
#include <hexagon_standalone.h>
#include <string.h>

static inline void infloop(void)
{
    while (1) {
        asm volatile("pause (#200)\n");
    }
}

#define NUM_THREADS 4
#define STACK_SIZE 0x8000
char __attribute__((aligned(16))) stack[NUM_THREADS][STACK_SIZE];
static void thread(void *y)
{
    int id = (int)y;
    printf("Starting thread %d\n", id);
    infloop();
}

#define THREAD_ENABLE_MASK 0x48

int main()
{
    assert(read_cfgtable_field(THREAD_ENABLE_MASK) & 0xf);
    printf("Starting thread 0\n");
    for (int i = 1; i < NUM_THREADS; i++) {
        thread_create(thread, (void *)&stack[i - 1][STACK_SIZE - 16], i,
                      (void *)i);
    }
    infloop();
    return 0;
}
