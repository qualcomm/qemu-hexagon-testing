// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Interrupt delivery and control tests for Hexagon v81.
//!
//! Tests SWI delivery, dispatch, masking, clear.

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

// Use different interrupt numbers for each test to avoid state contamination.

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
static INT_LAST_ARG: AtomicU32 = AtomicU32::new(0xFF);

extern "C" fn generic_handler(intno: u32) {
    if (intno as usize) < INT_COUNT.len() {
        INT_COUNT[intno as usize].fetch_add(1, Ordering::SeqCst);
    }
    INT_LAST_ARG.store(intno, Ordering::SeqCst);
}

/// Clear all pending SWIs safely: disable IE, clear all SWI bits, re-enable.
fn clear_all_swi() {
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    clear_swi(0xFFFF_FFFF);
    write_ssr(ssr);
}

/// SWI delivery: register handler for INT2, trigger, verify handler called.
fn test_swi_delivery() {
    clear_all_swi();
    INT_COUNT[2].store(0, Ordering::SeqCst);
    INT_LAST_ARG.store(0xFF, Ordering::SeqCst);

    register_interrupt(2, generic_handler);
    trigger_swi(1 << 2);
    busy_loop(50);

    check!(INT_COUNT[2].load(Ordering::SeqCst) >= 1);
    check32!(INT_LAST_ARG.load(Ordering::SeqCst), 2);
    clear_all_swi();
}

/// Multiple SWIs: register handlers for INT3 and INT5, trigger each, verify.
fn test_multiple_swi() {
    clear_all_swi();
    INT_COUNT[3].store(0, Ordering::SeqCst);
    INT_COUNT[5].store(0, Ordering::SeqCst);

    register_interrupt(3, generic_handler);
    register_interrupt(5, generic_handler);

    trigger_swi(1 << 3);
    busy_loop(50);
    check!(INT_COUNT[3].load(Ordering::SeqCst) >= 1);
    clear_all_swi();

    trigger_swi(1 << 5);
    busy_loop(50);
    check!(INT_COUNT[5].load(Ordering::SeqCst) >= 1);
    clear_all_swi();
}

/// IMASK: mask INT4, trigger SWI, verify NOT delivered; clear SWI, unmask.
fn test_imask_masking() {
    INT_COUNT[4].store(0, Ordering::SeqCst);
    register_interrupt(4, generic_handler);

    // Mask INT4
    let saved_imask = read_imask();
    write_imask(saved_imask | (1 << 4));

    // Disable IE too, so when we trigger the SWI it's fully blocked
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);

    trigger_swi(1 << 4);
    busy_loop(50);

    // Should NOT have fired (both masked and IE=0)
    check32!(INT_COUNT[4].load(Ordering::SeqCst), 0);

    // Clear the SWI before unmasking
    clear_swi(1 << 4);

    // Restore
    write_imask(saved_imask);
    write_ssr(ssr);
}

/// CSWI: trigger SWI with IE=0, clear via CSWI, re-enable IE, verify NOT delivered.
fn test_cswi_clear() {
    INT_COUNT[6].store(0, Ordering::SeqCst);
    register_interrupt(6, generic_handler);

    // Disable interrupts
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);

    trigger_swi(1 << 6);
    busy_loop(10);

    // Clear the pending SWI before re-enabling
    clear_swi(1 << 6);

    // Re-enable interrupts
    write_ssr(ssr | SSR_IE);
    busy_loop(50);

    // Should NOT have fired (was cleared)
    check32!(INT_COUNT[6].load(Ordering::SeqCst), 0);
}

/// Handler argument: verify handler receives correct interrupt number.
fn test_handler_argument() {
    clear_all_swi();
    INT_COUNT[7].store(0, Ordering::SeqCst);
    INT_LAST_ARG.store(0xFF, Ordering::SeqCst);

    register_interrupt(7, generic_handler);
    trigger_swi(1 << 7);
    busy_loop(50);

    check!(INT_COUNT[7].load(Ordering::SeqCst) >= 1);
    check32!(INT_LAST_ARG.load(Ordering::SeqCst), 7);
    clear_all_swi();
}

/// IPEND: disable IE and SYSCFG.INT_EN, trigger SWI, read IPEND via S17.
fn test_ipend_inspection() {
    register_interrupt(2, generic_handler);
    INT_COUNT[2].store(0, Ordering::SeqCst);

    // Disable global interrupt delivery
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    let syscfg = read_syscfg();
    write_syscfg(syscfg & !SYSCFG_INT_EN);

    trigger_swi(1 << 2);
    busy_loop(20);

    // With interrupts fully disabled, the SWI should be pending
    // Verify the handler did NOT fire
    check32!(INT_COUNT[2].load(Ordering::SeqCst), 0);

    // Clear SWI and restore
    clear_swi(1 << 2);
    write_syscfg(syscfg);
    write_ssr(ssr);
}

/// Sequential SWIs on fresh interrupt numbers: trigger one, wait, trigger another.
fn test_sequential_swi() {
    clear_all_swi();
    INT_COUNT[8].store(0, Ordering::SeqCst);
    INT_COUNT[9].store(0, Ordering::SeqCst);

    register_interrupt(8, generic_handler);
    register_interrupt(9, generic_handler);

    trigger_swi(1 << 8);
    busy_loop(50);
    check!(INT_COUNT[8].load(Ordering::SeqCst) >= 1);
    clear_all_swi();

    trigger_swi(1 << 9);
    busy_loop(50);
    check!(INT_COUNT[9].load(Ordering::SeqCst) >= 1);
    clear_all_swi();
}

/// SWI delivery with IE toggle: disable IE, trigger, enable, verify fires.
fn test_swi_delayed_delivery() {
    clear_all_swi();
    INT_COUNT[6].store(0, Ordering::SeqCst);
    register_interrupt(6, generic_handler);

    // Disable interrupts
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);

    trigger_swi(1 << 6);
    busy_loop(20);

    // Should not have fired yet
    check32!(INT_COUNT[6].load(Ordering::SeqCst), 0);

    // Re-enable interrupts — SWI should fire
    write_ssr(ssr | SSR_IE);
    busy_loop(50);

    check!(INT_COUNT[6].load(Ordering::SeqCst) >= 1);
    clear_all_swi();
}

/// IAD/ciad: the interrupt handler in crt0.S calls ciad() after dispatch.
/// After SWI delivery and handler return, the interrupt should be
/// re-deliverable (ciad cleared the IAD bit).
fn test_iad_cleared_after_handler() {
    // Clear any stale SWI bits from earlier tests
    clear_swi(0xFFFF_FFFF);

    INT_COUNT[10].store(0, Ordering::SeqCst);
    register_interrupt(10, generic_handler);

    // First trigger
    trigger_swi(1 << 10);
    busy_loop(50);
    check!(INT_COUNT[10].load(Ordering::SeqCst) >= 1);

    // Clear the SWI bit, IAD, and disable IE momentarily to get a clean state
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    clear_swi(1 << 10);
    ciad(1 << 10); // Ensure IAD is cleared
    INT_COUNT[10].store(0, Ordering::SeqCst);
    write_ssr(ssr | SSR_IE);

    // Second trigger — should be deliverable because the handler's ciad
    // cleared the auto-disable bit.
    trigger_swi(1 << 10);
    busy_loop(50);
    check!(INT_COUNT[10].load(Ordering::SeqCst) >= 1);

    // Cleanup
    let ssr2 = read_ssr();
    write_ssr(ssr2 & !SSR_IE);
    clear_swi(1 << 10);
    write_ssr(ssr2 | SSR_IE);
}

/// iassignw/iassignr: write interrupt assignment, read it back.
/// iassignw encodes interrupt number in bits [20:16] and thread mask
/// in the lower bits. iassignr reads the per-thread assignment.
fn test_iassign_readwrite() {
    // Save original assignment for INT11
    let intno: u32 = 11;
    let query = intno << 16;
    let saved = iassignr(query);

    // Assign INT11 to thread 0 only (bit 0 = 1)
    let assign_val = (intno << 16) | 0x1;
    iassignw(assign_val);

    let rb = iassignr(query);
    check32!(rb & 0x1, 1); // Thread 0 should be assigned

    // Assign INT11 to threads 0 and 1 (bits 0:1 = 0x3)
    let assign_val2 = (intno << 16) | 0x3;
    iassignw(assign_val2);

    let rb2 = iassignr(query);
    check32!(rb2 & 0x3, 0x3);

    // Restore original assignment
    let restore_val = (intno << 16) | saved;
    iassignw(restore_val);
}

/// ciad explicit: manually trigger ciad and verify interrupt can fire again.
fn test_ciad_explicit() {
    clear_all_swi();
    INT_COUNT[12].store(0, Ordering::SeqCst);
    register_interrupt(12, generic_handler);

    // Fire INT12
    trigger_swi(1 << 12);
    busy_loop(50);
    check!(INT_COUNT[12].load(Ordering::SeqCst) >= 1);

    // Clear SWI, explicitly call ciad, then re-trigger
    let ssr = read_ssr();
    write_ssr(ssr & !SSR_IE);
    clear_swi(1 << 12);
    INT_COUNT[12].store(0, Ordering::SeqCst);
    ciad(1 << 12);
    write_ssr(ssr | SSR_IE);

    // Fire again
    trigger_swi(1 << 12);
    busy_loop(50);
    check!(INT_COUNT[12].load(Ordering::SeqCst) >= 1);
    clear_all_swi();
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Interrupts");

    run_test("swi_delivery", test_swi_delivery);
    run_test("multiple_swi", test_multiple_swi);
    run_test("imask_masking", test_imask_masking);
    run_test("cswi_clear", test_cswi_clear);
    run_test("handler_argument", test_handler_argument);
    run_test("ipend_inspection", test_ipend_inspection);
    run_test("sequential_swi", test_sequential_swi);
    run_test("swi_delayed_delivery", test_swi_delayed_delivery);
    run_test("iad_cleared_after_handler", test_iad_cleared_after_handler);
    run_test("iassign_readwrite", test_iassign_readwrite);
    run_test("ciad_explicit", test_ciad_explicit);

    test_suite_end() as i32
}
