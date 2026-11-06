/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <hexagon_standalone.h>

#define NO_DEFAULT_EVENT_HANDLES
#include "mmu.h"

#define ITERATIONS 20000
#define MAX_SLOTS 64
/* Keep the scan within the first 128 TLB entries. */
#define TLB_SCAN_LIMIT 128
#define OLD_ASID 81
#define NEW_ASID 82
#define VALID (1ULL << 63)
#define STACK_SZ 4096
#define SYSCFG_MMUEN_BIT 0

static atomic_int stop;
static atomic_int worker_started;
static long long thread1_stack[STACK_SZ / sizeof(long long)];
static unsigned slots[MAX_SLOTS];

/* 4 KiB entries at virtual addresses that the test does not use. */
static uint64_t entry(unsigned asid, unsigned slot)
{
    return VALID | ((uint64_t)asid << 52) |
           ((uint64_t)(0x60000 + slot) << 32) | 1;
}

static void invalidate_worker(void *unused)
{
    tlbinvasid(OLD_ASID << 20);
    atomic_store_explicit(&worker_started, 1, memory_order_release);
    while (!atomic_load_explicit(&stop, memory_order_relaxed)) {
        tlbinvasid(OLD_ASID << 20);
    }
}

int main(void)
{
    unsigned count = 0;
    int failed = 0;
    uint32_t syscfg;

    /* Use only invalid entries, so the runtime mappings stay intact. */
    for (unsigned i = 1; i < TLB_SCAN_LIMIT && count < MAX_SLOTS; i++) {
        if (!(tlbr(i) & VALID)) {
            slots[count++] = i;
        }
    }
    if (count == 0) {
        puts("FAIL: no unused TLB entries available");
        return 1;
    }
    /*
     * The sim runtime uses identity mappings. Disable translation during
     * this TLB-only test so synchronized soft-TLB flushes do not limit
     * overlap between invalidation and replacement.
     */
    asm volatile("%0 = syscfg\n"
                 "r0 = clrbit(%0, #%1)\n"
                 "syscfg = r0\n"
                 : "=r"(syscfg) : "i"(SYSCFG_MMUEN_BIT) : "r0", "memory");
    thread_create(invalidate_worker,
                  &thread1_stack[STACK_SZ / sizeof(long long) - 2], 1, NULL);
    while (!atomic_load_explicit(&worker_started, memory_order_acquire)) {
    }
    for (unsigned n = 0; n < ITERATIONS && !failed; n++) {
        for (unsigned i = 0; i < count; i++) {
            tlbw(entry(OLD_ASID, i), slots[i]);
        }
        for (unsigned i = 0; i < count; i++) {
            tlbw(entry(NEW_ASID, i), slots[i]);
        }
        for (unsigned i = 0; i < count && !failed; i++) {
            uint64_t value = tlbr(slots[i]);
            if (value != entry(NEW_ASID, i)) {
                printf("FAIL: TLB[%u]=%016llx, the replacement was lost\n",
                       slots[i], (unsigned long long)value);
                failed = 1;
            }
        }
    }
    atomic_store_explicit(&stop, 1, memory_order_relaxed);
    thread_join(1 << 1);
    asm volatile("syscfg = %0" :: "r"(syscfg) : "memory");
    if (!failed) {
        puts("PASS");
    }
    return failed;
}
