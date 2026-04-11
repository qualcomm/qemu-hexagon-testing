// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Privilege mode tests for Hexagon v81.
//!
//! Tests SSR privilege mode bits and the trap0(#1) exit-user-mode handler
//! path from supervisor mode.

#![no_std]
#![no_main]
#![feature(asm_experimental_arch)]

use core::arch::asm;
use hexagon_arch_tests::*;

/// Verify we're in supervisor mode (SSR.UM=0).
fn test_supervisor_mode() {
    let ssr = read_ssr();
    check!(ssr & SSR_UM == 0);
}

/// Verify SSR.UM=0 in supervisor mode and that SSR is writable.
/// Note: we do NOT set UM=1 directly because that immediately changes
/// the privilege level, making subsequent supervisor register access fault.
fn test_ssr_um_bit() {
    let ssr = read_ssr();
    // We're in supervisor mode, UM should be 0
    check!(ssr & SSR_UM == 0);

    // Verify SSR is writable by toggling a safe bit (PE = parity enable)
    let modified = ssr ^ SSR_PE;
    write_ssr(modified);
    let readback = read_ssr();
    check!(readback & SSR_PE == modified & SSR_PE);

    // Restore
    write_ssr(ssr);
}

/// Test SSR.IE bit can be toggled.
fn test_ssr_ie_toggle() {
    let ssr = read_ssr();

    // Set IE
    write_ssr(ssr | SSR_IE);
    let readback = read_ssr();
    check!(readback & SSR_IE != 0);

    // Clear IE
    write_ssr(ssr & !SSR_IE);
    let readback2 = read_ssr();
    check!(readback2 & SSR_IE == 0);

    // Restore
    write_ssr(ssr);
}

/// Test trap0(#1) handler path: it should record cause and advance ELR.
fn test_trap0_exit_user_handler() {
    reset_exception_state();
    // Call trap0(#1) from supervisor mode — the handler should detect cause=1
    // and clear UM (which is already 0). It should still advance ELR and return.
    unsafe {
        asm!("trap0(#1)", options(nostack));
    }

    // Verify we survived and are still in supervisor mode
    let ssr = read_ssr();
    check!(ssr & SSR_UM == 0);
}

/// Test SSR.XE bit (exception enable).
fn test_ssr_xe_bit() {
    let ssr = read_ssr();
    // XE should be set by crt0
    check!(ssr & SSR_XE != 0);
}

/// Test SSR.CE bit (cycle enable for user mode).
fn test_ssr_ce_bit() {
    let ssr = read_ssr();

    // Set CE
    write_ssr(ssr | SSR_CE);
    let readback = read_ssr();
    check!(readback & SSR_CE != 0);

    // Clear CE
    write_ssr(ssr & !SSR_CE);
    let readback2 = read_ssr();
    check!(readback2 & SSR_CE == 0);

    // Restore
    write_ssr(ssr);
}

/// SSR.PE (parity enable) bit can be toggled.
fn test_ssr_pe_bit() {
    let ssr = read_ssr();

    // Set PE
    write_ssr(ssr | SSR_PE);
    let rb = read_ssr();
    check!(rb & SSR_PE != 0);

    // Clear PE
    write_ssr(ssr & !SSR_PE);
    let rb2 = read_ssr();
    check!(rb2 & SSR_PE == 0);

    // Restore
    write_ssr(ssr);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("User Mode / Privilege");

    run_test("supervisor_mode", test_supervisor_mode);
    run_test("ssr_um_bit", test_ssr_um_bit);
    run_test("ssr_ie_toggle", test_ssr_ie_toggle);
    run_test("trap0_exit_user_handler", test_trap0_exit_user_handler);
    run_test("ssr_xe_bit", test_ssr_xe_bit);
    run_test("ssr_ce_bit", test_ssr_ce_bit);
    run_test("ssr_pe_bit", test_ssr_pe_bit);

    test_suite_end() as i32
}
