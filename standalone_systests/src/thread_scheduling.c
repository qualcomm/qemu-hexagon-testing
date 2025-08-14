/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * This test runs a loop in a thread until the main thread sets a global
 * variable. This checks whether QEMU is properly switching threads, in order
 * to allow the main thread to set the variable.
 */

#include <hexagon_standalone.h>
#include <stdio.h>


volatile int running = 1;

#define STACK_SZ 1024
long long stack[STACK_SZ];
void thread_fn(void *id)
{
    while (running)
        ;
}

int main()
{
    thread_create(thread_fn, &stack[STACK_SZ - 1], 1, NULL);
    running = 0;
    thread_join(1 << 1);
    printf("PASS\n");
    return 0;
}
