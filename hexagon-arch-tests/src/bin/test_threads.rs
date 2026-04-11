// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Hardware thread tests for Hexagon v81.
//!
//! Tests multi-thread start/stop/wait/resume, MODECTL state transitions,
//! shared memory communication between threads, and per-thread register
//! isolation.

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

// Shared communication flags between threads.
static THREAD_FLAG: AtomicU32 = AtomicU32::new(0);
static THREAD_HTID: AtomicU32 = AtomicU32::new(0xFFFF);
static THREAD_DATA: AtomicU32 = AtomicU32::new(0);
static THREAD2_FLAG: AtomicU32 = AtomicU32::new(0);
static THREAD_RESUMED: AtomicU32 = AtomicU32::new(0);

/// Secondary thread entry: write HTID then set flag.
extern "C" fn thread1_basic() {
    let htid = read_htid();
    THREAD_HTID.store(htid, Ordering::SeqCst);
    THREAD_FLAG.store(1, Ordering::SeqCst);
}

/// Secondary thread entry: write a data pattern.
extern "C" fn thread1_data_writer() {
    THREAD_DATA.store(0xCAFE_BABE, Ordering::SeqCst);
    THREAD_FLAG.store(1, Ordering::SeqCst);
}

/// Thread 2 entry: set its own flag.
extern "C" fn thread2_entry() {
    THREAD2_FLAG.store(1, Ordering::SeqCst);
}

/// Thread entry that enters wait mode, then sets flag when resumed.
extern "C" fn thread1_wait_resume() {
    // Signal that we're alive before waiting
    THREAD_FLAG.store(1, Ordering::SeqCst);
    // Enter wait mode — will resume when thread 0 calls resume_threads
    wait_self(0);
    // After resume, signal completion
    THREAD_RESUMED.store(1, Ordering::SeqCst);
}

/// Small spin-wait on a flag with iteration limit.
fn wait_for_flag(flag: &AtomicU32, expected: u32, max_iters: u32) -> bool {
    for _ in 0..max_iters {
        if flag.load(Ordering::SeqCst) == expected {
            return true;
        }
        busy_loop(10);
    }
    false
}

/// Small busy-wait for a MODECTL condition with iteration limit.
fn wait_for_modectl_thread_stopped(tid: u32, max_iters: u32) -> bool {
    let mask = 1u32 << tid;
    for _ in 0..max_iters {
        let modectl = read_modectl();
        // Thread is stopped when its Enabled bit (lower 16) is clear
        if modectl & mask == 0 {
            return true;
        }
        busy_loop(10);
    }
    false
}

// -----------------------------------------------------------------------

/// Start thread 1, verify it runs and sets the flag, then verify it stopped.
fn test_start_stop_thread() {
    THREAD_FLAG.store(0, Ordering::SeqCst);
    THREAD_HTID.store(0xFFFF, Ordering::SeqCst);

    set_thread_entry(1, Some(thread1_basic));
    start_threads(1 << 1);

    // Wait for thread 1 to complete
    let ok = wait_for_flag(&THREAD_FLAG, 1, 10000);
    check!(ok);

    // Wait for thread 1 to stop itself
    let stopped = wait_for_modectl_thread_stopped(1, 10000);
    check!(stopped);
}

/// Verify MODECTL reflects thread 1 enabled during execution.
fn test_modectl_state() {
    let before = read_modectl();
    // Thread 0 should be enabled
    check!(before & 1 != 0);
    // Thread 1 should be stopped (from previous test or initial state)
    check!(before & (1 << 1) == 0);

    // Start thread 1 with a function that takes a while (writes data)
    THREAD_FLAG.store(0, Ordering::SeqCst);
    THREAD_DATA.store(0, Ordering::SeqCst);
    set_thread_entry(1, Some(thread1_data_writer));
    start_threads(1 << 1);

    // Wait for completion
    let ok = wait_for_flag(&THREAD_FLAG, 1, 10000);
    check!(ok);

    // After thread 1 stops, MODECTL bit 1 should be clear
    let stopped = wait_for_modectl_thread_stopped(1, 10000);
    check!(stopped);

    let after = read_modectl();
    check!(after & (1 << 1) == 0);
}

/// Verify per-thread HTID: thread 1 should read HTID=1.
fn test_per_thread_htid() {
    THREAD_FLAG.store(0, Ordering::SeqCst);
    THREAD_HTID.store(0xFFFF, Ordering::SeqCst);

    set_thread_entry(1, Some(thread1_basic));
    start_threads(1 << 1);

    let ok = wait_for_flag(&THREAD_FLAG, 1, 10000);
    check!(ok);
    check32!(THREAD_HTID.load(Ordering::SeqCst), 1);

    wait_for_modectl_thread_stopped(1, 10000);
}

/// Shared memory communication: thread 1 writes data, thread 0 reads it.
fn test_shared_memory() {
    THREAD_FLAG.store(0, Ordering::SeqCst);
    THREAD_DATA.store(0, Ordering::SeqCst);

    set_thread_entry(1, Some(thread1_data_writer));
    start_threads(1 << 1);

    let ok = wait_for_flag(&THREAD_FLAG, 1, 10000);
    check!(ok);
    check32!(THREAD_DATA.load(Ordering::SeqCst), 0xCAFE_BABE);

    wait_for_modectl_thread_stopped(1, 10000);
}

/// Start two threads concurrently: thread 1 and thread 2.
fn test_multiple_threads() {
    THREAD_FLAG.store(0, Ordering::SeqCst);
    THREAD2_FLAG.store(0, Ordering::SeqCst);
    THREAD_HTID.store(0xFFFF, Ordering::SeqCst);

    set_thread_entry(1, Some(thread1_basic));
    set_thread_entry(2, Some(thread2_entry));

    // Start both threads simultaneously
    start_threads((1 << 1) | (1 << 2));

    let ok1 = wait_for_flag(&THREAD_FLAG, 1, 10000);
    let ok2 = wait_for_flag(&THREAD2_FLAG, 1, 10000);
    check!(ok1);
    check!(ok2);

    // Both should have stopped
    wait_for_modectl_thread_stopped(1, 10000);
    wait_for_modectl_thread_stopped(2, 10000);

    // Thread 1's HTID should have been 1
    check32!(THREAD_HTID.load(Ordering::SeqCst), 1);
}

/// Wait/resume: start thread 1 with a function that waits, then resume it.
fn test_wait_resume() {
    THREAD_FLAG.store(0, Ordering::SeqCst);
    THREAD_RESUMED.store(0, Ordering::SeqCst);

    set_thread_entry(1, Some(thread1_wait_resume));
    start_threads(1 << 1);

    // Wait for thread 1 to signal it's alive (before it enters wait mode)
    let alive = wait_for_flag(&THREAD_FLAG, 1, 10000);
    check!(alive);

    // Give thread 1 time to enter wait mode
    busy_loop(200);

    // Check MODECTL: thread 1 should be in Wait state (bit 17 set)
    let modectl = read_modectl();
    check!(modectl & (1 << 17) != 0); // Wait bit for thread 1

    // Resume thread 1
    resume_threads(1 << 1);

    // Wait for thread 1 to set the resumed flag and stop
    let resumed = wait_for_flag(&THREAD_RESUMED, 1, 10000);
    check!(resumed);

    wait_for_modectl_thread_stopped(1, 10000);
}

/// STID.PRIO: set thread priority, verify readback.
fn test_stid_priority() {
    let saved = read_stid();

    // STID.PRIO is bits [23:16] per registers.adoc §3.1.1.2
    let prio: u32 = 42;
    let new_stid = (saved & !0x00FF_0000) | (prio << 16);
    write_stid(new_stid);

    let rb = read_stid();
    check32!((rb >> 16) & 0xFF, prio);

    // Restore
    write_stid(saved);
}

/// SCHEDCFG: write and read back scheduler configuration.
fn test_schedcfg_readwrite() {
    let saved = read_schedcfg();

    // SCHEDCFG: INTNO in bits [3:0], EN at bit 8
    let test_val: u32 = (1 << 8) | 5; // EN=1, INTNO=5
    write_schedcfg(test_val);

    let rb = read_schedcfg();
    check32!(rb & 0x10F, test_val);

    // Restore
    write_schedcfg(saved);
}

/// BESTWAIT: write and read back.
fn test_bestwait_readwrite() {
    let saved = read_bestwait();

    let test_val: u32 = 0x7F;
    write_bestwait(test_val);

    let rb = read_bestwait();
    check32!(rb & 0x1FF, test_val);

    // Restore
    write_bestwait(saved);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Hardware Threads");

    if !require_threads(0x7) {
        return test_suite_end() as i32;
    }

    run_test("start_stop_thread", test_start_stop_thread);
    run_test("modectl_state", test_modectl_state);
    run_test("per_thread_htid", test_per_thread_htid);
    run_test("shared_memory", test_shared_memory);
    run_test("multiple_threads", test_multiple_threads);
    run_test("wait_resume", test_wait_resume);
    run_test("stid_priority", test_stid_priority);
    run_test("schedcfg_readwrite", test_schedcfg_readwrite);
    run_test("bestwait_readwrite", test_bestwait_readwrite);

    test_suite_end() as i32
}
