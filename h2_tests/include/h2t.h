/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* Minimal test helpers for h2 guest programs. */

#ifndef H2T_H
#define H2T_H 1

#include <h2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define H2T_STACK_BYTES 8192
#define H2T_MAX_TASKS 64

static int h2t_failures;

#define H2T_CHECK(cond, fmt, ...)                                             \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s: " fmt "\n", __FILE__, __LINE__, #cond,     \
                   ##__VA_ARGS__);                                             \
            h2t_failures++;                                                    \
        }                                                                      \
    } while (0)

static inline int h2t_finish(const char *name)
{
    if (h2t_failures) {
        printf("FAIL %s (%d failures)\n", name, h2t_failures);
        return 1;
    }
    printf("PASS %s\n", name);
    return 0;
}

/* Stacks for spawned tasks; 8-byte aligned as required by the ABI. */
static unsigned long long h2t_stacks[H2T_MAX_TASKS]
                                    [H2T_STACK_BYTES / sizeof(unsigned long long)];

/*
 * Spawn task `i` running fn(arg) at priority prio.  Returns the h2 thread id.
 * h2 thread ids are odd on failure.
 */
static inline int h2t_spawn(int i, void (*fn)(void *), void *arg, unsigned prio)
{
    void *top = &h2t_stacks[i][H2T_STACK_BYTES / sizeof(unsigned long long)];
    int id = h2_thread_create((void *)fn, top, arg, prio);
    H2T_CHECK(id != 0 && !(id & 1), "h2_thread_create(%d) -> %d", i, id);
    return id;
}

/* Deterministic per-task pseudo random, so expected results are computable. */
static inline uint32_t h2t_lcg(uint32_t x)
{
    return x * 1664525u + 1013904223u;
}

/*
 * Build a 64-bit hexagon TLB entry (for h2_tlb_alloc) mapping a naturally
 * aligned page of 4KB << (2 * size_code) bytes.  log2pages = 2 * size_code.
 * The kernel overwrites the ASID field.
 */
static inline unsigned long long h2t_tlb_entry(unsigned va, unsigned pa,
                                               unsigned log2pages,
                                               unsigned cccc, unsigned xwru)
{
    unsigned ppn = pa >> 12;
    unsigned ppd = ((ppn & -(1u << log2pages)) << 1) | (1u << log2pages);
    unsigned lo = ppd | (cccc << 24) | (xwru << 28);
    unsigned hi = (va >> 12) | (1u << 31);
    return ((unsigned long long)hi << 32) | lo;
}

#endif
