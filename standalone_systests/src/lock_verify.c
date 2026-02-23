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
 *   - A thread stalled waiting to acquire k0lock does NOT service
 *     interrupts; the interrupt is steered to the running holder thread
 *   - Same for tlblock
 */

#include <stdio.h>
#include <string.h>
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
    asm volatile("k0lock\n");
}

static inline void do_k0unlock(void)
{
    asm volatile("k0unlock\n");
}

static inline void do_tlblock(void)
{
    asm volatile("tlblock\n");
}

static inline void do_tlbunlock(void)
{
    asm volatile("tlbunlock\n");
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

/* ===== Stalled thread does not service interrupts ===== */

/*
 * Per Section 6.1.3.2, a thread stalled on k0lock/tlblock is under a
 * "stall condition" and will not service interrupts.  The hardware
 * steers the interrupt to another qualified thread.
 *
 * Setup: thread 1 holds the lock with interrupts masked (imask).
 * Threads 2-3 are stalled waiting on the lock and are the ONLY
 * threads eligible for interrupt 0.
 *
 * On correct hardware the interrupt router recognises threads 2-3 as
 * stalled and does not deliver the interrupt.  The SWIs pend and are
 * eventually delivered after the threads un-stall and acquire the lock.
 *
 * If the emulator does not implement stall-based interrupt steering it
 * will wake the pended threads to service the ISR, then retry the lock
 * instruction.  This is observable: the ISR fires during stall_phase,
 * before the holder has released the lock.
 */

#define STALL_SWIS 200

static volatile int holder_ready;
static volatile int waiters_ready;
static volatile int swis_complete;
static volatile int stall_isr_htid[6];
static volatile int stall_phase;
static volatile int isr_while_pended;

static void stall_isr(int intno)
{
    int tid = get_htid();
    stall_isr_htid[tid]++;
    if (stall_phase)
        isr_while_pended++;
}

/* Thread 1: mask interrupts, acquire lock, signal ready, hold until done */
static void k0lock_holder(void *arg)
{
    set_thread_imask(0xffffffff);
    do_k0lock();
    holder_ready = 1;
    while (!swis_complete)
        delay(1);
    do_k0unlock();
}

/*
 * Threads 2-3: signal ready then attempt to acquire the lock.
 *
 * On real hardware the k0lock stalls until the holder releases.
 * After acquiring, the thread executes delay(1024) which gives time
 * for any legitimately-pending interrupts to drain.
 */
static void k0lock_waiter(void *arg)
{
    __sync_fetch_and_add(&waiters_ready, 1);
    do_k0lock();
    delay(1024);
    do_k0unlock();
}

static void tlblock_holder(void *arg)
{
    set_thread_imask(0xffffffff);
    do_tlblock();
    holder_ready = 1;
    while (!swis_complete)
        delay(1);
    do_tlbunlock();
}

static void tlblock_waiter(void *arg)
{
    __sync_fetch_and_add(&waiters_ready, 1);
    do_tlblock();
    delay(1024);
    do_tlbunlock();
}

static void stall_test_setup(void)
{
    holder_ready = 0;
    waiters_ready = 0;
    swis_complete = 0;
    stall_phase = 0;
    isr_while_pended = 0;
    memset((void *)stall_isr_htid, 0, sizeof(stall_isr_htid));

    register_interrupt(0, stall_isr);
    set_thread_imask(0xffffffff);   /* Mask all ints for thread 0 */
    iassignw(0, 1);          /* Exclude thread 0 from interrupt 0 */
    /*
     * Thread 1 (holder) masks its own interrupts via imask inside
     * its thread function.  This leaves only threads 2-3 (the pended
     * waiters) eligible for interrupt 0, forcing an emulator that
     * lacks stall-based steering to attempt delivery on pended
     * threads rather than silently routing to the holder.
     */
}

static void stall_test_fire_swis(void)
{
    /*
     * Wait for waiter threads to signal they are about to call
     * k0lock/tlblock.  After the atomic increment the very next
     * instruction is the lock acquisition, which will stall because
     * the holder already has it.  The delay() below gives ample
     * time for the 1-2 intervening instructions to retire,
     * guaranteeing the waiters are in the stall before the first
     * SWI fires.
     */
    while (waiters_ready < 2)
        delay(1);
    delay(200);

    /* Fire SWIs -- pended threads must not service them */
    stall_phase = 1;
    for (int i = 0; i < STALL_SWIS; i++) {
        swi(1);
        delay(1);
    }
    stall_phase = 0;

    swis_complete = 1;
}

static void stall_test_verify(const char *name)
{
    int waiter_total = stall_isr_htid[2] + stall_isr_htid[3];

    printf("  ISR by thread: T0=%d T1=%d T2=%d T3=%d\n",
           stall_isr_htid[0], stall_isr_htid[1],
           stall_isr_htid[2], stall_isr_htid[3]);
    printf("  ISR while pended on lock: %d\n", isr_while_pended);
    /* Thread 0: masked -- should not handle any */
    check32(stall_isr_htid[0], 0);
    /* Thread 1 (holder): masked via imask -- should not handle any */
    check32(stall_isr_htid[1], 0);
    /*
     * Self-validation: verify the waiter threads actually received
     * at least one interrupt after un-stalling and acquiring the lock.
     * If this is zero the SWIs were never processed and the test is
     * vacuous -- it would pass even if interrupt delivery were
     * completely broken.
     */
    check32_ne(waiter_total, 0);
    /*
     * The critical assertion: no ISR must fire on a pended thread
     * during the SWI phase.  On correct hardware, stalled threads
     * never service interrupts.  An emulator that wakes pended
     * threads for interrupt delivery will trip this check.
     */
    check32(isr_while_pended, 0);
}

/*
 * Thread stalled on k0lock does not service interrupts.
 */
static void test_k0lock_stall_no_interrupt(void)
{
    printf("stalled k0lock thread does not service interrupts\n");
    stall_test_setup();

    /* Start the holder -- it acquires k0lock and signals */
    create_waiting_thread(k0lock_holder,
                          &stack[0][STACK_SIZE - 16], 1, NULL);
    start_waiting_threads(1 << 1);
    while (!holder_ready)
        delay(1);

    /* Start waiters -- they stall on k0lock immediately */
    create_waiting_thread(k0lock_waiter,
                          &stack[1][STACK_SIZE - 16], 2, NULL);
    create_waiting_thread(k0lock_waiter,
                          &stack[2][STACK_SIZE - 16], 3, NULL);
    start_waiting_threads((1 << 2) | (1 << 3));

    stall_test_fire_swis();
    thread_join((1 << 1) | (1 << 2) | (1 << 3));
    stall_test_verify("k0lock");
}

/*
 * Thread stalled on tlblock does not service interrupts.
 */
static void test_tlblock_stall_no_interrupt(void)
{
    printf("stalled tlblock thread does not service interrupts\n");
    stall_test_setup();

    create_waiting_thread(tlblock_holder,
                          &stack[0][STACK_SIZE - 16], 1, NULL);
    start_waiting_threads(1 << 1);
    while (!holder_ready)
        delay(1);

    create_waiting_thread(tlblock_waiter,
                          &stack[1][STACK_SIZE - 16], 2, NULL);
    create_waiting_thread(tlblock_waiter,
                          &stack[2][STACK_SIZE - 16], 3, NULL);
    start_waiting_threads((1 << 2) | (1 << 3));

    stall_test_fire_swis();
    thread_join((1 << 1) | (1 << 2) | (1 << 3));
    stall_test_verify("tlblock");
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

    /* Stalled thread does not service interrupts */
    test_k0lock_stall_no_interrupt();
    test_tlblock_stall_no_interrupt();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
