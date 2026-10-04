/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Combined QuRT functional test.
 *
 * The test is an ordinary multi-threaded user process.  It never touches the
 * scheduler, interrupt controller, timer or TLB locks itself; it relies on
 * QuRT using them to satisfy these requests:
 *
 *   - more threads than hardware threads, at mixed priorities (scheduler)
 *   - mutex / condvar / semaphore / barrier traffic (kernel lock)
 *   - timed sleeps from many threads at once (qtimer -> l2vic -> ISR)
 *   - HVX unit lock, held across a sleep and a priority-neutral yield
 *   - HMX unit lock; VTCM mapped with qurt_mem_region_create (TLB, tlblock)
 *
 * Every thread contributes to shared totals which are compared with closed
 * form or golden expected values, so a thread that did not finish or computed
 * with corrupted vector/matrix state fails the test.
 *
 * The test first asserts that the system has what it needs and fails with a
 * "FAIL config" line if not.
 */

#include "qt.h"
#include <hexagon_types.h>
#include <hmx_hexagon_protos.h>
#include <hvx_hexagon_protos.h>

#define MIN_HW_THREADS 4
/* Temporary: build with -DHAVE_HMX=0 to drop all HMX use. */
#ifndef HAVE_HMX
#define HAVE_HMX 1
#endif
#define MUTEX_ITERS 50
#ifndef ITERATIONS
#define ITERATIONS 1 /* rounds of all phases; -DITERATIONS=N to run longer */
#endif
#define HVX_STEPS 32
#define LANES 32 /* 128-byte vector of 32-bit words */

#define TILE_BYTES 2048
#define BIAS_BYTES 256
#define PER_TASK 8192 /* keeps every tile 2KB aligned */
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

static qurt_mutex_t mtx;
static qurt_cond_t cond;
static qurt_barrier_t barrier;

static unsigned ntasks;
static unsigned char *vtcm;
static volatile unsigned counter; /* under mtx */
static volatile unsigned turn;    /* under mtx, passed along by cond */
static volatile unsigned long long order_sum;
static volatile unsigned sleeps_done;
static volatile unsigned hvx_sum;
static volatile unsigned hmx_sum;
static volatile unsigned bad_hvx, bad_hmx, lock_errors;
static HVX_Vector lane_ids __attribute__((aligned(128)));
static HVX_Vector hvx_out[QT_MAX_TASKS] __attribute__((aligned(128)));

/* Abort with a clear message if the system cannot run this test. */
#define REQUIRE(cond, fmt, ...)                                                \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL config: need " #cond ": " fmt "\n", ##__VA_ARGS__);   \
            printf("FAIL qurt_functional (unsupported configuration)\n");      \
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
    qt_yield(); /* the unit must still be ours with its accumulator intact */
    Q6_mxmem_AR_after_ub(out, TILE_BYTES / 2 - 1);
}

static __attribute__((noinline)) void run_round(unsigned me, unsigned it)
{

    /* Phase 1: kernel lock traffic, lined up to maximise contention. */
    qurt_barrier_wait(&barrier);
    for (unsigned i = 0; i < MUTEX_ITERS; i++) {
        qurt_mutex_lock(&mtx);
        counter++;
        if ((i & 7) == 0) {
            qt_yield();
        }
        qurt_mutex_unlock(&mtx);
    }
    /* Take turns, in thread-id order, through the condvar. */
    qurt_mutex_lock(&mtx);
    while (turn != it * ntasks + me) {
        qurt_cond_wait(&cond, &mtx);
    }
    order_sum += (unsigned long long)(me + 1) * (me + 1);
    turn++;
    qurt_cond_broadcast(&cond);
    qurt_mutex_unlock(&mtx);

    /* Phase 2: HVX, with a timed sleep and a yield while holding the unit. */
    if (qurt_hvx_lock(QURT_HVX_MODE_128B) != QURT_EOK) {
        __atomic_fetch_add(&lock_errors, 1, __ATOMIC_SEQ_CST);
        return;
    }
    HVX_Vector x = Q6_V_vzero();
    for (unsigned k = 0; k < HVX_STEPS; k++) {
        /* x += lane + me + k */
        x = Q6_Vw_vadd_VwVw(x, Q6_Vw_vadd_VwVw(lane_ids, Q6_V_vsplat_R(me + k)));
        if (k == HVX_STEPS / 2) {
            qurt_timer_sleep(150 + 10 * me); /* microseconds */
            __atomic_fetch_add(&sleeps_done, 1, __ATOMIC_SEQ_CST);
            qt_yield();
        }
    }
    hvx_out[me] = x;
    qurt_hvx_unlock();
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
    /* Phase 3: HMX on a per-thread VTCM tile, checked against golden values. */
    if (qurt_hmx_lock() != QURT_EOK) {
        __atomic_fetch_add(&lock_errors, 1, __ATOMIC_SEQ_CST);
        return;
    }
    unsigned char *act = vtcm + me * PER_TASK, *wgt = act + TILE_BYTES;
    unsigned char *out = wgt + TILE_BYTES;
    unsigned *bias = (unsigned *)(out + TILE_BYTES);
    unsigned pat = me % HMX_PATTERNS;
    fill_hmx_inputs(pat, act, wgt, bias);
    memset(out, 0xa5, TILE_BYTES);
    hmx_tile(act, wgt, out, bias);
    qurt_hmx_unlock();
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
}

/* VTCM lives in the TCM physical pool; its base depends on the core model
 * (v68n_1024: 0xd8400000, v73na_1: 0xd9000000 in the SDK simulators). */
static unsigned char *map_vtcm(size_t bytes)
{
#if __HEXAGON_ARCH__ >= 73
    const unsigned pa = 0xd9000000u;
#else
    const unsigned pa = 0xd8400000u;
#endif
    qurt_mem_pool_t pool;
    qurt_mem_region_attr_t attr;
    qurt_mem_region_t region;
    unsigned va = 0;
    if (qurt_mem_pool_attach("TCM_PHYSPOOL", &pool) != QURT_EOK) {
        return NULL;
    }
    qurt_mem_region_attr_init(&attr);
    qurt_mem_region_attr_set_mapping(&attr, QURT_MEM_MAPPING_PHYS_CONTIGUOUS);
    qurt_mem_region_attr_set_physaddr(&attr, pa);
    qurt_mem_region_attr_set_cache_mode(&attr, QURT_MEM_CACHE_WRITEBACK);
    if (qurt_mem_region_create(&region, bytes, pool, &attr) != QURT_EOK) {
        return NULL;
    }
    qurt_mem_region_attr_get(region, &attr);
    qurt_mem_region_attr_get_virtaddr(&attr, &va);
    return (unsigned char *)va;
}

int main(void)
{
    qurt_sysenv_hthreads_t ht = {0};
    qurt_arch_version_t av = {0};
    int rc_ht = qurt_sysenv_get_hw_threads(&ht);
    int rc_av = qurt_sysenv_get_arch_version(&av);
    int hvx_units = qurt_hvx_get_units();
    unsigned n128 = hvx_units > 0 ? ((unsigned)hvx_units >> 8) & 0xff : 0;
    printf("qurt_functional: %u hw threads, arch %#x, hvx units %#x\n",
           ht.hthreads, av.arch_version, (unsigned)hvx_units);

    ntasks = 2 * ht.hthreads + 4; /* always more threads than hw threads */
    REQUIRE(rc_ht == QURT_EOK, "qurt_sysenv_get_hw_threads -> %d", rc_ht);
    REQUIRE(rc_av == QURT_EOK && (av.arch_version & 0xff) >= 0x68,
            "arch version %#x (v68 or newer needed for HMX)", av.arch_version);
    REQUIRE(ht.hthreads >= MIN_HW_THREADS, "have %u hw threads", ht.hthreads);
    REQUIRE(ntasks <= QT_MAX_TASKS, "%u tasks", ntasks);
    REQUIRE(n128 >= 1, "no 128-byte HVX units (qurt_hvx_get_units %#x)",
            (unsigned)hvx_units);

    /* Timer: the system clock must advance across a sleep. */
    unsigned long long t0 = qurt_sysclock_get_hw_ticks();
    qurt_timer_sleep(200);
    unsigned long long t1 = qurt_sysclock_get_hw_ticks();
    REQUIRE(t1 > t0, "system clock did not advance across qurt_timer_sleep");

#if HAVE_HMX
    /* HMX: the unit must be lockable. */
    int hrc = qurt_hmx_lock();
    REQUIRE(hrc == QURT_EOK, "qurt_hmx_lock -> %d (no HMX unit?)", hrc);
    qurt_hmx_unlock();

    vtcm = map_vtcm(ntasks * PER_TASK);
    REQUIRE(vtcm != NULL, "could not map %u bytes of VTCM", ntasks * PER_TASK);
#endif

    qurt_mutex_init(&mtx);
    qurt_cond_init(&cond);
    qurt_barrier_init(&barrier, ntasks);
    uint32_t *ids = (uint32_t *)&lane_ids;
    for (unsigned l = 0; l < LANES; l++) {
        ids[l] = l;
    }

    /* Spawn in reverse so the condvar chain starts with waiters. */
    for (int i = ntasks - 1; i >= 0; i--) {
        if (qt_spawn(i, task, (void *)i, 100 + (i % 3)) != QURT_EOK) {
            /* Existing workers are blocked at the barrier.  Do not join them. */
            return qt_finish("qurt_functional");
        }
    }
    qt_join_all(ntasks);

    unsigned long long order_exp = 0;
    unsigned hvx_exp = 0;
#if HAVE_HMX
    unsigned hmx_exp = 0;
#endif
    for (unsigned me = 0; me < ntasks; me++) {
        order_exp += (unsigned long long)(me + 1) * (me + 1) * ITERATIONS;
        for (unsigned l = 0; l < LANES; l++) {
            hvx_exp += ITERATIONS * (HVX_STEPS * (l + me) + HVX_STEPS * (HVX_STEPS - 1) / 2);
        }
#if HAVE_HMX
        hmx_exp += ITERATIONS * hmx_golden[me % HMX_PATTERNS].sum;
#endif
    }
    QT_CHECK(lock_errors == 0, "%u HVX/HMX lock errors", lock_errors);
    QT_CHECK(counter == ntasks * MUTEX_ITERS * ITERATIONS,
             "counter %u != %u", counter, ntasks * MUTEX_ITERS * ITERATIONS);
    QT_CHECK(turn == ntasks * ITERATIONS, "turn %u != %u", turn,
             ntasks * ITERATIONS);
    QT_CHECK(order_sum == order_exp, "order_sum %llu != %llu", order_sum,
             order_exp);
    QT_CHECK(sleeps_done == ntasks * ITERATIONS, "%u of %u sleeps completed",
             sleeps_done, ntasks * ITERATIONS);
    QT_CHECK(bad_hvx == 0, "%u wrong HVX lanes", bad_hvx);
    QT_CHECK(hvx_sum == hvx_exp, "HVX total %u != %u", hvx_sum, hvx_exp);
#if HAVE_HMX
    QT_CHECK(bad_hmx == 0, "%u HMX tiles differ from golden", bad_hmx);
    QT_CHECK(hmx_sum == hmx_exp, "HMX total %u != %u", hmx_sum, hmx_exp);
#endif
    return qt_finish("qurt_functional");
}
