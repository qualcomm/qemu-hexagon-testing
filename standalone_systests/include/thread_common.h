/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef THREAD_COMMON_H
#define THREAD_COMMON_H

#include <stdint.h>
#define FORCE_INLINE __attribute__((always_inline))

/* MUST be inlined for start.c's reset handler */
static inline FORCE_INLINE uint32_t get_htid(void)
{
    uint32_t htid;
    asm volatile("%0 = htid\n\t" : "=r"(htid));
    return htid;
}

/* MUST be inlined for start.c's reset handler */
static inline FORCE_INLINE uint32_t remove_myself(uint32_t mask)
{
    return mask & ~(1 << get_htid());
}

#define THREAD_SEMAPHORE_OFF 0
#define THREAD_SEMAPHORE_ON_WAIT 1
#define THREAD_SEMAPHORE_GO 2
void set_semaphore_state(uint32_t mask, int state);

void create_waiting_thread(void (*func)(void *), void *sp, int tid, void *param);
void start_waiting_threads(uint32_t mask);
void thread_create_blocked(void (*func)(void *), void *sp, int tid, void *param);
void thread_run_blocked(void (*func)(void *), void *sp, int tid, void *param);

extern volatile int thread_semaphore[32]; /* volatile: changed by multiple threads */

/* MUST be inlined for start.c's reset handler */
static inline FORCE_INLINE void wait_on_semaphore(void)
{
    uint32_t htid = get_htid();
    thread_semaphore[htid] = THREAD_SEMAPHORE_ON_WAIT;
    while (thread_semaphore[htid] != THREAD_SEMAPHORE_GO) {
        asm volatile("pause(#1)\n");
    }
}

#endif
