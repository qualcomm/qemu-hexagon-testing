// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Interrupt stress tests for Hexagon v81.
//!
//! Exercises multi-thread interrupt delivery, rapid interrupt cycling,
//! simultaneous SWI delivery, nested interrupt triggering from handlers,
//! and interrupt/thread lifecycle interactions.  The goal is to stress
//! QEMU's interrupt and threading subsystems in patterns similar to a
//! real RTOS kernel workload.

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

// Per-interrupt handler invocation counters (INT0..INT15).
static INT_COUNT: [AtomicU32; 16] = [
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
];

// Shared state for cross-thread coordination.
static THREAD1_READY: AtomicU32 = AtomicU32::new(0);
static THREAD1_DONE: AtomicU32 = AtomicU32::new(0);

// Shared memory written by interrupt handlers for verification.
static HANDLER_DATA: [AtomicU32; 8] = [
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
];

// -----------------------------------------------------------------------
// Interrupt handlers
// -----------------------------------------------------------------------

/// Generic counting handler: increments INT_COUNT[intno].
extern "C" fn counting_handler(intno: u32) {
    if (intno as usize) < INT_COUNT.len() {
        INT_COUNT[intno as usize].fetch_add(1, Ordering::SeqCst);
    }
}

/// Cascade handler: counts itself, then triggers INT3.
/// Since IE is disabled during handler execution (crt0.S clears SSR.IE),
/// INT3 will be delivered after this handler returns and IE is restored.
extern "C" fn cascade_handler(intno: u32) {
    INT_COUNT[intno as usize].fetch_add(1, Ordering::SeqCst);
    trigger_swi(1 << 3);
}

/// Data writer handler: counts and writes a unique pattern to shared memory.
extern "C" fn data_writer_handler(intno: u32) {
    INT_COUNT[intno as usize].fetch_add(1, Ordering::SeqCst);
    if (intno as usize) < HANDLER_DATA.len() {
        HANDLER_DATA[intno as usize].store(0xDEAD_0000 | intno, Ordering::SeqCst);
    }
}

// -----------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------

fn reset_counts() {
    for c in INT_COUNT.iter() {
        c.store(0, Ordering::SeqCst);
    }
}

fn reset_handler_data() {
    for d in HANDLER_DATA.iter() {
        d.store(0, Ordering::SeqCst);
    }
}

fn clear_all_swi() {
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    clear_swi(0xFFFF_FFFF);
    ciad(0xFFFF_FFFF);
    write_ssr(ssr);
}

fn wait_for_flag(flag: &AtomicU32, expected: u32, max_iters: u32) -> bool {
    for _ in 0..max_iters {
        if flag.load(Ordering::SeqCst) == expected {
            return true;
        }
        busy_loop(10);
    }
    false
}

fn wait_for_modectl_thread_stopped(tid: u32, max_iters: u32) -> bool {
    let mask = 1u32 << tid;
    for _ in 0..max_iters {
        if read_modectl() & mask == 0 {
            return true;
        }
        busy_loop(10);
    }
    false
}

// -----------------------------------------------------------------------
// Tests: single-thread interrupt stress
// -----------------------------------------------------------------------

/// Fire 6 different interrupts in rapid succession on thread 0.
/// Exercises the interrupt dispatch path under burst load.
fn test_multi_int_burst() {
    clear_all_swi();
    reset_counts();

    for i in 2..=7u32 {
        register_interrupt(i, counting_handler);
    }

    // Rapid-fire 6 SWIs (separate instructions to stress per-interrupt dispatch)
    trigger_swi(1 << 2);
    trigger_swi(1 << 3);
    trigger_swi(1 << 4);
    trigger_swi(1 << 5);
    trigger_swi(1 << 6);
    trigger_swi(1 << 7);
    busy_loop(200);

    for i in 2..=7u32 {
        check!(INT_COUNT[i as usize].load(Ordering::SeqCst) >= 1);
    }
    clear_all_swi();
}

/// Trigger multiple SWI bits in a single instruction.
/// Hardware delivers them LSB-first; verify all fire.
fn test_simultaneous_swi_bits() {
    clear_all_swi();
    reset_counts();

    register_interrupt(2, counting_handler);
    register_interrupt(3, counting_handler);
    register_interrupt(4, counting_handler);

    // One SWI instruction with three bits set simultaneously
    trigger_swi((1 << 2) | (1 << 3) | (1 << 4));
    busy_loop(200);

    check!(INT_COUNT[2].load(Ordering::SeqCst) >= 1);
    check!(INT_COUNT[3].load(Ordering::SeqCst) >= 1);
    check!(INT_COUNT[4].load(Ordering::SeqCst) >= 1);
    clear_all_swi();
}

/// Rapidly fire the same interrupt 20 times with proper clear/ciad cycle.
/// Stresses the interrupt delivery pipeline under repeated re-triggering,
/// similar to an RTOS raising the same interrupt multiple times.
fn test_rapid_refire_cycle() {
    clear_all_swi();
    reset_counts();
    register_interrupt(2, counting_handler);

    let iterations = 20u32;
    for _ in 0..iterations {
        trigger_swi(1 << 2);
        busy_loop(50);

        // Ensure clean state between iterations: disable IE, clear SWI
        // pending bit, clear IAD, then re-enable.
        let ssr = read_ssr();
        write_ssr(ssr & !SSR_IE);
        clear_swi(1 << 2);
        ciad(1 << 2);
        write_ssr(ssr | SSR_IE);
    }

    check!(INT_COUNT[2].load(Ordering::SeqCst) >= iterations);
    clear_all_swi();
}

/// Handler for INT2 triggers SWI for INT3 (cascade).
/// INT3 fires after INT2's handler returns and IE is restored.
/// Exercises the cascading interrupt delivery path.
fn test_nested_swi_from_handler() {
    clear_all_swi();
    reset_counts();

    register_interrupt(2, cascade_handler);
    register_interrupt(3, counting_handler);

    trigger_swi(1 << 2);
    busy_loop(200);

    check!(INT_COUNT[2].load(Ordering::SeqCst) >= 1);
    check!(INT_COUNT[3].load(Ordering::SeqCst) >= 1);
    clear_all_swi();
}

/// Disable IE, accumulate 4 pending SWIs, then re-enable.
/// All 4 should fire in rapid succession once IE is restored.
/// Exercises delivery of multiple pending interrupts becoming ready simultaneously.
fn test_delayed_delivery_burst() {
    clear_all_swi();
    reset_counts();

    for i in 2..=5u32 {
        register_interrupt(i, counting_handler);
    }

    // Disable interrupts globally
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);

    // Accumulate 4 pending SWIs while delivery is disabled
    trigger_swi((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
    busy_loop(20);

    // Verify none delivered yet
    for i in 2..=5u32 {
        check32!(INT_COUNT[i as usize].load(Ordering::SeqCst), 0);
    }

    // Re-enable — all 4 should fire in rapid succession
    write_ssr(ssr | SSR_IE);
    busy_loop(200);

    for i in 2..=5u32 {
        check!(INT_COUNT[i as usize].load(Ordering::SeqCst) >= 1);
    }
    clear_all_swi();
}

/// Four interrupt handlers each write unique data patterns to shared memory.
/// Verifies handler-written data is visible to the main thread after delivery.
/// Exercises handler-to-main-thread data visibility.
fn test_handler_shared_memory() {
    clear_all_swi();
    reset_counts();
    reset_handler_data();

    for i in 2..=5u32 {
        register_interrupt(i, data_writer_handler);
    }

    trigger_swi((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
    busy_loop(200);

    for i in 2..=5u32 {
        check32!(
            HANDLER_DATA[i as usize].load(Ordering::SeqCst),
            0xDEAD_0000 | i
        );
    }
    clear_all_swi();
}

// -----------------------------------------------------------------------
// Tests: multi-thread interrupt stress
//
// These tests run secondary threads concurrently with thread 0's interrupt
// handling. The secondary threads create scheduling/timing pressure on the
// emulator (like concurrent RTOS threads), while thread 0 handles all
// interrupts.
// -----------------------------------------------------------------------

/// Thread entry: busy worker that hammers shared memory.
extern "C" fn thread_busy_worker() {
    THREAD1_READY.store(1, Ordering::SeqCst);

    // Busy work: repeatedly write to shared memory (creates memory traffic
    // that interleaves with thread 0's interrupt handler stack saves/restores)
    for i in 0..1000u32 {
        HANDLER_DATA[0].store(i, Ordering::SeqCst);
        busy_loop(5);
    }

    THREAD1_DONE.store(1, Ordering::SeqCst);
}

/// Thread entry: signals ready, then waits for completion signal.
/// Stays alive while thread 0 does interrupt work.
extern "C" fn thread_spin_worker() {
    THREAD1_READY.store(1, Ordering::SeqCst);

    // Spin until told to stop (exercises thread scheduling while thread 0
    // handles interrupts)
    while THREAD1_DONE.load(Ordering::SeqCst) == 0 {
        busy_loop(10);
    }
}

/// Thread entry: enters wait mode, resumes when signaled.
extern "C" fn thread_wait_worker() {
    THREAD1_READY.store(1, Ordering::SeqCst);
    // Enter wait mode — wakes on resume_threads()
    wait_self(0);
    THREAD1_DONE.store(1, Ordering::SeqCst);
}

/// Fire a burst of interrupts while a concurrent thread hammers shared memory.
/// Stresses the emulator's ability to interleave thread execution with
/// interrupt handler register saves/restores — similar to concurrent RTOS
/// threads with ISR activity.
fn test_int_burst_with_busy_thread() {
    clear_all_swi();
    reset_counts();
    reset_handler_data();
    THREAD1_READY.store(0, Ordering::SeqCst);
    THREAD1_DONE.store(0, Ordering::SeqCst);

    for i in 2..=7u32 {
        register_interrupt(i, counting_handler);
    }

    set_thread_entry(1, Some(thread_busy_worker));
    start_threads(1 << 1);

    let ready = wait_for_flag(&THREAD1_READY, 1, 50000);
    check!(ready);

    // Fire 6 interrupts while thread 1 is actively writing shared memory
    trigger_swi(1 << 2);
    trigger_swi(1 << 3);
    trigger_swi(1 << 4);
    trigger_swi(1 << 5);
    trigger_swi(1 << 6);
    trigger_swi(1 << 7);
    busy_loop(200);

    for i in 2..=7u32 {
        check!(INT_COUNT[i as usize].load(Ordering::SeqCst) >= 1);
    }

    // Wait for thread 1 to finish its work
    let done = wait_for_flag(&THREAD1_DONE, 1, 50000);
    check!(done);
    wait_for_modectl_thread_stopped(1, 10000);
    check32!(HANDLER_DATA[0].load(Ordering::SeqCst), 999);
    clear_all_swi();
}

/// Rapidly refire interrupts on thread 0 while a concurrent thread spins.
/// The spinning thread creates constant scheduling pressure, exercising
/// the emulator's thread interleaving during repeated interrupt delivery.
fn test_rapid_refire_with_spinning_thread() {
    clear_all_swi();
    reset_counts();
    THREAD1_READY.store(0, Ordering::SeqCst);
    THREAD1_DONE.store(0, Ordering::SeqCst);

    register_interrupt(2, counting_handler);

    set_thread_entry(1, Some(thread_spin_worker));
    start_threads(1 << 1);

    let ready = wait_for_flag(&THREAD1_READY, 1, 50000);
    check!(ready);

    // Hammer interrupts on thread 0 while thread 1 spins
    let iterations = 10u32;
    for _ in 0..iterations {
        trigger_swi(1 << 2);
        busy_loop(50);

        let ssr = read_ssr();
        write_ssr(ssr & !SSR_IE);
        clear_swi(1 << 2);
        ciad(1 << 2);
        write_ssr(ssr | SSR_IE);
    }

    check!(INT_COUNT[2].load(Ordering::SeqCst) >= iterations);

    // Tell thread 1 to stop
    THREAD1_DONE.store(1, Ordering::SeqCst);
    wait_for_modectl_thread_stopped(1, 10000);
    clear_all_swi();
}

/// Fire an interrupt on thread 0 immediately after starting thread 1.
/// Stresses the thread startup / interrupt delivery race path in QEMU.
fn test_interrupt_during_thread_start() {
    clear_all_swi();
    reset_counts();
    THREAD1_READY.store(0, Ordering::SeqCst);
    THREAD1_DONE.store(0, Ordering::SeqCst);

    register_interrupt(2, counting_handler);

    set_thread_entry(1, Some(thread_busy_worker));
    start_threads(1 << 1);

    // Immediately fire SWI on thread 0 — races with thread 1's startup
    trigger_swi(1 << 2);
    busy_loop(100);

    check!(INT_COUNT[2].load(Ordering::SeqCst) >= 1);

    let done = wait_for_flag(&THREAD1_DONE, 1, 50000);
    check!(done);
    wait_for_modectl_thread_stopped(1, 10000);
    clear_all_swi();
}

/// Interleave thread wait/resume with interrupt handling.
/// Thread 1 enters wait mode; thread 0 handles interrupts, then resumes
/// thread 1. Stresses the interaction between thread state transitions
/// (wait/resume) and interrupt delivery.
/// Exercises the wait/resume + interrupt interaction path.
fn test_int_with_thread_wait_resume() {
    clear_all_swi();
    reset_counts();
    THREAD1_READY.store(0, Ordering::SeqCst);
    THREAD1_DONE.store(0, Ordering::SeqCst);

    register_interrupt(2, counting_handler);
    register_interrupt(3, counting_handler);

    set_thread_entry(1, Some(thread_wait_worker));
    start_threads(1 << 1);

    // Wait for thread 1 to signal ready before it enters wait mode
    let ready = wait_for_flag(&THREAD1_READY, 1, 50000);
    check!(ready);

    // Give thread 1 time to enter wait mode
    busy_loop(200);

    // Fire interrupts while thread 1 is in wait mode
    trigger_swi(1 << 2);
    busy_loop(100);
    check!(INT_COUNT[2].load(Ordering::SeqCst) >= 1);
    clear_all_swi();

    trigger_swi(1 << 3);
    busy_loop(100);
    check!(INT_COUNT[3].load(Ordering::SeqCst) >= 1);
    clear_all_swi();

    // Resume thread 1 — interrupt handling + thread resume is a common
    // QEMU stress point
    resume_threads(1 << 1);

    let done = wait_for_flag(&THREAD1_DONE, 1, 50000);
    check!(done);
    wait_for_modectl_thread_stopped(1, 10000);
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Interrupt Stress (ISR)");

    if !require_threads(0x3) {
        return test_suite_end() as i32;
    }

    // Single-thread interrupt stress
    run_test("multi_int_burst", test_multi_int_burst);
    run_test("simultaneous_swi_bits", test_simultaneous_swi_bits);
    run_test("rapid_refire_cycle", test_rapid_refire_cycle);
    run_test("nested_swi_from_handler", test_nested_swi_from_handler);
    run_test("delayed_delivery_burst", test_delayed_delivery_burst);
    run_test("handler_shared_memory", test_handler_shared_memory);

    // Multi-thread interrupt stress
    run_test(
        "int_burst_with_busy_thread",
        test_int_burst_with_busy_thread,
    );
    run_test(
        "rapid_refire_with_spinning_thread",
        test_rapid_refire_with_spinning_thread,
    );
    run_test(
        "interrupt_during_thread_start",
        test_interrupt_during_thread_start,
    );
    run_test(
        "int_with_thread_wait_resume",
        test_int_with_thread_wait_resume,
    );

    test_suite_end() as i32
}
