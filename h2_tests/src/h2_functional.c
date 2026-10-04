/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Combined h2 functional test.
 *
 * The test is an ordinary multi-threaded program.  It never touches the
 * scheduler, interrupt controller, timer or TLB locks itself; instead it
 * relies on the hypervisor using them to satisfy these requests:
 *
 *   - more tasks than hardware threads (scheduler, thread context switch)
 *   - mutex / condvar / semaphore / barrier traffic (kernel lock)
 *   - nanosleep with many concurrent sleepers (qtimer -> l2vic -> ISR)
 *   - HVX context hand-out, held across a sleep and a yield
 *   - HMX unit hand-out; VTCM is mapped with h2_tlb_alloc (tlblock)
 *
 * Every task contributes to shared totals which are compared with closed form
 * expected values, so a task that did not finish, ran twice or computed with
 * corrupted vector/matrix state fails the test.
 *
 * The test first asserts that the system has what it needs and fails with a
 * "FAIL config" line if not.
 */

#include "h2t.h"
#include <h2_common_pmap.h>
#include <h2_mxaccess.h>
#include <h2_vecaccess.h>
#include <hexagon_types.h>
#include <hmx_hexagon_protos.h>
#include <hvx_hexagon_protos.h>

#ifndef MIN_HW_THREADS
#define MIN_HW_THREADS 4
#endif
/* Temporary: build with -DHAVE_HMX=0 to drop all HMX use. */
#ifndef HAVE_HMX
#define HAVE_HMX 1
#endif
#define MAX_TASKS H2T_MAX_TASKS
#define SLEEP_NS 5000000ull
#define MUTEX_ITERS 50
#ifndef ITERATIONS
#define ITERATIONS 1 /* rounds of all phases; -DITERATIONS=N to run longer */
#endif
#define HVX_STEPS 32
#define LANES 32 /* 128-byte vector of 32-bit words */

#define TILE_BYTES 2048
#define BIAS_BYTES 256
#define PER_TASK 8192 /* keeps every tile 2KB aligned */
#define VTCM_WINDOW (256u << 10)
#define VTCM_PAGE_LOG2 6 /* 256KB; the largest page tlb_alloc accepts here */
#define HMX_PATTERNS 2

/*
 * Golden HMX output tile checksums, recorded from hexagon-sim.  Pattern 1
 * converts differently on v68 and v73.
 */
static const struct {
    unsigned long long fnv;
    unsigned sum;
} hmx_golden[HMX_PATTERNS] = {
    {0xf8ff4a5867cbf049ull, 257056},
#if __HEXAGON_ARCH__ >= 73
    {0x12227843bde17062ull, 261291},
#else
    {0x4b2d98ed76a693abull, 261560},
#endif
};

static h2_mutex_t mtx;
static h2_cond_t cond;
static h2_barrier_t barrier;
static h2_sem_t done;
static h2_vecaccess_state_t vacc;
static h2_mxaccess_state_t mx;

static unsigned ntasks;
static unsigned char *vtcm;
static volatile unsigned counter; /* under mtx */
static volatile unsigned turn;    /* under mtx, passed along by cond */
static volatile unsigned long long order_sum;
static volatile unsigned sleeps_done;
static volatile unsigned hvx_sum;
static volatile unsigned hmx_sum;
static volatile unsigned bad_hvx, bad_hmx, acquire_errors;
static HVX_Vector lane_ids __attribute__((aligned(128)));
static HVX_Vector hvx_out[MAX_TASKS] __attribute__((aligned(128)));

/* Abort with a clear message if the system cannot run this test. */
#define REQUIRE(cond, fmt, ...)                                                \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL config: need " #cond ": " fmt "\n", ##__VA_ARGS__);   \
            printf("FAIL h2_functional (unsupported configuration)\n");        \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static unsigned long long fnv1a(const unsigned char *p, unsigned n)
{
    unsigned long long h = 0xcbf29ce484222325ull;
    while (n--) {
        h = (h ^ *p++) * 0x100000001b3ull;
    }
    return h;
}

static void fill_hmx_inputs(unsigned pat, unsigned char *act,
                            unsigned char *wgt, unsigned *bias)
{
    for (unsigned i = 0; i < TILE_BYTES; i++) {
        act[i] = (unsigned char)(i * 7 + pat * 13 + (i >> 5));
        wgt[i] = (unsigned char)(i * 3 + pat * 5 + 1);
    }
    for (unsigned i = 0; i < BIAS_BYTES / 4; i++) {
        bias[i] = 0x4000 + pat;
    }
}

/* One activation x weight tile; the Rt operand is a length - 1. */
static void hmx_tile(unsigned char *act, unsigned char *wgt, unsigned char *out,
                     unsigned *bias)
{
    Q6_mxclracc();
    Q6_bias_mxmem_A(bias);
    Q6_activation_ub_mxmem_RR((unsigned)act, TILE_BYTES / 2 - 1);
    Q6_weight_b_mxmem_RR((unsigned)wgt, TILE_BYTES / 2 - 1);
    h2_yield(); /* the unit must still be ours with its accumulator intact */
    Q6_mxmem_AR_after_ub(out, TILE_BYTES / 2 - 1);
}

static __attribute__((noinline)) void run_round(unsigned me, unsigned it)
{

    /* Phase 1: kernel lock traffic, lined up to maximise contention. */
    h2_barrier_wait(&barrier);
    for (unsigned i = 0; i < MUTEX_ITERS; i++) {
        h2_mutex_lock(&mtx);
        counter++;
        if ((i & 7) == 0) {
            h2_yield();
        }
        h2_mutex_unlock(&mtx);
    }
    /* Take turns, in task-id order, through the condvar. */
    h2_mutex_lock(&mtx);
    while (turn != it * ntasks + me) {
        h2_cond_wait(&cond, &mtx);
    }
    order_sum += (unsigned long long)(me + 1) * (me + 1);
    turn++;
    h2_cond_broadcast(&cond);
    h2_mutex_unlock(&mtx);

    /* Phase 2: HVX, with a timed sleep and a yield while holding the context. */
    h2_vecaccess_ret_t got = h2_vecaccess_acquire(&vacc);
    if (got.idx < 0 || got.length != H2_VECACCESS_VLENGTH_128) {
        __atomic_fetch_add(&acquire_errors, 1, __ATOMIC_SEQ_CST);
        return;
    }
    HVX_Vector x = Q6_V_vzero();
    for (unsigned k = 0; k < HVX_STEPS; k++) {
        /* x += lane + me + k */
        x = Q6_Vw_vadd_VwVw(x, Q6_Vw_vadd_VwVw(lane_ids, Q6_V_vsplat_R(me + k)));
        if (k == HVX_STEPS / 2) {
            /*
             * h2 never wakes a sleeper whose deadline has already passed by
             * the time the timeout is programmed, so keep the sleeps long
             * compared with the scheduling jitter of a slow emulator.
             */
            h2_nanosleep(SLEEP_NS + 100000ull * me);
            __atomic_fetch_add(&sleeps_done, 1, __ATOMIC_SEQ_CST);
            h2_yield();
        }
    }
    hvx_out[me] = x;
    h2_vecaccess_release(&vacc, got.idx);
    const uint32_t *w = (const uint32_t *)&hvx_out[me];
    unsigned vsum = 0;
    for (unsigned l = 0; l < LANES; l++) {
        /* sum over k of (l + me + k) */
        uint32_t e = HVX_STEPS * (l + me) + HVX_STEPS * (HVX_STEPS - 1) / 2;
        if (w[l] != e) {
            __atomic_fetch_add(&bad_hvx, 1, __ATOMIC_SEQ_CST);
        }
        vsum += w[l];
    }
    __atomic_fetch_add(&hvx_sum, vsum, __ATOMIC_SEQ_CST);

#if HAVE_HMX
    /* Phase 3: HMX on a per-task VTCM tile, checked against golden values. */
    int idx = h2_mxaccess_acquire(&mx);
    if (idx < 0) {
        __atomic_fetch_add(&acquire_errors, 1, __ATOMIC_SEQ_CST);
        return;
    }
    unsigned char *act = vtcm + me * PER_TASK, *wgt = act + TILE_BYTES;
    unsigned char *out = wgt + TILE_BYTES;
    unsigned *bias = (unsigned *)(out + TILE_BYTES);
    unsigned pat = me % HMX_PATTERNS;
    fill_hmx_inputs(pat, act, wgt, bias);
    memset(out, 0xa5, TILE_BYTES);
    hmx_tile(act, wgt, out, bias);
    h2_mxaccess_release(&mx, idx);
    unsigned msum = 0;
    for (unsigned i = 0; i < TILE_BYTES; i++) {
        msum += out[i];
    }
    unsigned long long fnv = fnv1a(out, TILE_BYTES);
    if (fnv != hmx_golden[pat].fnv || msum != hmx_golden[pat].sum) {
        __atomic_fetch_add(&bad_hmx, 1, __ATOMIC_SEQ_CST);
        printf("task %u: HMX pattern %u fnv %#llx sum %u\n", me, pat, fnv, msum);
    }
    __atomic_fetch_add(&hmx_sum, msum, __ATOMIC_SEQ_CST);
#endif

}

static void task(void *arg)
{
    unsigned me = (unsigned)arg;
    for (unsigned it = 0; it < ITERATIONS; it++) {
        run_round(me, it);
    }
    h2_sem_up(&done);
}

int main(void)
{
    unsigned hthreads = __builtin_popcount(h2_info(INFO_HTHREADS));
    int hvx_len = h2_info(INFO_HVX_VLENGTH);
    int hvx_ctx = h2_info(INFO_COPROC_CONTEXTS);
    int hmx_units = h2_info(INFO_HMX_INSTANCES);
    unsigned vtcm_base = h2_info(INFO_VTCM_BASE);
    unsigned vtcm_size = h2_info(INFO_VTCM_SIZE) * 1024;
    printf("h2_functional: %u hw threads, HVX %d bytes x %d contexts, "
           "%d HMX, VTCM %#x (%u KB)\n",
           hthreads, hvx_len, hvx_ctx, hmx_units, vtcm_base, vtcm_size / 1024);

    ntasks = 2 * hthreads + 4; /* always more tasks than threads */
    REQUIRE(hthreads >= MIN_HW_THREADS, "have %u hw threads", hthreads);
    REQUIRE(ntasks <= MAX_TASKS, "%u tasks", ntasks);
    REQUIRE(hvx_len == 128, "HVX vector length is %d bytes", hvx_len);
    REQUIRE(hvx_ctx >= 1, "%d HVX contexts", hvx_ctx);
#if HAVE_HMX
    REQUIRE(hmx_units >= 1, "%d HMX units", hmx_units);
    REQUIRE(vtcm_size >= ntasks * PER_TASK, "VTCM is %u bytes, need %u",
            vtcm_size, ntasks * PER_TASK);
    REQUIRE(ntasks * PER_TASK <= VTCM_WINDOW, "test VTCM window too small");
#endif
    REQUIRE(h2_info(INFO_TIMER_BASE) != 0, "no timer");
    REQUIRE(h2_info(INFO_L2VIC_BASE) != 0, "no l2vic");

    h2_mutex_init(&mtx);
    h2_cond_init(&cond);
    h2_barrier_init(&barrier, ntasks);
    h2_sem_init_val(&done, 0); /* h2_sem_init() starts at 1 */
    uint32_t *ids = (uint32_t *)&lane_ids;
    for (unsigned l = 0; l < LANES; l++) {
        ids[l] = l;
    }
    REQUIRE(h2_vecaccess_init(&vacc, H2_VECACCESS_HVX_128) == 0, "vecaccess");
#if HAVE_HMX
    REQUIRE(h2_mxaccess_init(&mx) == 0, "mxaccess");

    /* Guests get no VTCM mapping; ask the kernel for a pinned TLB entry. */
    int t = h2_tlb_alloc(
        h2t_tlb_entry(vtcm_base, vtcm_base, VTCM_PAGE_LOG2, L1WB_L2C, URWX));
    REQUIRE(t >= 0, "h2_tlb_alloc for VTCM failed: %d", t);
    vtcm = (unsigned char *)vtcm_base;
#endif

    /* Spawn in reverse so the condvar chain starts with waiters. */
    for (int i = ntasks - 1; i >= 0; i--) {
        int id = h2t_spawn(i, task, (void *)i, 10 + (i % 3));
        if (id == 0 || (id & 1)) {
            /* Existing workers are blocked at the barrier.  Do not wait on done. */
            return h2t_finish("h2_functional");
        }
    }
    for (unsigned i = 0; i < ntasks; i++) {
        h2_sem_down(&done);
    }

    unsigned long long order_exp = 0;
    unsigned hvx_exp = 0, hmx_exp = 0;
    for (unsigned me = 0; me < ntasks; me++) {
        order_exp += (unsigned long long)(me + 1) * (me + 1) * ITERATIONS;
        for (unsigned l = 0; l < LANES; l++) {
            hvx_exp += ITERATIONS * (HVX_STEPS * (l + me) + HVX_STEPS * (HVX_STEPS - 1) / 2);
        }
#if HAVE_HMX
        hmx_exp += ITERATIONS * hmx_golden[me % HMX_PATTERNS].sum;
#endif
    }
    H2T_CHECK(acquire_errors == 0, "%u HVX/HMX acquire errors", acquire_errors);
    H2T_CHECK(counter == ntasks * MUTEX_ITERS * ITERATIONS,
              "counter %u != %u", counter, ntasks * MUTEX_ITERS * ITERATIONS);
    H2T_CHECK(turn == ntasks * ITERATIONS, "turn %u != %u", turn,
              ntasks * ITERATIONS);
    H2T_CHECK(order_sum == order_exp, "order_sum %llu != %llu", order_sum,
              order_exp);
    H2T_CHECK(sleeps_done == ntasks * ITERATIONS, "%u of %u sleeps completed",
              sleeps_done, ntasks * ITERATIONS);
    H2T_CHECK(bad_hvx == 0, "%u wrong HVX lanes", bad_hvx);
    H2T_CHECK(hvx_sum == hvx_exp, "HVX total %u != %u", hvx_sum, hvx_exp);
#if HAVE_HMX
    H2T_CHECK(bad_hmx == 0, "%u HMX tiles differ from golden", bad_hmx);
    H2T_CHECK(hmx_sum == hmx_exp, "HMX total %u != %u", hmx_sum, hmx_exp);
    h2_tlb_free(t);
#endif
    return h2t_finish("h2_functional");
}
