// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Performance counter tests for Hexagon v81.
//!
//! Tests PCYCLE, PCYCLELO/HI, UPCYCLE, enable/disable via SYSCFG.

#![no_std]
#![no_main]

use hexagon_arch_tests::*;

/// PCYCLE should increment: read, execute NOPs, read again, verify increased.
fn test_pcycle_incrementing() {
    let before = read_pcycle();
    busy_loop(100);
    let after = read_pcycle();
    check!(after > before);
}

/// PCYCLELO/PCYCLEHI: read individual halves and verify consistency.
fn test_pcycle_hilo() {
    let lo = read_pcyclelo();
    let hi = read_pcyclehi();
    // hi:lo should form a plausible counter value (non-zero since we've been running)
    let combined = ((hi as u64) << 32) | (lo as u64);
    check!(combined > 0);
}

/// PCYCLE enable/disable: clear SYSCFG bit 6, verify counter stops, re-enable.
fn test_pcycle_enable_disable() {
    // Disable pcycle
    let syscfg = read_syscfg();
    write_syscfg(syscfg & !SYSCFG_PCYCLE_EN);

    let before = read_pcycle();
    busy_loop(100);
    let after = read_pcycle();
    // Counter should NOT have advanced (or advanced very little)
    check!(after == before);

    // Re-enable
    write_syscfg(syscfg | SYSCFG_PCYCLE_EN);

    // Verify it's incrementing again
    let before2 = read_pcycle();
    busy_loop(100);
    let after2 = read_pcycle();
    check!(after2 > before2);
}

/// UPCYCLE: when SSR.CE=1, upcycle should be accessible and non-zero.
fn test_upcycle() {
    // Set CE bit to enable user pcycle access
    let ssr = read_ssr();
    write_ssr(ssr | SSR_CE);

    let upc = read_upcycle();
    // upcycle should be non-zero since pcycle is running
    check!(upc > 0);

    // Restore SSR
    write_ssr(ssr);
}

/// PCYCLE monotonicity: multiple reads should always increase.
fn test_pcycle_monotonic() {
    let a = read_pcycle();
    busy_loop(10);
    let b = read_pcycle();
    busy_loop(10);
    let c = read_pcycle();
    check!(b > a);
    check!(c > b);
}

/// PCYCLE 64-bit consistency: hi:lo from read_pcycle should match
/// individual reads (approximately, allowing for time between reads).
fn test_pcycle_64bit_consistency() {
    let combined = read_pcycle();
    let lo = read_pcyclelo();
    let hi = read_pcyclehi();

    // The combined value was read earlier, so lo/hi should be >= combined parts.
    // Just verify they're all non-zero and plausible
    let combined_lo = combined as u32;
    let combined_hi = (combined >> 32) as u32;

    // hi should match (counter shouldn't overflow 32-bit in a few instructions)
    check32!(hi, combined_hi);
    // lo should be >= combined_lo (it was read later)
    check!(lo >= combined_lo);
}

/// SYSCFG.PCYCLE_EN readback: verify the bit is set after enable.
fn test_pcycle_syscfg_bit() {
    let syscfg = read_syscfg();
    check!(syscfg & SYSCFG_PCYCLE_EN != 0);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Performance Counters");

    run_test("pcycle_incrementing", test_pcycle_incrementing);
    run_test("pcycle_hilo", test_pcycle_hilo);
    run_test("pcycle_enable_disable", test_pcycle_enable_disable);
    run_test("upcycle", test_upcycle);
    run_test("pcycle_monotonic", test_pcycle_monotonic);
    run_test("pcycle_64bit_consistency", test_pcycle_64bit_consistency);
    run_test("pcycle_syscfg_bit", test_pcycle_syscfg_bit);

    test_suite_end() as i32
}
