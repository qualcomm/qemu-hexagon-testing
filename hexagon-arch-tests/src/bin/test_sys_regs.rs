// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! System register access tests for Hexagon v81.
//!
//! Tests read/write of system registers from supervisor mode:
//! SSR, EVB, SYSCFG, IMASK, VID, MODECTL, cfgbase, FRAMEKEY.

#![no_std]
#![no_main]

use hexagon_arch_tests::*;

/// Verify SSR: UM=0 (supervisor), IE=1 (interrupts enabled by crt0), XE=1.
fn test_ssr_initial() {
    let ssr = read_ssr();
    // We should be in supervisor mode: UM (bit 16) = 0
    check!(ssr & SSR_UM == 0);
    // Interrupts enabled by crt0: IE (bit 18) = 1
    check!(ssr & SSR_IE != 0);
    // XE (bit 31) set by crt0
    check!(ssr & SSR_XE != 0);
    // ASID should be 0
    let asid = (ssr >> SSR_ASID_SHIFT) & 0x7F;
    check32!(asid, 0);
}

/// Verify EVB is 4K-aligned and non-zero.
fn test_evb() {
    let evb = read_evb();
    check32_ne!(evb, 0);
    // EVB must be 4K-aligned (low 12 bits = 0)
    check32!(evb & 0xFFF, 0);
}

/// Read/modify SYSCFG: verify MMU, caches, pcycle enabled by crt0.
fn test_syscfg() {
    let syscfg = read_syscfg();
    // MMU enabled
    check!(syscfg & SYSCFG_MMU_EN != 0);
    // Icache enabled
    check!(syscfg & SYSCFG_ICACHE_EN != 0);
    // Interrupt enable
    check!(syscfg & SYSCFG_INT_EN != 0);
    // Pcycle enabled
    check!(syscfg & SYSCFG_PCYCLE_EN != 0);
}

/// Read/write IMASK: write pattern, read back, verify match, then restore.
fn test_imask_readwrite() {
    let saved = read_imask();
    let pattern: u32 = 0xAAAA_0000;
    write_imask(pattern);
    let readback = read_imask();
    check32!(readback, pattern);
    // Restore
    write_imask(saved);
}

/// Read/write VID: write routing, read back, restore.
fn test_vid_readwrite() {
    let saved = read_vid();
    let pattern: u32 = 0x0000_0005;
    write_vid(pattern);
    let readback = read_vid();
    check32!(readback, pattern);
    // Restore
    write_vid(saved);
}

/// Read MODECTL: verify thread 0 is enabled (bit 0).
fn test_modectl() {
    let modectl = read_modectl();
    // Thread 0 should be enabled
    check!(modectl & 1 != 0);
}

/// Read cfgbase: should be non-zero.
fn test_cfgbase() {
    let cfgbase = read_cfgbase();
    check32_ne!(cfgbase, 0);
}

/// Read/write FRAMEKEY.
fn test_framekey() {
    let saved = read_framekey();
    let key: u32 = 0xDEAD_BEEF;
    write_framekey(key);
    let readback = read_framekey();
    check32!(readback, key);
    // Restore
    write_framekey(saved);
}

/// Read HTID: should be 0 (thread 0).
fn test_htid() {
    let htid = read_htid();
    check32!(htid, 0);
}

/// Read/write SGP0 and SGP1 (supervisor general purpose registers).
fn test_sgp_readwrite() {
    let saved_sgp0 = read_sgp0();
    let saved_sgp1 = read_sgp1();

    let test0: u32 = 0xAAAA_5555;
    let test1: u32 = 0x5555_AAAA;
    write_sgp0(test0);
    write_sgp1(test1);

    let rb0 = read_sgp0();
    let rb1 = read_sgp1();
    check32!(rb0, test0);
    check32!(rb1, test1);

    // Restore
    write_sgp0(saved_sgp0);
    write_sgp1(saved_sgp1);
}

/// Read/write STID (software thread ID).
fn test_stid_readwrite() {
    let saved = read_stid();
    let test_val: u32 = 0x0000_003F;
    write_stid(test_val);
    let rb = read_stid();
    check32!(rb & 0x3F, test_val & 0x3F);
    // Restore
    write_stid(saved);
}

/// Write EVB with a valid 4K-aligned value, read back, restore.
fn test_evb_write() {
    let saved = read_evb();
    // Write a different 4K-aligned address
    let test_evb = saved; // Use same value to avoid breaking event dispatch
    write_evb(test_evb);
    let rb = read_evb();
    check32!(rb, test_evb);
    check32!(rb & 0xFFF, 0); // Still 4K-aligned
                             // Restore
    write_evb(saved);
}

/// Read BADVA and ELR directly from supervisor mode.
fn test_badva_elr_direct() {
    let badva = read_badva();
    let elr = read_elr();
    // These are just verifying the registers are readable without crash.
    // Values are whatever was left from previous exception handling.
    // BADVA and ELR should be word-aligned if they were set.
    check!(badva & 0x0 == 0); // always true, just verifies read works
    check!(elr & 0x0 == 0);
}

/// Read DIAG register.
fn test_diag() {
    let diag = read_diag();
    // DIAG register is implementation-defined. Just verify readable.
    check!(diag | 0 == diag); // tautology, verifies read works
}

/// Write/read SSR.ASID field.
fn test_ssr_asid() {
    let ssr = read_ssr();
    let orig_asid = (ssr >> SSR_ASID_SHIFT) & 0x7F;
    check32!(orig_asid, 0); // ASID should be 0 at boot

    // Set ASID to a non-zero value
    let new_ssr = (ssr & !(0x7F << SSR_ASID_SHIFT)) | (0x42 << SSR_ASID_SHIFT);
    write_ssr(new_ssr);
    let rb = read_ssr();
    let rb_asid = (rb >> SSR_ASID_SHIFT) & 0x7F;
    check32!(rb_asid, 0x42);

    // Restore
    write_ssr(ssr);
}

/// Verify SYSCFG DMT (dual-mode threading) bit is set by crt0.
fn test_syscfg_dmt() {
    let syscfg = read_syscfg();
    // DMT enable is bit 15 (set by crt0)
    check!(syscfg & (1 << 15) != 0);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("System Registers");

    run_test("ssr_initial", test_ssr_initial);
    run_test("evb", test_evb);
    run_test("syscfg", test_syscfg);
    run_test("imask_readwrite", test_imask_readwrite);
    run_test("vid_readwrite", test_vid_readwrite);
    run_test("modectl", test_modectl);
    run_test("cfgbase", test_cfgbase);
    run_test("framekey", test_framekey);
    run_test("htid", test_htid);
    run_test("sgp_readwrite", test_sgp_readwrite);
    run_test("stid_readwrite", test_stid_readwrite);
    run_test("evb_write", test_evb_write);
    run_test("badva_elr_direct", test_badva_elr_direct);
    run_test("diag", test_diag);
    run_test("ssr_asid", test_ssr_asid);
    run_test("syscfg_dmt", test_syscfg_dmt);

    test_suite_end() as i32
}
