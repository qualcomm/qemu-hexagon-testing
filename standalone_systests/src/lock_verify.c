/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Lock Verification Test
 *
 * Verifies correct behavior of k0lock/k0unlock and tlblock/tlbunlock:
 *   - SYSCFG.KL (bit 12) is set by k0lock and cleared by k0unlock
 *   - SYSCFG.TL (bit 11) is set by tlblock and cleared by tlbunlock
 *   - Both locks are independent (holding one does not affect the other)
 *   - k0lock provides mutual exclusion (shared counter test)
 *   - tlblock provides mutual exclusion (shared counter test)
 *   - Interrupts are delivered while k0lock is held; SYSCFG.KL remains
 *     set through the ISR; mutual exclusion is preserved
 *   - Same for tlblock
 *   - A thread stalled waiting to acquire k0lock/tlblock does not service
 *     interrupts (checked over several trials; see the stall section)
 *   - Contended acquisitions preserve mutual exclusion under interrupt
 *     pressure, and each confirmed owner can service an interrupt
 */

#include <stdio.h>
#include <stdatomic.h>
#include "hexagon_standalone.h"
#include "interrupts.h"
#include "thread_common.h"

static int err;
#include "hex_test.h"

#define SYSCFG_TL_BIT   11
#define SYSCFG_KL_BIT   12

#define COMPUTE_THREADS 3
#define STACK_SIZE      16384
#define ITERATIONS      1000

static char stack[COMPUTE_THREADS][STACK_SIZE] __attribute__((__aligned__(8)));

/* From the hexagon standalone runtime */
void register_interrupt(int intno, void (*handler)(int intno));

static inline void do_k0lock(void)
{
    asm volatile("k0lock\n" : : : "memory");
}

static inline void do_k0unlock(void)
{
    asm volatile("k0unlock\n" : : : "memory");
}

static inline void do_tlblock(void)
{
    asm volatile("tlblock\n" : : : "memory");
}

static inline void do_tlbunlock(void)
{
    asm volatile("tlbunlock\n" : : : "memory");
}

static inline uint32_t get_syscfg(void)
{
    uint32_t reg;
    asm volatile("%0 = syscfg" : "=r"(reg));
    return reg;
}


/* ===== Single-thread SYSCFG bit verification ===== */

/*
 * Verify SYSCFG.KL bit is set by k0lock and cleared by k0unlock.
 */
static void test_k0lock_syscfg_bit(void)
{
    uint32_t syscfg;

    printf("k0lock/k0unlock SYSCFG.KL bit\n");

    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 0);

    do_k0lock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 1);

    do_k0unlock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 0);
}

/*
 * Verify SYSCFG.TL bit is set by tlblock and cleared by tlbunlock.
 */
static void test_tlblock_syscfg_bit(void)
{
    uint32_t syscfg;

    printf("tlblock/tlbunlock SYSCFG.TL bit\n");

    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 0);

    do_tlblock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 1);

    do_tlbunlock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 0);
}

/*
 * Verify k0lock and tlblock are independent locks.
 * Holding one should not affect the other's SYSCFG bit.
 */
static void test_locks_independent(void)
{
    uint32_t syscfg;

    printf("k0lock and tlblock are independent\n");

    do_k0lock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 1);
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 0);

    do_tlblock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 1);
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 1);

    do_k0unlock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 0);
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 1);

    do_tlbunlock();
    syscfg = get_syscfg();
    check32((syscfg >> SYSCFG_KL_BIT) & 1, 0);
    check32((syscfg >> SYSCFG_TL_BIT) & 1, 0);
}

/* ===== Multi-thread mutual exclusion (no interrupts) ===== */

static volatile int k0_shared_counter;

static void k0lock_counter_thread(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        do_k0lock();
        int temp = k0_shared_counter;
        delay(4);
        k0_shared_counter = temp + 1;
        do_k0unlock();
    }
}

static void test_k0lock_mutual_exclusion(void)
{
    uint32_t thread_mask = ((1 << COMPUTE_THREADS) - 1) << 1;

    printf("k0lock mutual exclusion (%d threads x %d iterations)\n",
           COMPUTE_THREADS, ITERATIONS);

    k0_shared_counter = 0;

    for (int i = 0; i < COMPUTE_THREADS; i++) {
        create_waiting_thread(k0lock_counter_thread,
                              &stack[i][STACK_SIZE - 16], i + 1, NULL);
    }
    start_waiting_threads(thread_mask);
    thread_join(thread_mask);

    printf("  k0lock counter: %d (expected %d)\n",
           k0_shared_counter, COMPUTE_THREADS * ITERATIONS);
    check32(k0_shared_counter, COMPUTE_THREADS * ITERATIONS);
}

static volatile int tlb_shared_counter;

static void tlblock_counter_thread(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        do_tlblock();
        int temp = tlb_shared_counter;
        delay(4);
        tlb_shared_counter = temp + 1;
        do_tlbunlock();
    }
}

static void test_tlblock_mutual_exclusion(void)
{
    uint32_t thread_mask = ((1 << COMPUTE_THREADS) - 1) << 1;

    printf("tlblock mutual exclusion (%d threads x %d iterations)\n",
           COMPUTE_THREADS, ITERATIONS);

    tlb_shared_counter = 0;

    for (int i = 0; i < COMPUTE_THREADS; i++) {
        create_waiting_thread(tlblock_counter_thread,
                              &stack[i][STACK_SIZE - 16], i + 1, NULL);
    }
    start_waiting_threads(thread_mask);
    thread_join(thread_mask);

    printf("  tlblock counter: %d (expected %d)\n",
           tlb_shared_counter, COMPUTE_THREADS * ITERATIONS);
    check32(tlb_shared_counter, COMPUTE_THREADS * ITERATIONS);
}

/* ===== Mutual exclusion under interrupt pressure ===== */

/*
 * Interrupt handler that records SYSCFG lock state.
 *
 * The architecture does NOT mask interrupts when a hardware lock is held.
 * A thread that has acquired k0lock or tlblock can still be interrupted.
 * The lock bit in SYSCFG remains set throughout the ISR. After rte, the
 * thread resumes its critical section with the lock still held.
 *
 * This handler must NOT attempt to acquire k0lock or tlblock -- the
 * interrupted thread may already hold it, which would deadlock.
 */
static volatile int isr_count;
static volatile int isr_kl_set_count;
static volatile int isr_tl_set_count;

static void lock_interrupt_handler(int intno)
{
    uint32_t syscfg;
    asm volatile("%0 = syscfg" : "=r"(syscfg));
    if ((syscfg >> SYSCFG_KL_BIT) & 1)
        isr_kl_set_count++;
    if ((syscfg >> SYSCFG_TL_BIT) & 1)
        isr_tl_set_count++;
    isr_count++;
}

/*
 * Worker thread for the interrupt test -- same read-delay-write pattern
 * but with delay(1024) inside the critical section so the lock is held for
 * a substantial window, making it very likely that an SWI lands while the
 * lock bit is set.
 */
static void k0lock_counter_thread_long(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        do_k0lock();
        int temp = k0_shared_counter;
        delay(1024);
        k0_shared_counter = temp + 1;
        do_k0unlock();
    }
}

static void tlblock_counter_thread_long(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        do_tlblock();
        int temp = tlb_shared_counter;
        delay(1024);
        tlb_shared_counter = temp + 1;
        do_tlbunlock();
    }
}

/*
 * k0lock mutual exclusion with interrupts.
 *
 * Worker threads protect a shared counter with k0lock while the main
 * thread fires software interrupts.  Interrupt handler records whether
 * SYSCFG.KL was set when the ISR ran, proving the interrupt was delivered
 * while the lock was held.  The shared counter must still be correct,
 * proving mutual exclusion is maintained across interrupts.
 */
static void test_k0lock_with_interrupts(void)
{
    uint32_t thread_mask = ((1 << COMPUTE_THREADS) - 1) << 1;

    printf("k0lock mutual exclusion with interrupts "
           "(%d threads x %d iterations)\n", COMPUTE_THREADS, ITERATIONS);

    k0_shared_counter = 0;
    isr_count = 0;
    isr_kl_set_count = 0;
    isr_tl_set_count = 0;

    register_interrupt(0, lock_interrupt_handler);
    set_thread_imask(0xffffffff);   /* Mask all ints for thread 0 (main) */
    iassignw(0, 1);          /* Exclude thread 0 from interrupt 0 */

    for (int i = 0; i < COMPUTE_THREADS; i++) {
        create_waiting_thread(k0lock_counter_thread_long,
                              &stack[i][STACK_SIZE - 16], i + 1, NULL);
    }
    start_waiting_threads(thread_mask);

    /* Fire SWIs while workers are running their critical sections */
    for (int i = 0; i < ITERATIONS; i++) {
        swi(1);              /* Trigger interrupt 0 */
        delay(1);
    }

    thread_join(thread_mask);

    printf("  k0lock counter: %d (expected %d)\n",
           k0_shared_counter, COMPUTE_THREADS * ITERATIONS);
    check32(k0_shared_counter, COMPUTE_THREADS * ITERATIONS);

    printf("  Interrupts taken: %d (while KL held: %d)\n",
           isr_count, isr_kl_set_count);
    /* Verify interrupts were actually delivered */
    check32_ne(isr_count, 0);
    /* Verify at least one interrupt landed inside a critical section */
    check32_ne(isr_kl_set_count, 0);
}

/*
 * tlblock mutual exclusion with interrupts.
 */
static void test_tlblock_with_interrupts(void)
{
    uint32_t thread_mask = ((1 << COMPUTE_THREADS) - 1) << 1;

    printf("tlblock mutual exclusion with interrupts "
           "(%d threads x %d iterations)\n", COMPUTE_THREADS, ITERATIONS);

    tlb_shared_counter = 0;
    isr_count = 0;
    isr_kl_set_count = 0;
    isr_tl_set_count = 0;

    register_interrupt(0, lock_interrupt_handler);
    set_thread_imask(0xffffffff);
    iassignw(0, 1);

    for (int i = 0; i < COMPUTE_THREADS; i++) {
        create_waiting_thread(tlblock_counter_thread_long,
                              &stack[i][STACK_SIZE - 16], i + 1, NULL);
    }
    start_waiting_threads(thread_mask);

    for (int i = 0; i < ITERATIONS; i++) {
        swi(1);
        delay(1);
    }

    thread_join(thread_mask);

    printf("  tlblock counter: %d (expected %d)\n",
           tlb_shared_counter, COMPUTE_THREADS * ITERATIONS);
    check32(tlb_shared_counter, COMPUTE_THREADS * ITERATIONS);

    printf("  Interrupts taken: %d (while TL held: %d)\n",
           isr_count, isr_tl_set_count);
    check32_ne(isr_count, 0);
    check32_ne(isr_tl_set_count, 0);
}

#define SPIN_LIMIT 20000000u
#define WAITER_MASK ((1u << 2) | (1u << 3))

struct contention_lock {
    void (*lock)(void);
    void (*unlock)(void);
};

/* ===== Stalled waiter does not service interrupts ===== */

/*
 * A thread stalled on k0lock/tlblock is under a stall condition and does
 * not service interrupts; the interrupt is steered elsewhere or stays
 * pending until the lock is acquired.
 *
 * The stalled interval cannot be observed from another thread: a waiter
 * that has signalled readiness may not yet have issued the lock packet
 * (e.g. the host descheduled it under MTTCG), and an interrupt taken in
 * that window is legitimate.  So a single trial cannot be conclusive in
 * the failing direction.  Instead run several trials.  A correct
 * implementation never delivers while a waiter is stalled, so any trial
 * where the waiters have genuinely reached the lock is clean; an
 * implementation that delivers to stalled threads does so every trial.
 * The test therefore requires at least one clean trial.
 *
 * Thread 1 holds the lock with interrupts masked, and threads 2-3 (the
 * waiters) are the only threads eligible for interrupt 0.  SWIs are fired
 * only after a settle delay, and the lock is released only afterwards.
 */
#define STALL_TRIALS 8
#define STALL_SWIS 50
#define STALL_SETTLE_ITERS 20000

static atomic_uint stall_holder_ready;
static atomic_uint stall_waiters_ready;
static atomic_uint stall_release;
static atomic_uint stall_released_flag;
static atomic_uint stall_isr_before_release;
static atomic_uint stall_isr_total;

static void stall_isr(int intno)
{
    if (!atomic_load(&stall_released_flag))
        atomic_fetch_add(&stall_isr_before_release, 1);
    atomic_fetch_add(&stall_isr_total, 1);
}

static void stall_holder(void *arg)
{
    const struct contention_lock *ops = arg;

    set_thread_imask(0xffffffff);
    ops->lock();
    atomic_store(&stall_holder_ready, 1);
    for (unsigned n = 0; n < SPIN_LIMIT && !atomic_load(&stall_release); n++)
        pause();
    atomic_store(&stall_released_flag, 1);
    ops->unlock();
}

static void stall_waiter(void *arg)
{
    const struct contention_lock *ops = arg;

    atomic_fetch_add(&stall_waiters_ready, 1);
    ops->lock();
    /* Let legitimately pending interrupts drain while we own the lock. */
    delay(1024);
    ops->unlock();
}

static void test_stalled_waiter_no_interrupt(const char *name,
                                             void (*lock)(void),
                                             void (*unlock)(void))
{
    struct contention_lock ops = { lock, unlock };
    int clean_trials = 0;

    printf("stalled %s thread does not service interrupts\n", name);
    set_thread_imask(0xffffffff);
    iassignw(0, 0x3);  /* Exclude threads 0 and 1 */
    register_interrupt(0, stall_isr);

    for (int trial = 0; trial < STALL_TRIALS; trial++) {
        atomic_store(&stall_holder_ready, 0);
        atomic_store(&stall_waiters_ready, 0);
        atomic_store(&stall_release, 0);
        atomic_store(&stall_released_flag, 0);
        atomic_store(&stall_isr_before_release, 0);
        atomic_store(&stall_isr_total, 0);

        create_waiting_thread(stall_holder, &stack[0][STACK_SIZE - 16], 1,
                              &ops);
        start_waiting_threads(1 << 1);
        for (unsigned n = 0; n < SPIN_LIMIT &&
                             !atomic_load(&stall_holder_ready); n++)
            pause();
        check32(atomic_load(&stall_holder_ready), 1);

        for (unsigned tid = 2; tid <= 3; tid++)
            create_waiting_thread(stall_waiter,
                                  &stack[tid - 1][STACK_SIZE - 16], tid,
                                  &ops);
        start_waiting_threads(WAITER_MASK);
        for (unsigned n = 0; n < SPIN_LIMIT &&
                             atomic_load(&stall_waiters_ready) != 2; n++)
            pause();
        check32(atomic_load(&stall_waiters_ready), 2);
        delay(STALL_SETTLE_ITERS);

        for (int i = 0; i < STALL_SWIS; i++) {
            swi(1);
            delay(1);
        }
        delay(STALL_SETTLE_ITERS);

        /* Snapshot before releasing: later ISRs are legitimate. */
        if (atomic_load(&stall_isr_before_release) == 0)
            clean_trials++;
        atomic_store(&stall_release, 1);
        thread_join((1 << 1) | WAITER_MASK);
    }

    printf("  clean trials: %d / %d\n", clean_trials, STALL_TRIALS);
    check32_ne(clean_trials, 0);
    /*
     * Self-validation of the last trial: the SWIs were delivered to the
     * waiters once they owned the lock, so interrupt delivery works.
     */
    check32_ne(atomic_load(&stall_isr_total), 0);
}

/* ===== Contended acquisition and interrupt delivery ===== */

/*
 * A signal immediately before k0lock/tlblock does not prove that the
 * instruction has executed. A waiter may be descheduled after the signal,
 * and interrupts before its acquisition attempt are legitimate. No delay
 * on another thread can establish that the waiter has actually stalled.
 *
 * Instead, hold the lock on thread 0 while starting two waiters and firing
 * SWIs. Allow interrupts before acquisition. After releasing the initial
 * owner, wait for each waiter to report a completed acquisition, then
 * require it to handle an interrupt before allowing it to unlock. Service
 * the waiters in acquisition order, which need not be thread-ID order.
 *
 * This tests exclusion and owner interrupt delivery without claiming to
 * observe the unobservable interval inside another thread's lock packet.
 */
#define CONTENTION_SWIS 200

static atomic_uint waiters_ready;
static atomic_uint acquired_mask;
static atomic_uint release_mask;
static atomic_uint active_owners;
static atomic_uint overlapping_owners;
static atomic_uint contention_irqs[6];
static atomic_uint missing_lock_in_isr;
static atomic_uint acquire_irqs[6];
static uint32_t contention_syscfg_mask;

/* Wait for the ISR count of tid to move past 'before'; false on timeout. */
static int wait_irq_after(unsigned tid, unsigned before)
{
    for (unsigned n = 0; n < SPIN_LIMIT; n++) {
        if (atomic_load(&contention_irqs[tid]) != before)
            return 1;
        pause();
    }
    return 0;
}

static void contention_isr(int intno)
{
    unsigned tid = get_htid();
    unsigned bit = 1u << tid;

    if ((atomic_load(&acquired_mask) & bit) &&
        !(atomic_load(&release_mask) & bit) &&
        !(get_syscfg() & contention_syscfg_mask)) {
        atomic_fetch_add(&missing_lock_in_isr, 1);
    }
    atomic_fetch_add(&contention_irqs[tid], 1);
}

static void contention_waiter(void *arg)
{
    const struct contention_lock *ops = arg;
    unsigned bit = 1u << get_htid();

    atomic_fetch_add(&waiters_ready, 1);
    ops->lock();
    if (atomic_fetch_add(&active_owners, 1) != 0)
        atomic_fetch_add(&overlapping_owners, 1);
    atomic_store(&acquire_irqs[get_htid()],
                 atomic_load(&contention_irqs[get_htid()]));
    atomic_fetch_or(&acquired_mask, bit);
    while (!(atomic_load(&release_mask) & bit))
        pause();
    atomic_fetch_sub(&active_owners, 1);
    ops->unlock();
}

static void test_contended_lock_with_interrupts(const char *name,
                                               void (*lock)(void),
                                               void (*unlock)(void),
                                               unsigned syscfg_bit)
{
    struct contention_lock ops = { lock, unlock };
    unsigned pending = WAITER_MASK;

    printf("contended %s acquisition and owner interrupts\n", name);
    atomic_store(&waiters_ready, 0);
    atomic_store(&acquired_mask, 0);
    atomic_store(&release_mask, 0);
    atomic_store(&active_owners, 0);
    atomic_store(&overlapping_owners, 0);
    atomic_store(&missing_lock_in_isr, 0);
    for (unsigned tid = 0; tid < 6; tid++) {
        atomic_store(&contention_irqs[tid], 0);
        atomic_store(&acquire_irqs[tid], 0);
    }
    contention_syscfg_mask = 1u << syscfg_bit;

    set_thread_imask(0xffffffff);
    /* Exclude threads 0 and 1; allow interrupts on the waiters. */
    iassignw(0, 0x3);
    register_interrupt(0, contention_isr);
    lock();
    atomic_fetch_add(&active_owners, 1);

    for (unsigned tid = 2; tid <= 3; tid++) {
        create_waiting_thread(contention_waiter,
                              &stack[tid - 2][STACK_SIZE - 16], tid, &ops);
    }
    start_waiting_threads(WAITER_MASK);
    for (unsigned n = 0; n < SPIN_LIMIT &&
                         atomic_load(&waiters_ready) != 2; n++)
        pause();
    check32(atomic_load(&waiters_ready), 2);
    for (unsigned i = 0; i < CONTENTION_SWIS; i++) {
        swi(1);
        pause();
    }
    atomic_fetch_sub(&active_owners, 1);
    unlock();

    for (unsigned n = 0; pending && n < SPIN_LIMIT; n++) {
        unsigned acquired = atomic_load(&acquired_mask) & pending;

        for (unsigned tid = 2; tid <= 3; tid++) {
            unsigned bit = 1u << tid;

            if (!(acquired & bit))
                continue;
            /* The waiter holds the lock until our explicit release. */
            iassignw(0, ~bit);
            /*
             * The earlier SWIs may have left one interrupt pending (SWIs
             * coalesce).  Send two in sequence: the first absorbs any such
             * leftover, so the second interrupt is attributable to us.
             */
            for (int round = 0; round < 2; round++) {
                unsigned before = atomic_load(&contention_irqs[tid]);

                swi(1);
                check32(wait_irq_after(tid, before), 1);
            }
            atomic_fetch_or(&release_mask, bit);
            pending &= ~bit;
        }
        pause();
    }
    check32(pending, 0);
    /* Never leave a waiter spinning if the handshake failed. */
    atomic_fetch_or(&release_mask, WAITER_MASK);
    thread_join(WAITER_MASK);

    printf("  Interrupts: T2=%u T3=%u; overlapping owners: %u\n",
           atomic_load(&contention_irqs[2]),
           atomic_load(&contention_irqs[3]),
           atomic_load(&overlapping_owners));
    check32(atomic_load(&contention_irqs[0]), 0);
    check32(atomic_load(&contention_irqs[1]), 0);
    /* Each owner handled at least two interrupts after its acquisition. */
    check32_ne(atomic_load(&contention_irqs[2]) -
                   atomic_load(&acquire_irqs[2]) >= 2, 0);
    check32_ne(atomic_load(&contention_irqs[3]) -
                   atomic_load(&acquire_irqs[3]) >= 2, 0);
    check32(atomic_load(&overlapping_owners), 0);
    check32(atomic_load(&missing_lock_in_isr), 0);
    check32(atomic_load(&active_owners), 0);
    check32(get_syscfg() & contention_syscfg_mask, 0);
}

int main(int argc, char *argv[])
{
    /* Single-thread SYSCFG bit verification */
    test_k0lock_syscfg_bit();
    test_tlblock_syscfg_bit();
    test_locks_independent();

    /* Multi-thread mutual exclusion */
    test_k0lock_mutual_exclusion();
    test_tlblock_mutual_exclusion();

    /* Multi-thread mutual exclusion under interrupt pressure */
    test_k0lock_with_interrupts();
    test_tlblock_with_interrupts();

    /* Stalled waiters do not service interrupts */
    test_stalled_waiter_no_interrupt("k0lock", do_k0lock, do_k0unlock);
    test_stalled_waiter_no_interrupt("tlblock", do_tlblock, do_tlbunlock);

    /* Contended acquisition and confirmed-owner interrupt delivery */
    test_contended_lock_with_interrupts("k0lock", do_k0lock, do_k0unlock,
                                        SYSCFG_KL_BIT);
    test_contended_lock_with_interrupts("tlblock", do_tlblock, do_tlbunlock,
                                        SYSCFG_TL_BIT);

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
