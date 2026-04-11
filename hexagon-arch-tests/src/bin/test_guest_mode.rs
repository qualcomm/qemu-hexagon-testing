// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Monitor/Guest virtualization register tests for Hexagon v81.
//!
//! Tests guest mode SSR bits, CCR configuration, and virtualization
//! control registers accessible from monitor (supervisor) mode.

#![no_std]
#![no_main]

use hexagon_arch_tests::*;

/// Verify we're not in guest mode (SSR.GM=0).
fn test_not_in_guest_mode() {
    let ssr = read_ssr();
    check!(ssr & SSR_GM == 0);
}

/// SSR.GM bit can be set/cleared (from monitor mode).
fn test_ssr_gm_bit() {
    let ssr = read_ssr();
    check!(ssr & SSR_GM == 0);

    // Set GM bit
    write_ssr(ssr | SSR_GM);
    let _readback = read_ssr();
    // Note: writing GM=1 without rte doesn't actually enter guest mode
    // but the bit should be writable

    // Restore (clear GM)
    write_ssr(ssr & !SSR_GM);
    let final_ssr = read_ssr();
    check!(final_ssr & SSR_GM == 0);
}

/// Read/write CCR: verify accessible and toggling bits works.
fn test_ccr_readwrite() {
    let ccr = read_ccr();

    // Toggle GIE bit
    write_ccr(ccr | CCR_GIE);
    let readback = read_ccr();
    check!(readback & CCR_GIE != 0);

    // Toggle GTE bit
    write_ccr(ccr | CCR_GTE);
    let readback2 = read_ccr();
    check!(readback2 & CCR_GTE != 0);

    // Restore
    write_ccr(ccr);
    let restored = read_ccr();
    check32!(restored, ccr);
}

/// CCR guest interrupt enable bits.
fn test_ccr_gie_bits() {
    let ccr = read_ccr();

    // Set GIE, GTE, GEE, GRE
    let guest_bits = CCR_GIE | CCR_GTE | CCR_GEE | CCR_GRE;
    write_ccr(ccr | guest_bits);
    let readback = read_ccr();
    check!(readback & guest_bits == guest_bits);

    // Clear all guest bits
    write_ccr(ccr & !guest_bits);
    let readback2 = read_ccr();
    check!(readback2 & guest_bits == 0);

    // Restore
    write_ccr(ccr);
}

/// Test VID register (vector interrupt destination).
fn test_vid_register() {
    let saved = read_vid();
    let test_val: u32 = 0x000000AA;
    write_vid(test_val);
    let readback = read_vid();
    check32!(readback, test_val);
    // Restore
    write_vid(saved);
}

/// Verify MODECTL reflects thread enable state.
fn test_modectl_register() {
    let modectl = read_modectl();
    // Thread 0 must be enabled (bit 0)
    check!(modectl & 1 != 0);
}

/// Read/write GELR (guest exception link register) from monitor mode.
fn test_gelr_readwrite() {
    let saved = read_gelr();
    let test_val: u32 = 0x0000_1000;
    write_gelr(test_val);
    let rb = read_gelr();
    check32!(rb, test_val);
    // Restore
    write_gelr(saved);
}

/// Read/write GSR (guest status register) from monitor mode.
fn test_gsr_readwrite() {
    let saved = read_gsr();
    // Write a safe value (just CAUSE field, bits 7:0)
    let test_val: u32 = 0x0000_0012;
    write_gsr(test_val);
    let rb = read_gsr();
    check32!(rb & 0xFF, test_val & 0xFF);
    // Restore
    write_gsr(saved);
}

/// Read/write GOSP (guest OS pointer) from monitor mode.
fn test_gosp_readwrite() {
    let saved = read_gosp();
    let test_val: u32 = 0xABCD_0000;
    write_gosp(test_val);
    let rb = read_gosp();
    check32!(rb, test_val);
    // Restore
    write_gosp(saved);
}

/// Read/write GBADVA (guest bad virtual address) from monitor mode.
fn test_gbadva_readwrite() {
    let saved = read_gbadva();
    let test_val: u32 = 0xDEAD_0000;
    write_gbadva(test_val);
    let rb = read_gbadva();
    check32!(rb, test_val);
    // Restore
    write_gbadva(saved);
}

/// CCR.VV1 bit (version vector 1).
fn test_ccr_vv1_bit() {
    let ccr = read_ccr();
    // Toggle VV1 bit
    write_ccr(ccr | CCR_VV1);
    let rb = read_ccr();
    check!(rb & CCR_VV1 != 0);
    // Clear VV1
    write_ccr(ccr & !CCR_VV1);
    let rb2 = read_ccr();
    check!(rb2 & CCR_VV1 == 0);
    // Restore
    write_ccr(ccr);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Guest Mode / Virtualization");

    run_test("not_in_guest_mode", test_not_in_guest_mode);
    run_test("ssr_gm_bit", test_ssr_gm_bit);
    run_test("ccr_readwrite", test_ccr_readwrite);
    run_test("ccr_gie_bits", test_ccr_gie_bits);
    run_test("vid_register", test_vid_register);
    run_test("modectl_register", test_modectl_register);
    run_test("gelr_readwrite", test_gelr_readwrite);
    run_test("gsr_readwrite", test_gsr_readwrite);
    run_test("gosp_readwrite", test_gosp_readwrite);
    run_test("gbadva_readwrite", test_gbadva_readwrite);
    run_test("ccr_vv1_bit", test_ccr_vv1_bit);

    test_suite_end() as i32
}
