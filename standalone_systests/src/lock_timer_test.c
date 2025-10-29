/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Lock Timer Test
 *
 * This test exercises both k0lock/k0unlock and tlblock/tlbunlock with
 * multiple threads while measuring timer values to verify exclusive access.
 * Each thread attempts to acquire the lock, reads the timer on entry and exit
 * of the critical section, and stores these values. After all rounds
 * complete, the test verifies that there is no overlap between
 * critical sections by checking timer intervals.
 *
 * The test runs in two phases:
 * 1. k0lock/k0unlock testing
 * 2. tlblock/tlbunlock testing
 */

#include "cfgtable.h"
#include "hexagon_standalone.h"
#include "interrupts.h"
#include "thread_common.h"
#include "util.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "timer.h"

#define N_THREADS 4
#define STACK_SIZE 16384
#define WORK_ITERATIONS 3000
#define MAX_INT_NUM 8
#define ALL_INTERRUPTS_MASK 0xff
#define ROUNDS_PER_THREAD 400

static char stack[N_THREADS][STACK_SIZE] __attribute__((__aligned__(8)));

typedef struct {
  uint64_t start_time;
  uint64_t end_time;
  int thread_id;
  int round;
} lock_record_t;

typedef struct {
  void (*lock_func)(void);
  void (*unlock_func)(void);
  const char *name;
} lock_ops_t;

static volatile int global_round_counter = 0;
static lock_record_t lock_records[N_THREADS * ROUNDS_PER_THREAD];
static volatile bool test_complete = false;
static volatile uint64_t work_data[N_THREADS][WORK_ITERATIONS]
    __attribute__((__aligned__(64)));
static lock_ops_t *current_lock_ops = NULL;

static atomic_int ints_by_irq[MAX_INT_NUM];
static atomic_int ints_by_tid[N_THREADS + 1];
static volatile int swi_issued_count = 0;

static inline void k0lock(void) { asm volatile("k0lock\n"); }
static inline void k0unlock(void) { asm volatile("k0unlock\n"); }
static inline void tlblock(void) { asm volatile("tlblock\n"); }
static inline void tlbunlock(void) { asm volatile("tlbunlock\n"); }

static void interrupt_handler(int intno) {
  uint32_t thread_id = get_htid();
  atomic_fetch_add(&ints_by_irq[intno], 1);
  atomic_fetch_add(&ints_by_tid[thread_id], 1);
}

static lock_ops_t k0lock_ops = {
    .lock_func = k0lock, .unlock_func = k0unlock, .name = "k0lock"};

static lock_ops_t tlblock_ops = {
    .lock_func = tlblock, .unlock_func = tlbunlock, .name = "tlblock"};

static void init_clock(void) {
  uint32_t subsystem_base = GET_SUBSYSTEM_BASE();
  printf("Using subsystem base: 0x%08lx\n", (unsigned long)subsystem_base);

  uint32_t qtmr_base = subsystem_base + 0x20000;
  volatile uint32_t *qtmr_ac_cntacr = (volatile uint32_t *)(qtmr_base + 0x40);
  volatile uint32_t *qtmr_cntp_ver = (volatile uint32_t *)(qtmr_base + 0x1fd0);
  volatile uint32_t *qtmr_cntp_tval = (volatile uint32_t *)(qtmr_base + 0x1028);
  volatile uint32_t *qtmr_cntp_ctl = (volatile uint32_t *)(qtmr_base + 0x102c);

  uint32_t cntp_ver = *qtmr_cntp_ver;
  printf("QTimer version: CNTP=0x%08lx\n", (unsigned long)cntp_ver);

  /* Expect CNTP version to be at least 0x20020000 */
  if (cntp_ver < 0x20020000) {
    fprintf(stderr,
            "ERROR: QTimer CNTP version 0x%08lx is less than expected "
            "0x20020000\n",
            (unsigned long)cntp_ver);
    abort();
  }

  *qtmr_ac_cntacr = 0x3f;
  *qtmr_cntp_tval = QTMR_FREQ / 1000;
  *qtmr_cntp_ctl = 1;
}

int work_values[1024];
static void do_work(void) {
  for (int i = 0; i < sizeof(work_values) / sizeof(work_values[0]); i++) {
    work_values[i] += i * get_htid();
    pause();
  }
}

static void thread_func(void *arg) {
  int thread_id = (int)(uintptr_t)arg;

  printf("Worker thread %d: STARTED\n", thread_id);

  while (!test_complete) {
    current_lock_ops->lock_func();

    int round = __sync_fetch_and_add(&global_round_counter, 1);
    if (round >= N_THREADS * ROUNDS_PER_THREAD) {
      current_lock_ops->unlock_func();
      break;
    }

    // Use QTimer counter for timing measurements
    uint64_t start_time = timer_read_pair();

    // Do work while holding the lock
    do_work();

    uint64_t end_time = timer_read_pair();
    uint64_t dur = end_time - start_time;
    if (dur == 0) {
      fprintf(stderr, "clock malfunction, zero tick duration\n");
      abort();
    }

    current_lock_ops->unlock_func();

    // Record timing data
    lock_records[round].start_time = start_time;
    lock_records[round].end_time = end_time;
    lock_records[round].thread_id = thread_id;
    lock_records[round].round = round;
  }
}

static int compare_records(const void *a, const void *b) {
  const lock_record_t *ra = (const lock_record_t *)a;
  const lock_record_t *rb = (const lock_record_t *)b;

  if (ra->start_time < rb->start_time)
    return -1;
  if (ra->start_time > rb->start_time)
    return 1;
  return 0;
}

static bool verify_no_overlaps(void) {
  int total_records = global_round_counter;

  // Sort records by start time
  qsort(lock_records, total_records, sizeof(lock_record_t), compare_records);

  printf("Verifying %d lock records for overlaps...\n", total_records);

  for (int i = 0; i < total_records - 1; i++) {
    lock_record_t *current = &lock_records[i];
    lock_record_t *next = &lock_records[i + 1];

    if (current->end_time > next->start_time) {
      printf("ERROR: Lock overlap detected!\n");
      printf("  Thread %d (round %d): %llu - %llu\n", current->thread_id,
             current->round, current->start_time, current->end_time);
      printf("  Thread %d (round %d): %llu - %llu\n", next->thread_id,
             next->round, next->start_time, next->end_time);
      printf("  Overlap: %llu cycles\n", current->end_time - next->start_time);
      return false;
    }
  }

  printf("SUCCESS: No lock overlaps detected in %d records\n", total_records);
  return true;
}

static void reset_test_state(void) {
  swi_issued_count = 0;
  global_round_counter = 0;
  test_complete = false;

  for (int i = 0; i < MAX_INT_NUM; i++) {
    atomic_store(&ints_by_irq[i], 0);
  }

  for (int i = 0; i <= N_THREADS; i++) {
    atomic_store(&ints_by_tid[i], 0);
  }

  // Clear lock records
  memset(lock_records, 0, sizeof(lock_records));
}

static int total_int_count(void) {
  // Calculate current total interrupt count
  int total = 0;
  for (int tid = 0; tid <= N_THREADS; tid++) {
    total += atomic_load(&ints_by_tid[tid]);
  }
  return total;
}

static void wait_for_int_count(int count) {
  while (total_int_count() < count) {
    do_work();
  }
}

static bool run_lock_test(lock_ops_t *lock_ops) {
  printf("\n=== Testing %s/unlock with interrupt assignment ===\n",
         lock_ops->name);
  printf("Threads: %d, Target lock operations: %d\n", N_THREADS,
         N_THREADS * ROUNDS_PER_THREAD);

  current_lock_ops = lock_ops;
  reset_test_state();

  for (int i = 0; i < MAX_INT_NUM; i++) {
    register_interrupt(i, interrupt_handler);
  }

  set_thread_imask(ALL_INTERRUPTS_MASK);

  printf("Creating %d threads...\n", N_THREADS);
  for (int i = 0; i < N_THREADS; i++) {
    create_waiting_thread(thread_func, &stack[i][STACK_SIZE], i + 1,
                          (void *)(uintptr_t)(i + 1));
  }

  printf("Starting threads...\n");
  uint32_t thread_start_mask = (1 << (N_THREADS + 1)) - 2;
  printf("Thread start mask: 0x%08lx\n", (unsigned long)thread_start_mask);

  start_waiting_threads(thread_start_mask);

  /* Configure interrupt assignment AFTER threads start: DISABLE interrupts for
   * main thread (bit 0 set)
   */
  uint32_t disable_main_mask =
      (1 << 0); /* 0x01 - disable for thread 0, enable for all others */
  for (int i = 0; i < MAX_INT_NUM; i++) {
    iassignw(i, disable_main_mask);
  }
  printf(
      "Configured interrupt assignment - disabled for main thread: 0x%08lx\n",
      (unsigned long)disable_main_mask);

  volatile uint64_t dummy_work = 0;

  // Issue SWIs while threads are working
  while (global_round_counter < ROUNDS_PER_THREAD) {
    swi(ALL_INTERRUPTS_MASK);
    swi_issued_count++;

    wait_for_int_count(swi_issued_count * MAX_INT_NUM);
  }

  // Signal threads to complete
  test_complete = true;

  uint32_t thread_mask = ((1 << (N_THREADS + 1)) - 2);
  thread_join(thread_mask);

  printf("All threads completed. Verifying interrupt results...\n");
  printf("Final global_round_counter: %d (target was %d)\n",
         global_round_counter, N_THREADS * ROUNDS_PER_THREAD);

  printf("Interrupt verification:\n");
  int total_expected_interrupts = swi_issued_count * MAX_INT_NUM;
  printf("SWI instructions issued: %d\n", swi_issued_count);
  printf("Expected total interrupts: %d\n", total_expected_interrupts);

  int total_ints_by_tid = 0;
  for (int i = 0; i <= N_THREADS; i++) {
    int count = atomic_load(&ints_by_tid[i]);
    printf("Thread %d interrupt count: %d\n", i, count);
    total_ints_by_tid += count;
  }

  int total_ints_by_irq = 0;
  for (int i = 0; i < MAX_INT_NUM; i++) {
    int count = atomic_load(&ints_by_irq[i]);
    printf("IRQ %d interrupt count: %d\n", i, count);
    total_ints_by_irq += count;
  }

  assert(total_ints_by_tid == total_expected_interrupts);

  // Verify main thread got no interrupts
  if (atomic_load(&ints_by_tid[0]) != 0) {
    printf("ERROR: Main thread (TID 0) should not have received interrupts, "
           "got %d\n",
           atomic_load(&ints_by_tid[0]));
    return false;
  }

  // Verify worker threads got interrupts
  for (int i = 1; i <= N_THREADS; i++) {
    if (atomic_load(&ints_by_tid[i]) == 0) {
      printf("ERROR: Worker thread %d should have received interrupts, got 0\n",
             i);
      return false;
    }
  }

  // Verify total interrupt counts are reasonable (greater than 10)
  if (total_ints_by_tid <= 10) {
    printf("ERROR: Total interrupts by TID (%d) should be greater than 10\n",
           total_ints_by_tid);
    return false;
  }

  if (total_ints_by_irq <= 10) {
    printf("ERROR: Total interrupts by IRQ (%d) should be greater than 10\n",
           total_ints_by_irq);
    return false;
  }

  if (total_ints_by_tid != total_ints_by_irq) {
    printf(
        "ERROR: Total interrupts by TID (%d) doesn't match total by IRQ (%d)\n",
        total_ints_by_tid, total_ints_by_irq);
    return false;
  }

  // Verify timing - this is the critical test for mutual exclusion
  if (!verify_no_overlaps()) {
    printf("FAIL - %s lock test failed timing verification\n", lock_ops->name);
    return false;
  }

  printf(
      "PASS - %s lock test with interrupt and timing verification successful\n",
      lock_ops->name);
  return true;
}

int main(int argc, char *argv[]) {
  printf("Testing lock primitives with timer measurements\n");

  uint32_t thread_enable_mask = read_cfgtable_field(0x48);
  printf("Hardware thread enable mask: 0x%08lx\n",
         (unsigned long)thread_enable_mask);

  uint32_t required_mask = (1 << N_THREADS) - 1;
  assert((thread_enable_mask & required_mask) == required_mask);
  printf("Verified %d hardware threads are available for test\n", N_THREADS);

  uint32_t subsystem_base = GET_SUBSYSTEM_BASE();
  add_translation((void *)subsystem_base, (void *)subsystem_base, 4);

  init_clock();

  bool k0lock_passed = run_lock_test(&k0lock_ops);
  bool tlblock_passed = run_lock_test(&tlblock_ops);

  printf("k0lock test: %s\n", k0lock_passed ? "PASS" : "FAIL");
  printf("tlblock test: %s\n", tlblock_passed ? "PASS" : "FAIL");

  if (k0lock_passed && tlblock_passed) {
    printf("OVERALL: PASS - Both lock primitives provide proper mutual "
           "exclusion\n");
    return 0;
  } else {
    printf("OVERALL: FAIL - One or more lock primitives failed\n");
    return 1;
  }
}
