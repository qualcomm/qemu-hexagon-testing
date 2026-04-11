// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! TLB/MMU operation tests for Hexagon v81.
//!
//! Tests TLB read/write/probe, invalidation, ASID matching.

#![no_std]
#![no_main]

use hexagon_arch_tests::*;

/// Use high TLB indices that won't conflict with runtime's fixed entries.
const TEST_TLB_IDX: u32 = 60;

/// VPN for test: 1MB page 0x100 => VA 0x10000000.
const TEST_VPN: u32 = 0x100;

/// tlbw/tlbr: write entry at test index, read back, verify fields match.
fn test_tlb_write_read() {
    let hi = make_tlb_hi(TEST_VPN, 0, true);
    let lo = make_tlb_lo(TEST_VPN, TLB_PERM_XWRU, true);

    tlb_write(hi, lo, TEST_TLB_IDX);
    isync();

    let (read_hi, read_lo) = tlb_read(TEST_TLB_IDX);
    check32!(read_hi, hi);
    check32!(read_lo, lo);

    // Clean up
    tlb_invalidate(TEST_TLB_IDX);
}

/// tlbp: write entry for VA, probe, verify correct index returned.
fn test_tlb_probe_hit() {
    let hi = make_tlb_hi(TEST_VPN, 0, true);
    let lo = make_tlb_lo(TEST_VPN, TLB_PERM_XWRU, true);

    tlb_write(hi, lo, TEST_TLB_IDX);
    isync();

    // Probe using the hi word (contains VPN+ASID+G)
    let result = tlb_probe(hi);

    // Result should be a non-negative index
    check!(result >= 0);
    check32!(result as u32, TEST_TLB_IDX);

    // Clean up
    tlb_invalidate(TEST_TLB_IDX);
}

/// tlbp miss: probe unmapped VA, verify bit 31 set (not found).
fn test_tlb_probe_miss() {
    // Probe for VPN 0x200 which we haven't explicitly mapped at a known index.
    // Use global=false and ASID=0x7F to avoid matching any existing entry.
    let probe_hi = make_tlb_hi(0x200, 0x7F, false);
    let result = tlb_probe(probe_hi);
    // Bit 31 should be set (miss)
    check!(result < 0);
}

/// TLB invalidate: write valid entry, invalidate it, probe returns not-found.
fn test_tlb_invalidate() {
    let hi = make_tlb_hi(TEST_VPN, 0, true);
    let lo = make_tlb_lo(TEST_VPN, TLB_PERM_XWRU, true);

    // Write entry
    tlb_write(hi, lo, TEST_TLB_IDX);
    isync();

    // Verify it's there
    let result = tlb_probe(hi);
    check!(result >= 0);

    // Invalidate
    tlb_invalidate(TEST_TLB_IDX);

    // Probe should now miss
    let result2 = tlb_probe(hi);
    check!(result2 < 0);
}

/// Global entry: verify a global entry (G=1) can be probed.
fn test_tlb_global_entry() {
    // Write a global entry at test index
    let hi = make_tlb_hi(0x180, 0, true);
    let lo = make_tlb_lo(0x180, TLB_PERM_XWRU, true);

    tlb_write(hi, lo, TEST_TLB_IDX);
    isync();

    // Probe should find it
    let result = tlb_probe(hi);
    check!(result >= 0);
    check32!(result as u32, TEST_TLB_IDX);

    // Read back and verify global bit (bit 0 of hi)
    let (read_hi, _read_lo) = tlb_read(TEST_TLB_IDX);
    check!(read_hi & 1 != 0); // Global bit set

    // Clean up
    tlb_invalidate(TEST_TLB_IDX);
}

/// Multiple concurrent entries at different TLB indices.
fn test_tlb_multiple_entries() {
    const IDX_A: u32 = 58;
    const IDX_B: u32 = 59;
    const IDX_C: u32 = TEST_TLB_IDX;

    let hi_a = make_tlb_hi(0x110, 0, true);
    let lo_a = make_tlb_lo(0x110, TLB_PERM_XWRU, true);
    let hi_b = make_tlb_hi(0x120, 0, true);
    let lo_b = make_tlb_lo(0x120, TLB_PERM_XWRU, true);
    let hi_c = make_tlb_hi(0x130, 0, true);
    let lo_c = make_tlb_lo(0x130, TLB_PERM_XWRU, true);

    tlb_write(hi_a, lo_a, IDX_A);
    tlb_write(hi_b, lo_b, IDX_B);
    tlb_write(hi_c, lo_c, IDX_C);
    isync();

    // Probe each — should find at correct indices
    let res_a = tlb_probe(hi_a);
    let res_b = tlb_probe(hi_b);
    let res_c = tlb_probe(hi_c);
    check32!(res_a as u32, IDX_A);
    check32!(res_b as u32, IDX_B);
    check32!(res_c as u32, IDX_C);

    // Read back each
    let (rh_a, rl_a) = tlb_read(IDX_A);
    check32!(rh_a, hi_a);
    check32!(rl_a, lo_a);
    let (rh_b, rl_b) = tlb_read(IDX_B);
    check32!(rh_b, hi_b);
    check32!(rl_b, lo_b);

    // Clean up
    tlb_invalidate(IDX_A);
    tlb_invalidate(IDX_B);
    tlb_invalidate(IDX_C);
}

/// TLB overwrite: write entry, overwrite with different data, verify new data.
fn test_tlb_overwrite() {
    let hi1 = make_tlb_hi(0x140, 0, true);
    let lo1 = make_tlb_lo(0x140, TLB_PERM_XWRU, true);

    tlb_write(hi1, lo1, TEST_TLB_IDX);
    isync();

    // Verify first entry
    let (rh1, rl1) = tlb_read(TEST_TLB_IDX);
    check32!(rh1, hi1);
    check32!(rl1, lo1);

    // Overwrite with different VPN
    let hi2 = make_tlb_hi(0x150, 0, true);
    let lo2 = make_tlb_lo(0x150, TLB_PERM_XWR, true);

    tlb_write(hi2, lo2, TEST_TLB_IDX);
    isync();

    // Verify overwritten entry
    let (rh2, rl2) = tlb_read(TEST_TLB_IDX);
    check32!(rh2, hi2);
    check32!(rl2, lo2);

    // Old VPN should no longer probe at this index
    let result_old = tlb_probe(hi1);
    check!(result_old < 0 || result_old as u32 != TEST_TLB_IDX);

    // New VPN should probe correctly
    let result_new = tlb_probe(hi2);
    check32!(result_new as u32, TEST_TLB_IDX);

    // Clean up
    tlb_invalidate(TEST_TLB_IDX);
}

/// TLB non-global entry: probe with matching ASID should hit.
fn test_tlb_asid_match() {
    let asid: u32 = 5;
    let hi = make_tlb_hi(0x160, asid, false); // non-global
    let lo = make_tlb_lo(0x160, TLB_PERM_XWRU, true);

    tlb_write(hi, lo, TEST_TLB_IDX);
    isync();

    // Probe with same ASID should hit
    let probe_hi = make_tlb_hi(0x160, asid, false);
    let result = tlb_probe(probe_hi);
    check!(result >= 0);
    check32!(result as u32, TEST_TLB_IDX);

    // Clean up
    tlb_invalidate(TEST_TLB_IDX);
}

/// TLB entry with different permissions (no execute).
fn test_tlb_permissions() {
    let hi = make_tlb_hi(0x170, 0, true);
    let lo_ru = make_tlb_lo(0x170, TLB_PERM_RU, true); // Read+User only

    tlb_write(hi, lo_ru, TEST_TLB_IDX);
    isync();

    // Read back and verify the lo word encodes the permission bits
    let (_rh, rl) = tlb_read(TEST_TLB_IDX);
    // The perm bits are encoded in the lo word. Verify we can read back
    // a different lo than XWRU
    let lo_xwru = make_tlb_lo(0x170, TLB_PERM_XWRU, true);
    check32_ne!(rl, lo_xwru);

    // Clean up
    tlb_invalidate(TEST_TLB_IDX);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("TLB/MMU");

    run_test("tlb_write_read", test_tlb_write_read);
    run_test("tlb_probe_hit", test_tlb_probe_hit);
    run_test("tlb_probe_miss", test_tlb_probe_miss);
    run_test("tlb_invalidate", test_tlb_invalidate);
    run_test("tlb_global_entry", test_tlb_global_entry);
    run_test("tlb_multiple_entries", test_tlb_multiple_entries);
    run_test("tlb_overwrite", test_tlb_overwrite);
    run_test("tlb_asid_match", test_tlb_asid_match);
    run_test("tlb_permissions", test_tlb_permissions);

    test_suite_end() as i32
}
