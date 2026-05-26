/*
 * Nordschleife multicore MCW test
 *
 * Exercises the sa8797p-nsp machine's multi-core infrastructure and
 * inter-core communication via the Multicast Widget (MCW), in two
 * phases:
 *
 *   Phase 1 - map-reduce across every thread of every core.
 *     Core 0 broadcasts a seed value via MCW fanout to every worker
 *     core in a single transaction. Each core (coordinator
 *     included) spawns MAX_THREADS threads; each thread computes
 *     seed ^ (core_id * MAX_THREADS + tid) and atomically accumulates
 *     its calculations into a per-core sum. Each worker reports its
 *     core sum back via MCW reduce; core 0 verifies all sums.
 *
 *   Phase 2 - master-mask filtering.
 *     Core 0 broadcasts a "V1" sentinel, then changes its master
 *     MCID mask to exclude one worker core, broadcasts a "V2"
 *     sentinel, restores the mask, and broadcasts a "V3" check-now
 *     sentinel. Each worker, on seeing V3, inspects its own V2 slot and
 *     reports whether the broadcast landed. The excluded worker must
 *     report "did not see V2" while the others must report "saw V2".
 *
 * Run:
 *   qemu-system-hexagon -M sa8797p-nsp -smp cores=4,threads=4 \
 *       -kernel sa8797p_nsp_multicore -nographic -semihosting
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hexagon_standalone.h"
#include "cfgtable.h"
#include "mcw.h"
int err;
#include "../hex_test.h"

/******************************************************************************
 * Core-local compute: every thread contributes to core_sum
 *****************************************************************************/

#define MAX_THREADS     4
#define MAX_CORES    4
#define STACK_SIZE      4096

static char __attribute__((aligned(16))) stacks[MAX_THREADS][STACK_SIZE];

/* Per-core state for phase 1's compute. */
static volatile uint32_t compute_seed;     /* shared by all HW threads */
static volatile uint32_t core_sum;  /* updated atomically */

static void atomic_add_u32(volatile uint32_t *target, uint32_t val)
{
    asm volatile("1: r0 = memw_locked(%0)\n\t"
                 "   r0 = add(r0, %1)\n\t"
                 "   memw_locked(%0, p0) = r0\n\t"
                 "   if (!p0) jump 1b\n\t"
                 :
                 : "r"(target), "r"(val)
                 : "p0", "r0", "memory");
}

static void compute_thread(void *arg)
{
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    uint32_t core_id = read_cfgtable_field(CFGTABLE_CORE_ID);
    uint32_t val = compute_seed ^ (core_id * MAX_THREADS + tid);
    atomic_add_u32(&core_sum, val);
}

static void run_core_compute(uint32_t core_id)
{
    uint32_t active_mask = 0;

    for (int t = 1; t < MAX_THREADS; t++) {
        thread_create(compute_thread, &stacks[t][STACK_SIZE], t,
                      (void *)(uintptr_t)t);
        active_mask |= (1 << t);
    }
    compute_thread((void *)(uintptr_t)0);
    active_mask |= 1;
    thread_join(active_mask);

    printf("  core %u: sum=0x%lx\n", (unsigned)core_id, (unsigned long)core_sum);
}

/******************************************************************************
 * Inter-core communication: MCID assignment and shared data layout
 *****************************************************************************/

/*
 * MCID 0 (REDUCE): worker -> coordinator. Each worker's master LUT
 * points at port mask 0b0001 (core 0 only). The coordinator's slave
 * LUT maps MCID 0 to the start of its L2TCM, where core_slot_t
 * entries live.
 *
 * MCID 1 (BCAST): coordinator -> all workers. The coordinator's master
 * LUT points at port mask covering every worker. Every worker's slave
 * LUT maps MCID 1 to the start of its local L2TCM, where a
 * worker_bcast_t receive buffer lives.
 */
#define REDUCE_MCID  0
#define BCAST_MCID   1

/*
 * Per-core reduce slot. Different cores write at different byte offsets
 * within the same MCID.
 */
typedef struct {
    /* Phase 1: map-reduce */
    uint32_t phase1_ready;
    uint32_t phase1_sum;      /* aggregate of this core's thread XORs */
    uint32_t phase1_done;
    /* Phase 2: master-mask filter */
    uint32_t phase2_ack;
    uint32_t phase2_saw_v2;
    uint32_t phase2_report;
} core_slot_t;

/*
 * Broadcast-receive buffer in each worker's L2TCM. Same volatile
 * rationale as core_slot_t: core 0's MCW master is the writer.
 */
typedef struct {
    uint32_t map_seed;       /* phase 1 payload */
    uint32_t phase2_v1;
    uint32_t phase2_v2;
    uint32_t phase2_v3;
} worker_bcast_t;

#define PHASE1_SEED   0xc0ffee42U

/******************************************************************************
 * Worker core routines
 *****************************************************************************/

static uint32_t worker_slot_offset(uint32_t core_id)
{
    return core_id * (uint32_t)sizeof(core_slot_t);
}

static void worker_send_field(uint32_t core_id, size_t field_off, uint32_t val)
{
    mcw_fanout_write32(REDUCE_MCID,
                       worker_slot_offset(core_id) + (uint32_t)field_off, val);
}

/* Spin until *addr becomes non-zero and return the observed value. */
static uint32_t wait_nonzero(volatile uint32_t *addr)
{
    while (!*addr) {
        /* spin */
    }
    return *addr;
}

static void worker_core_main(uint32_t core_id)
{
    /* volatile: filled by core 0 via MCW fanout, not by this CPU. */
    volatile worker_bcast_t *bcast;
    uint32_t l2tcm_base = read_cfgtable_field(0x0) << 16;

    /*
     * Program our master LUT so MCID 0 targets core 0 only, and our
     * slave LUT so MCID 1 lands in the start of our own L2TCM.
     */
    mcw_master_enable_all_mask();
    mcw_master_set_portmask(REDUCE_MCID, 1U << 0);
    mcw_slave_set_entry(BCAST_MCID, 0, sizeof(worker_bcast_t));

    /* Zero the broadcast receive buffer before signalling ready. */
    bcast = (void *)(uintptr_t)l2tcm_base;
    memset((void *)bcast, 0, sizeof(*bcast));
    worker_send_field(core_id, offsetof(core_slot_t, phase1_ready), 1);

    /* Phase 1: wait for seed, run all local threads, send our core sum */
    compute_seed = wait_nonzero(&bcast->map_seed);
    core_sum = 0;
    run_core_compute(core_id);
    worker_send_field(core_id, offsetof(core_slot_t, phase1_sum),
                      core_sum);
    worker_send_field(core_id, offsetof(core_slot_t, phase1_done), 1);

    /* Phase 2, step 1: ack V1 so coordinator knows we're listening. */
    wait_nonzero(&bcast->phase2_v1);
    worker_send_field(core_id, offsetof(core_slot_t, phase2_ack), 1);

    /* Phase 2, step 3: wait for V3 (sent with the mask restored). */
    wait_nonzero(&bcast->phase2_v3);
    worker_send_field(core_id, offsetof(core_slot_t, phase2_saw_v2), bcast->phase2_v2);
    worker_send_field(core_id, offsetof(core_slot_t, phase2_report), 1);

    /* Done - halt forever */
    asm volatile("wait(r0)\n");
    __builtin_unreachable();
}

/******************************************************************************
 * Coordinator core routines
 *****************************************************************************/

/* ---- Machine addresses ---- */
#define PEER_CSR_BASE       0xf0000000U
#define PEER_CSR_STRIDE     0x00010000U

static void wake_peer_cores(uint32_t core_count)
{
    printf("  waking %d peer cores...\n", (int)(core_count - 1));
    for (uint32_t c = 1; c < core_count; c++) {
        volatile uint32_t *peer_pwr = (volatile uint32_t *)
            (PEER_CSR_BASE + c * PEER_CSR_STRIDE);
        *peer_pwr = 0x1;
    }
}

static uint32_t all_workers_mask(uint32_t core_count)
{
    return (uint32_t)(((1U << core_count) - 1) & ~1U) & 0xf;
}

static uint32_t expected_core_sum(uint32_t core_id, uint32_t seed)
{
    uint32_t sum = 0;
    for (uint32_t t = 0; t < MAX_THREADS; t++) {
        sum += seed ^ (core_id * MAX_THREADS + t);
    }
    return sum;
}

static void phase1_map_reduce(volatile core_slot_t *slots,
                              uint32_t core_count)
{
    /* Wait for every worker to have its slave LUT programmed. */
    for (uint32_t c = 1; c < core_count; c++) {
        wait_nonzero(&slots[c].phase1_ready);
    }

    printf("  phase 1: broadcasting seed 0x%lx...\n", (unsigned long)PHASE1_SEED);
    compute_seed = PHASE1_SEED;
    mcw_fanout_write32(BCAST_MCID, offsetof(worker_bcast_t, map_seed), PHASE1_SEED);

    /* Run our own contribution in parallel with the workers'. */
    core_sum = 0;
    run_core_compute(0);
    slots[0].phase1_sum = core_sum;
    slots[0].phase1_done = 1;

    for (uint32_t c = 0; c < core_count; c++) {
        wait_nonzero(&slots[c].phase1_done);
        check32(slots[c].phase1_sum, expected_core_sum(c, PHASE1_SEED));
    }
}

/* volatile: changed by other core */
static void phase2_mask_filter(volatile core_slot_t *slots, uint32_t core_count)
{
    uint32_t excluded = core_count - 1;
    uint32_t filter = (uint32_t)(~(1U << excluded)) & 0xf;

    printf("  phase 2: mask-filter test, excluding core %u...\n",
           (unsigned)excluded);

    /* Step 1: broadcast V1, wait for everyone to ack. */
    mcw_fanout_write32(BCAST_MCID, offsetof(worker_bcast_t, phase2_v1), 1);
    for (uint32_t c = 1; c < core_count; c++) {
        wait_nonzero(&slots[c].phase2_ack);
    }

    /* Step 2: apply the master-mask filter and broadcast V2. */
    mcw_master_set_mask(filter);
    mcw_fanout_write32(BCAST_MCID, offsetof(worker_bcast_t, phase2_v2), 1);

    /* Step 3: restore the mask and broadcast V3 to signal "check now". */
    mcw_master_set_mask(0xf);
    mcw_fanout_write32(BCAST_MCID, offsetof(worker_bcast_t, phase2_v3), 1);

    for (uint32_t c = 1; c < core_count; c++) {
        wait_nonzero(&slots[c].phase2_report);
        uint32_t expected_saw = (c == excluded) ? 0 : 1;
        check32(slots[c].phase2_saw_v2, expected_saw);
    }
}

static int coordinator_main(uint32_t core_count)
{
    uint32_t l2tcm_base = read_cfgtable_field(0x0) << 16;
    /* volatile: workers fill these slots via MCW, we poll them locally. */
    volatile core_slot_t *slots = (void *)(uintptr_t)l2tcm_base;

    printf("sa8797p-nsp multicore MCW test (%d cores)\n", (int)core_count);
    assert(core_count >= 3 && core_count <= MAX_CORES);

    /*
     * Program core 0's slave LUT: incoming MCW writes on REDUCE_MCID
     * land at the start of our L2TCM, covering the core_slot_t array.
     */
    mcw_slave_set_entry(REDUCE_MCID, 0, sizeof(core_slot_t) * MAX_CORES);

    /* Program the broadcast master LUT once: fan out to every worker. */
    mcw_master_enable_all_mask();
    mcw_master_set_portmask(BCAST_MCID, all_workers_mask(core_count));

    wake_peer_cores(core_count);

    phase1_map_reduce(slots, core_count);
    phase2_mask_filter(slots, core_count);

    puts(err ? "FAIL" : "PASS");
    return err;
}

/* Runs on every core's thread 0 */
int main(void)
{
    uint32_t core_id = read_cfgtable_field(CFGTABLE_CORE_ID);
    uint32_t core_count = read_cfgtable_field(CFGTABLE_CORE_COUNT);
    if (core_id == 0) {
        return coordinator_main(core_count);
    }
    worker_core_main(core_id);
    return 0;
}
