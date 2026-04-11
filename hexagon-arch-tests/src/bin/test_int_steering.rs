// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Interrupt steering tests for Hexagon v81.
//!
//! Tests priority-based thread qualification via SCHEDCFG[8] and STID.PRIO,
//! IMASK-based routing via iassignw, and interactions between priority
//! steering and thread qualification (IE, IMASK).
//!
//! Cross-thread interrupt delivery uses the L2VIC (SOFT_INT → L1 INT#2)
//! since SWI only sets pending on the calling thread. A fully functional
//! L2VIC is required — missing or non-functional L2VIC is a fatal error.
//!
//! Uses 3 threads (T0, T1, T2). L2VIC group 0 delivers via L1 INT#2.

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

// L2VIC configuration
const L2VIC_TLB_IDX: u32 = 2;
const L2VIC_L1_INTNO: u32 = 2; // L2VIC group 0 → L1 INT#2
const TEST_L2_IRQ: u32 = 3; // Arbitrary L2 IRQ number for triggering

// SWI-based test uses a different INT# to avoid L2VIC conflicts
const SWI_INTNO: u32 = 5;

// Handler records which thread handled the interrupt
static HANDLER_HTID: AtomicU32 = AtomicU32::new(0xFFFF);
static HANDLER_COUNT: AtomicU32 = AtomicU32::new(0);

// Per-thread synchronization
static T1_RUNNING: AtomicU32 = AtomicU32::new(0);
static T1_EXIT: AtomicU32 = AtomicU32::new(0);
static T2_RUNNING: AtomicU32 = AtomicU32::new(0);
static T2_EXIT: AtomicU32 = AtomicU32::new(0);

// Per-thread desired priority
static T1_DESIRED_PRIO: AtomicU32 = AtomicU32::new(0);
static T2_DESIRED_PRIO: AtomicU32 = AtomicU32::new(0);

// Flag: if set, thread 1 keeps SSR.IE=0 (for IE qualification test)
static T1_CLEAR_IE: AtomicU32 = AtomicU32::new(0);

// L2VIC state
static mut L2VIC_VA: u32 = 0;

// -----------------------------------------------------------------------
// Handlers
// -----------------------------------------------------------------------

/// SWI handler: records HTID.
extern "C" fn swi_handler(_intno: u32) {
    HANDLER_HTID.store(read_htid(), Ordering::SeqCst);
    HANDLER_COUNT.fetch_add(1, Ordering::SeqCst);
}

/// L2VIC handler for L1 INT#2: records HTID, clears L2VIC interrupt.
extern "C" fn l2vic_handler(_intno: u32) {
    HANDLER_HTID.store(read_htid(), Ordering::SeqCst);
    HANDLER_COUNT.fetch_add(1, Ordering::SeqCst);

    let vid = read_vid();
    let l2_irq = vid & 0x3FF;
    let slice = l2_irq / 32;
    let bit = l2_irq % 32;
    let base = unsafe { L2VIC_VA };
    if base != 0 {
        l2vic_write(base, L2VIC_INT_CLEAR + 4 * slice, 1 << bit);
    }
}

// -----------------------------------------------------------------------
// Thread entries — secondary threads enable IE and clear IMASK
// -----------------------------------------------------------------------

extern "C" fn thread1_entry() {
    let prio = T1_DESIRED_PRIO.load(Ordering::SeqCst);
    let stid = read_stid();
    write_stid((stid & !STID_PRIO_MASK) | (prio << STID_PRIO_SHIFT));

    // Unmask all interrupts on this thread
    write_imask(0);

    // Enable IE unless the test specifically wants it disabled
    if T1_CLEAR_IE.load(Ordering::SeqCst) == 0 {
        let ssr = read_ssr();
        write_ssr(ssr | SSR_IE);
    }

    T1_RUNNING.store(1, Ordering::SeqCst);

    while T1_EXIT.load(Ordering::SeqCst) == 0 {
        busy_loop(10);
    }
}

extern "C" fn thread2_entry() {
    let prio = T2_DESIRED_PRIO.load(Ordering::SeqCst);
    let stid = read_stid();
    write_stid((stid & !STID_PRIO_MASK) | (prio << STID_PRIO_SHIFT));

    write_imask(0);
    let ssr = read_ssr();
    write_ssr(ssr | SSR_IE);

    T2_RUNNING.store(1, Ordering::SeqCst);

    while T2_EXIT.load(Ordering::SeqCst) == 0 {
        busy_loop(10);
    }
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

fn wait_for(flag: &AtomicU32, expected: u32, max_iters: u32) -> bool {
    for _ in 0..max_iters {
        if flag.load(Ordering::SeqCst) == expected {
            return true;
        }
        busy_loop(10);
    }
    false
}

fn wait_thread_stopped(tid: u32, max_iters: u32) -> bool {
    let mask = 1u32 << tid;
    for _ in 0..max_iters {
        if read_modectl() & mask == 0 {
            return true;
        }
        busy_loop(10);
    }
    false
}

fn setup_threads(prio1: u32, prio2: u32) {
    T1_RUNNING.store(0, Ordering::SeqCst);
    T1_EXIT.store(0, Ordering::SeqCst);
    T2_RUNNING.store(0, Ordering::SeqCst);
    T2_EXIT.store(0, Ordering::SeqCst);
    T1_DESIRED_PRIO.store(prio1, Ordering::SeqCst);
    T2_DESIRED_PRIO.store(prio2, Ordering::SeqCst);

    set_thread_entry(1, Some(thread1_entry));
    set_thread_entry(2, Some(thread2_entry));
    start_threads((1 << 1) | (1 << 2));

    wait_for(&T1_RUNNING, 1, 50000);
    wait_for(&T2_RUNNING, 1, 50000);
    // Let threads fully settle (STID, IMASK, IE all written)
    busy_loop(200);
}

fn teardown_threads() {
    T1_EXIT.store(1, Ordering::SeqCst);
    T2_EXIT.store(1, Ordering::SeqCst);
    wait_thread_stopped(1, 50000);
    wait_thread_stopped(2, 50000);
}

fn reset_handler_state() {
    HANDLER_HTID.store(0xFFFF, Ordering::SeqCst);
    HANDLER_COUNT.store(0, Ordering::SeqCst);
}

/// Clear L2VIC + L1 INT#2 state between tests.
fn clear_l2vic_state() {
    let base = unsafe { L2VIC_VA };
    if base != 0 {
        let slice = TEST_L2_IRQ / 32;
        let bit = TEST_L2_IRQ % 32;
        l2vic_write(base, L2VIC_INT_CLEAR + 4 * slice, 1 << bit);
    }
    // Temporarily disable IE while clearing SWI and IAD state
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    clear_swi(1 << L2VIC_L1_INTNO);
    ciad(1 << L2VIC_L1_INTNO);
    // Re-enable IE explicitly (don't rely on saved SSR which may have IE=0)
    write_ssr(ssr | SSR_IE);
}

/// Trigger L2VIC SOFT_INT for TEST_L2_IRQ and wait for delivery.
fn trigger_l2vic_and_wait() {
    let base = unsafe { L2VIC_VA };
    let slice = TEST_L2_IRQ / 32;
    let bit = TEST_L2_IRQ % 32;

    // Re-enable the L2VIC interrupt before each trigger. The l2vic.so cosim
    // clears INT_ENABLE when INT_CLEAR is written for edge-triggered IRQs,
    // so we must re-arm it before each use.
    l2vic_write(base, L2VIC_INT_ENABLE_SET + 4 * slice, 1 << bit);
    busy_loop(10);

    l2vic_write(base, L2VIC_SOFT_INT + 4 * slice, 1 << bit);
    // Wait for handler, checking periodically for cross-thread delivery
    for _ in 0..200 {
        if HANDLER_COUNT.load(Ordering::SeqCst) > 0 {
            break;
        }
        busy_loop(50);
    }
}

// -----------------------------------------------------------------------
// L2VIC setup for steering tests
// -----------------------------------------------------------------------

/// Set up L2VIC for cross-thread interrupt steering tests.
/// Panics if L2VIC cannot be discovered or is non-functional.
fn setup_l2vic() {
    let subsys_raw = read_cfgtable_field(CFGTABLE_SUBSYSTEM_BASE);
    if subsys_raw == 0 {
        panic!("FATAL: subsystem_base is 0 — cannot discover L2VIC");
    }
    let subsys_base = subsys_raw << 16;
    let l2vic_base = subsys_base + 0x0001_0000;
    let vpn_1m = l2vic_base >> 20;
    install_device_mapping(vpn_1m, vpn_1m, L2VIC_TLB_IDX);
    unsafe {
        L2VIC_VA = l2vic_base;
    }

    // Probe: check SET and CLR both work
    l2vic_write(l2vic_base, L2VIC_INT_ENABLE_CLR, 1 << 0);
    busy_loop(10);
    l2vic_write(l2vic_base, L2VIC_INT_ENABLE_SET, 1 << 0);
    busy_loop(10);
    let en_set = l2vic_read(l2vic_base, L2VIC_INT_ENABLE);
    l2vic_write(l2vic_base, L2VIC_INT_ENABLE_CLR, 1 << 0);
    busy_loop(10);
    let en_clr = l2vic_read(l2vic_base, L2VIC_INT_ENABLE);
    if (en_set & 1 == 0) || (en_clr & 1 != 0) {
        panic!(
            "FATAL: L2VIC probe failed at 0x{:08x}: \
                enable after SET=0x{:x}, after CLR=0x{:x}",
            l2vic_base, en_set, en_clr
        );
    }

    // Configure TEST_L2_IRQ: edge type, enable
    let slice = TEST_L2_IRQ / 32;
    let bit = TEST_L2_IRQ % 32;
    let type_reg = l2vic_read(l2vic_base, L2VIC_INT_TYPE + 4 * slice);
    l2vic_write(
        l2vic_base,
        L2VIC_INT_TYPE + 4 * slice,
        type_reg | (1 << bit),
    );
    l2vic_write(l2vic_base, L2VIC_INT_ENABLE_SET + 4 * slice, 1 << bit);

    // Register handler for L1 INT#2
    register_interrupt(L2VIC_L1_INTNO, l2vic_handler);
}

fn cleanup_l2vic() {
    let base = unsafe { L2VIC_VA };
    if base != 0 {
        let slice = TEST_L2_IRQ / 32;
        let bit = TEST_L2_IRQ % 32;
        l2vic_write(base, L2VIC_INT_ENABLE_CLR + 4 * slice, 1 << bit);
    }
    tlb_invalidate(L2VIC_TLB_IDX);
    unsafe {
        L2VIC_VA = 0;
    }
}

// -----------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------

/// Test 1: Steering disabled (SWI-based, always runs).
/// SCHEDCFG[8]=0, SWI fires on T0 normally.
fn test_steering_disabled() {
    let saved_schedcfg = read_schedcfg();
    let saved_imask = read_imask();

    write_schedcfg(saved_schedcfg & !SCHEDCFG_EN);

    reset_handler_state();
    register_interrupt(SWI_INTNO, swi_handler);
    write_imask(saved_imask & !(1 << SWI_INTNO));

    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    clear_swi(1 << SWI_INTNO);
    ciad(1 << SWI_INTNO);
    write_ssr(ssr | SSR_IE);

    trigger_swi(1 << SWI_INTNO);
    busy_loop(200);

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    let htid = HANDLER_HTID.load(Ordering::SeqCst);
    check32!(htid, 0); // SWI always fires on calling thread

    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    let ssr2 = read_ssr();
    write_ssr(ssr2 & !SSR_IE);
    clear_swi(1 << SWI_INTNO);
    ciad(1 << SWI_INTNO);
    write_ssr(ssr2);
}

/// Test 2: iassignw routes L1 INT#2 to T1 only.
fn test_iassign_to_thread1() {
    let saved_schedcfg = read_schedcfg();
    let saved_imask = read_imask();

    write_schedcfg(saved_schedcfg & !SCHEDCFG_EN);

    reset_handler_state();
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    setup_threads(100, 100);

    // Mask T0+T2, unmask T1 only (IMASK bit=1 means masked)
    iassignw((L2VIC_L1_INTNO << 16) | 0x5);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 1);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 3: iassignw routes L1 INT#2 to T0 only.
fn test_iassign_to_thread0() {
    let saved_schedcfg = read_schedcfg();
    let saved_imask = read_imask();

    write_schedcfg(saved_schedcfg & !SCHEDCFG_EN);

    reset_handler_state();
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    setup_threads(100, 100);

    // Mask T1+T2, unmask T0 only
    iassignw((L2VIC_L1_INTNO << 16) | 0x6);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 0);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 4: Priority-based steering. T1 has best (highest) priority.
/// The hardware scheduler selects the thread with the highest STID.PRIO value
/// as the "best" candidate for interrupt delivery.
fn test_steering_priority_basic() {
    let saved_schedcfg = read_schedcfg();
    let saved_stid = read_stid();
    let saved_imask = read_imask();

    // Enable priority steering for INT#2
    write_schedcfg((saved_schedcfg & !0xF) | SCHEDCFG_EN | L2VIC_L1_INTNO);

    // T0 gets lowest priority
    write_stid((saved_stid & !STID_PRIO_MASK) | (10 << STID_PRIO_SHIFT));
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    reset_handler_state();

    // T1=200 (best/highest), T2=100
    setup_threads(200, 100);
    iassignw((L2VIC_L1_INTNO << 16) | 0x0);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 1);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_stid(saved_stid);
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 5: Priority reversed — T0 has best (highest) priority.
fn test_steering_priority_reversed() {
    let saved_schedcfg = read_schedcfg();
    let saved_stid = read_stid();
    let saved_imask = read_imask();

    write_schedcfg((saved_schedcfg & !0xF) | SCHEDCFG_EN | L2VIC_L1_INTNO);
    write_stid((saved_stid & !STID_PRIO_MASK) | (200 << STID_PRIO_SHIFT));
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    reset_handler_state();

    // T1=10 (lowest), T2=100
    setup_threads(10, 100);
    iassignw((L2VIC_L1_INTNO << 16) | 0x0);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 0);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_stid(saved_stid);
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 6: Three-thread priority ordering — T2 has best (highest) priority.
fn test_steering_three_thread_ordering() {
    let saved_schedcfg = read_schedcfg();
    let saved_stid = read_stid();
    let saved_imask = read_imask();

    write_schedcfg((saved_schedcfg & !0xF) | SCHEDCFG_EN | L2VIC_L1_INTNO);
    write_stid((saved_stid & !STID_PRIO_MASK) | (50 << STID_PRIO_SHIFT));
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    reset_handler_state();

    // T1=100, T2=200 (best/highest)
    setup_threads(100, 200);
    iassignw((L2VIC_L1_INTNO << 16) | 0x0);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 2);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_stid(saved_stid);
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 7: iassignw overrides priority — T0 has best (highest) priority but is
/// excluded by iassignw along with T1. T2 should handle the interrupt despite
/// having the lowest priority, because iassign routing takes precedence.
fn test_steering_imask_overrides_priority() {
    let saved_schedcfg = read_schedcfg();
    let saved_stid = read_stid();
    let saved_imask = read_imask();

    write_schedcfg((saved_schedcfg & !0xF) | SCHEDCFG_EN | L2VIC_L1_INTNO);
    write_stid((saved_stid & !STID_PRIO_MASK) | (200 << STID_PRIO_SHIFT));
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    reset_handler_state();

    // T1=100 (middle prio), T2=10 (lowest prio)
    setup_threads(100, 10);

    // Mask T0+T1 (bit0+bit1), unmask only T2 — forces delivery to T2
    iassignw((L2VIC_L1_INTNO << 16) | 0x3);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 2);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_stid(saved_stid);
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 8: IE qualification — T1 has best (highest) priority but SSR.IE=0.
/// T2 (next best priority, IE=1) should handle it.
fn test_steering_ie_qualification() {
    let saved_schedcfg = read_schedcfg();
    let saved_stid = read_stid();
    let saved_imask = read_imask();

    write_schedcfg((saved_schedcfg & !0xF) | SCHEDCFG_EN | L2VIC_L1_INTNO);
    write_stid((saved_stid & !STID_PRIO_MASK) | (10 << STID_PRIO_SHIFT));
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    reset_handler_state();

    // T1 will NOT enable IE
    T1_CLEAR_IE.store(1, Ordering::SeqCst);

    // T1=200 (best prio but IE=0), T2=100 (next best, IE=1)
    setup_threads(200, 100);
    iassignw((L2VIC_L1_INTNO << 16) | 0x0);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 2);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    T1_CLEAR_IE.store(0, Ordering::SeqCst);
    teardown_threads();
    write_stid(saved_stid);
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

/// Test 9: Dynamic priority change — two rounds with different priorities.
/// Higher STID.PRIO = higher priority = preferred for interrupt delivery.
fn test_steering_dynamic_priority() {
    let saved_schedcfg = read_schedcfg();
    let saved_stid = read_stid();
    let saved_imask = read_imask();

    write_schedcfg((saved_schedcfg & !0xF) | SCHEDCFG_EN | L2VIC_L1_INTNO);
    write_imask(saved_imask & !(1 << L2VIC_L1_INTNO));

    // Round 1: T0=10 (lowest), T1=200 (best), T2=100
    write_stid((saved_stid & !STID_PRIO_MASK) | (10 << STID_PRIO_SHIFT));
    reset_handler_state();

    setup_threads(200, 100);
    iassignw((L2VIC_L1_INTNO << 16) | 0x0);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 1);

    teardown_threads();
    clear_l2vic_state();

    // Round 2: T0=200 (best), T1=10 (lowest), T2=100
    write_stid((saved_stid & !STID_PRIO_MASK) | (200 << STID_PRIO_SHIFT));
    reset_handler_state();

    setup_threads(10, 100);
    iassignw((L2VIC_L1_INTNO << 16) | 0x0);

    clear_l2vic_state();
    trigger_l2vic_and_wait();

    check!(HANDLER_COUNT.load(Ordering::SeqCst) >= 1);
    check32!(HANDLER_HTID.load(Ordering::SeqCst), 0);

    iassignw((L2VIC_L1_INTNO << 16) | 0x0);
    teardown_threads();
    write_stid(saved_stid);
    write_imask(saved_imask);
    write_schedcfg(saved_schedcfg);
    clear_l2vic_state();
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Interrupt Steering");

    if !require_threads(0x7) {
        return test_suite_end() as i32;
    }

    // Set up L2VIC for cross-thread tests (panics if not functional)
    setup_l2vic();

    run_test("steering_disabled", test_steering_disabled);
    run_test("iassign_to_thread1", test_iassign_to_thread1);
    run_test("iassign_to_thread0", test_iassign_to_thread0);
    run_test("steering_priority_basic", test_steering_priority_basic);
    run_test(
        "steering_priority_reversed",
        test_steering_priority_reversed,
    );
    run_test(
        "steering_three_thread_ordering",
        test_steering_three_thread_ordering,
    );
    run_test(
        "steering_imask_overrides_priority",
        test_steering_imask_overrides_priority,
    );
    run_test("steering_ie_qualification", test_steering_ie_qualification);
    run_test("steering_dynamic_priority", test_steering_dynamic_priority);

    cleanup_l2vic();

    test_suite_end() as i32
}
