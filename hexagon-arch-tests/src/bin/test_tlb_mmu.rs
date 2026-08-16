// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! TLB/MMU operation tests for Hexagon v81.
//!
//! Tests TLB read/write/probe, invalidation, ASID matching,
//! and overlapping entry detection via ctlbw/tlboc.

#![no_std]
#![no_main]
#![feature(asm_experimental_arch)]

use core::arch::asm;
use hexagon_arch_tests::*;

/// Use high TLB indices that won't conflict with runtime's fixed entries.
const TEST_TLB_IDX: u32 = 60;

/// VPN for test: 1MB page 0x300 => VA 0x30000000.
/// Avoids 0x100 (0x10000000) which is the PL011 UART MMIO region.
const TEST_VPN: u32 = 0x300;

// ---------------------------------------------------------------------------
// TLB entry helpers with correct V/G encoding for overlap tests
// ---------------------------------------------------------------------------

/// Build a TLB hi word (R1 of the R1:0 pair) with explicit V and G bits.
///
/// TLB register layout (R1 = bits 63:32 of TLB entry):
///   bit 31 = V (valid)
///   bit 30 = G (global)
///   bits 26:20 = ASID
///   bits 19:0 = VPN (VA[31:12] for 1MB page = vpn_1m in bits [19:8])
///
/// The existing `make_tlb_hi` always sets V=1 G=1 (0xC000_0000). This
/// helper allows controlling G independently for overlap semantics testing.
fn make_tlb_hi_vg(vpn_1m: u32, asid: u32, global: bool) -> u32 {
    let v_bit: u32 = 1 << 31;
    let g_bit: u32 = if global { 1 << 30 } else { 0 };
    v_bit | g_bit | ((asid & 0x7F) << 20) | ((vpn_1m & 0xFFF) << 8)
}

/// Build a TLB hi word for a 4MB page with correct V/G encoding.
fn make_tlb_hi_vg_4m(vpn_1m: u32, asid: u32, global: bool) -> u32 {
    make_tlb_hi_vg(vpn_1m, asid, global)
}

// ---------------------------------------------------------------------------
// ctlbw / tlboc wrappers
// ---------------------------------------------------------------------------

/// Conditional TLB write: checks if entry (hi:lo) would overlap any existing
/// entry. If no overlap, writes to `idx` and returns 0x8000_0000.
/// If single overlap, does NOT write and returns the overlapping index.
/// If multiple overlaps, returns 0xFFFF_FFFF.
#[inline(always)]
fn ctlbw(hi: u32, lo: u32, idx: u32) -> u32 {
    let result: u32;
    unsafe {
        asm!(
            "r1:0 = combine({hi}, {lo})",
            "{res} = ctlbw(r1:0, {idx})",
            hi = in(reg) hi,
            lo = in(reg) lo,
            idx = in(reg) idx,
            res = out(reg) result,
            out("r0") _,
            out("r1") _,
            options(nostack),
        );
    }
    result
}

/// TLB overlap check: checks if entry (hi:lo) would overlap any existing
/// entry. Returns overlapping index, 0x8000_0000 (none), or 0xFFFF_FFFF
/// (multiple overlaps). Does NOT write.
#[inline(always)]
fn tlboc(hi: u32, lo: u32) -> u32 {
    let result: u32;
    unsafe {
        asm!(
            "r1:0 = combine({hi}, {lo})",
            "{res} = tlboc(r1:0)",
            hi = in(reg) hi,
            lo = in(reg) lo,
            res = out(reg) result,
            out("r0") _,
            out("r1") _,
            options(nostack),
        );
    }
    result
}

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

// ---------------------------------------------------------------------------
// Overlap detection tests (ctlbw / tlboc)
// ---------------------------------------------------------------------------

/// tlboc with no overlap: check an entry against an empty TLB region.
/// Should return 0x8000_0000 (no overlap found).
fn test_tlboc_no_overlap() {
    // Use VPN 0x400 which is not mapped anywhere in our test region.
    let hi = make_tlb_hi_vg(0x400, 0, true);
    let lo = make_tlb_lo(0x400, TLB_PERM_XWRU, true);

    let result = tlboc(hi, lo);
    check32!(result, 0x8000_0000);
}

/// ctlbw with no overlap: entry should be written and return 0x8000_0000.
fn test_ctlbw_no_overlap_writes() {
    const IDX: u32 = 55;
    let hi = make_tlb_hi_vg(0x410, 0, true);
    let lo = make_tlb_lo(0x410, TLB_PERM_XWRU, true);

    let result = ctlbw(hi, lo, IDX);
    isync();

    // Should succeed: 0x8000_0000 means "no overlap, entry written"
    check32!(result, 0x8000_0000);

    // Verify the entry was actually written
    let (read_hi, read_lo) = tlb_read(IDX);
    check32!(read_hi, hi);
    check32!(read_lo, lo);

    // Clean up
    tlb_invalidate(IDX);
}

/// ctlbw with single overlap: same VPN + same ASID already exists.
/// Should NOT write and return the index of the overlapping entry.
fn test_ctlbw_single_overlap_same_vpn_asid() {
    const EXISTING_IDX: u32 = 56;
    const NEW_IDX: u32 = 57;

    // Install an existing non-global entry with ASID=3
    let hi = make_tlb_hi_vg(0x420, 3, false);
    let lo = make_tlb_lo(0x420, TLB_PERM_XWRU, true);
    tlb_write(hi, lo, EXISTING_IDX);
    isync();

    // Try ctlbw with the same VPN and same ASID at a different index
    let new_hi = make_tlb_hi_vg(0x420, 3, false);
    let new_lo = make_tlb_lo(0x500, TLB_PERM_XWRU, true); // different PPN
    let result = ctlbw(new_hi, new_lo, NEW_IDX);

    // Should return the index of the overlapping entry
    check32!(result, EXISTING_IDX);

    // Verify the new entry was NOT written
    let (read_hi, _) = tlb_read(NEW_IDX);
    check32_ne!(read_hi, new_hi);

    // Clean up
    tlb_invalidate(EXISTING_IDX);
    tlb_invalidate(NEW_IDX);
}

/// tlboc detects overlap when existing entry is global.
/// A global entry overlaps with any ASID if VPN ranges intersect.
fn test_tlboc_overlap_global_entry() {
    const EXISTING_IDX: u32 = 56;

    // Install a global entry (G=1)
    let hi = make_tlb_hi_vg(0x430, 0, true);
    let lo = make_tlb_lo(0x430, TLB_PERM_XWRU, true);
    tlb_write(hi, lo, EXISTING_IDX);
    isync();

    // Check overlap with a non-global entry at the same VPN but different ASID
    let check_hi = make_tlb_hi_vg(0x430, 5, false);
    let check_lo = make_tlb_lo(0x430, TLB_PERM_XWRU, true);
    let result = tlboc(check_hi, check_lo);

    // Should detect the overlap because existing entry is global
    check32!(result, EXISTING_IDX);

    // Clean up
    tlb_invalidate(EXISTING_IDX);
}

/// tlboc reports no overlap when ASIDs differ and neither entry is global.
/// The overlap check ignores the incoming entry's G bit, but still requires
/// ASIDs to match (or the existing entry to be global).
fn test_tlboc_no_overlap_different_asid() {
    const EXISTING_IDX: u32 = 56;

    // Install a non-global entry with ASID=3
    let hi = make_tlb_hi_vg(0x440, 3, false);
    let lo = make_tlb_lo(0x440, TLB_PERM_XWRU, true);
    tlb_write(hi, lo, EXISTING_IDX);
    isync();

    // Check overlap with same VPN but ASID=7 (different), non-global
    let check_hi = make_tlb_hi_vg(0x440, 7, false);
    let check_lo = make_tlb_lo(0x440, TLB_PERM_XWRU, true);
    let result = tlboc(check_hi, check_lo);

    // Should NOT overlap because ASIDs differ and existing is non-global
    check32!(result, 0x8000_0000);

    // Clean up
    tlb_invalidate(EXISTING_IDX);
}

/// ctlbw with multiple overlapping entries returns 0xFFFF_FFFF.
fn test_ctlbw_multi_overlap() {
    const IDX_A: u32 = 54;
    const IDX_B: u32 = 55;
    const NEW_IDX: u32 = 56;

    // Install two global entries at the same VPN (deliberately creating overlap)
    let hi_a = make_tlb_hi_vg(0x450, 0, true);
    let lo_a = make_tlb_lo(0x450, TLB_PERM_XWRU, true);
    let hi_b = make_tlb_hi_vg(0x450, 0, true);
    let lo_b = make_tlb_lo(0x451, TLB_PERM_XWRU, true); // different PPN

    // Write both directly (bypassing ctlbw) to create intentional duplicates
    tlb_write(hi_a, lo_a, IDX_A);
    tlb_write(hi_b, lo_b, IDX_B);
    isync();

    // Now ctlbw for the same VPN should detect multiple overlaps
    let new_hi = make_tlb_hi_vg(0x450, 0, true);
    let new_lo = make_tlb_lo(0x452, TLB_PERM_XWRU, true);
    let result = ctlbw(new_hi, new_lo, NEW_IDX);

    // 0xFFFF_FFFF means multiple overlaps detected
    check32!(result, 0xFFFF_FFFF);

    // Clean up
    tlb_invalidate(IDX_A);
    tlb_invalidate(IDX_B);
    tlb_invalidate(NEW_IDX);
}

/// tlboc with multiple overlapping entries returns 0xFFFF_FFFF.
fn test_tlboc_multi_overlap() {
    const IDX_A: u32 = 54;
    const IDX_B: u32 = 55;

    // Create two entries at the same VPN (both global)
    let hi_a = make_tlb_hi_vg(0x460, 0, true);
    let lo_a = make_tlb_lo(0x460, TLB_PERM_XWRU, true);
    let hi_b = make_tlb_hi_vg(0x460, 0, true);
    let lo_b = make_tlb_lo(0x461, TLB_PERM_XWRU, true);

    tlb_write(hi_a, lo_a, IDX_A);
    tlb_write(hi_b, lo_b, IDX_B);
    isync();

    // tlboc should detect multiple
    let check_hi = make_tlb_hi_vg(0x460, 0, true);
    let check_lo = make_tlb_lo(0x462, TLB_PERM_XWRU, true);
    let result = tlboc(check_hi, check_lo);

    check32!(result, 0xFFFF_FFFF);

    // Clean up
    tlb_invalidate(IDX_A);
    tlb_invalidate(IDX_B);
}

/// Overlap detection with different page sizes: a 4MB page overlaps
/// multiple 1MB pages within its range.
///
/// A 4MB entry covers VPN range [base, base+3] in 1MB units.
/// A 1MB entry within that range should be detected as overlapping.
fn test_tlboc_overlap_different_page_sizes() {
    const EXISTING_IDX: u32 = 56;

    // Install a 4MB global page at VPN 0x480 (VA 0x48000000, covers 0x480-0x483)
    let hi_4m = make_tlb_hi_vg_4m(0x480, 0, true);
    let lo_4m = make_tlb_lo_4m(0x480, TLB_PERM_XWRU, true);
    tlb_write(hi_4m, lo_4m, EXISTING_IDX);
    isync();

    // Check overlap with a 1MB entry at VPN 0x481 (within the 4MB range)
    let check_hi = make_tlb_hi_vg(0x481, 0, true);
    let check_lo = make_tlb_lo(0x481, TLB_PERM_XWRU, true);
    let result = tlboc(check_hi, check_lo);

    // Should detect overlap
    check32!(result, EXISTING_IDX);

    // Clean up
    tlb_invalidate(EXISTING_IDX);
}

/// Overlap detection: 1MB entry does NOT overlap a 4MB entry at a
/// non-overlapping base address.
fn test_tlboc_no_overlap_different_page_sizes() {
    const EXISTING_IDX: u32 = 56;

    // 4MB page at VPN 0x480 covers VA range [0x48000000, 0x4C000000)
    let hi_4m = make_tlb_hi_vg_4m(0x480, 0, true);
    let lo_4m = make_tlb_lo_4m(0x480, TLB_PERM_XWRU, true);
    tlb_write(hi_4m, lo_4m, EXISTING_IDX);
    isync();

    // Check a 1MB entry at VPN 0x490 (outside the 4MB range)
    let check_hi = make_tlb_hi_vg(0x490, 0, true);
    let check_lo = make_tlb_lo(0x490, TLB_PERM_XWRU, true);
    let result = tlboc(check_hi, check_lo);

    // Should NOT overlap
    check32!(result, 0x8000_0000);

    // Clean up
    tlb_invalidate(EXISTING_IDX);
}

/// ctlbw overlap check ignores invalid entries: an invalid entry at the
/// same VPN should NOT cause an overlap detection.
fn test_ctlbw_ignores_invalid_entries() {
    const EXISTING_IDX: u32 = 56;
    const NEW_IDX: u32 = 57;

    // Write a valid entry, then invalidate it
    let hi = make_tlb_hi_vg(0x4A0, 0, true);
    let lo = make_tlb_lo(0x4A0, TLB_PERM_XWRU, true);
    tlb_write(hi, lo, EXISTING_IDX);
    isync();
    tlb_invalidate(EXISTING_IDX);

    // Now ctlbw with the same VPN should succeed (no valid overlap)
    let new_hi = make_tlb_hi_vg(0x4A0, 0, true);
    let new_lo = make_tlb_lo(0x4A0, TLB_PERM_XWRU, true);
    let result = ctlbw(new_hi, new_lo, NEW_IDX);
    isync();

    // Should succeed: no valid overlapping entry
    check32!(result, 0x8000_0000);

    // Verify it was written
    let (read_hi, read_lo) = tlb_read(NEW_IDX);
    check32!(read_hi, new_hi);
    check32!(read_lo, new_lo);

    // Clean up
    tlb_invalidate(NEW_IDX);
}

/// The overlap check ignores the incoming entry's Global bit.
/// Verify: a new global entry still does NOT overlap an existing non-global
/// entry with a different ASID (the incoming G=1 doesn't expand the match).
fn test_tlboc_incoming_global_no_bypass() {
    const EXISTING_IDX: u32 = 56;

    // Install a non-global entry with ASID=3
    let hi = make_tlb_hi_vg(0x4B0, 3, false);
    let lo = make_tlb_lo(0x4B0, TLB_PERM_XWRU, true);
    tlb_write(hi, lo, EXISTING_IDX);
    isync();

    // Check overlap with incoming entry that IS global but has ASID=7
    // Incoming G bit is ignored, so this should NOT match
    // (existing is non-global + ASID mismatch)
    let check_hi = make_tlb_hi_vg(0x4B0, 7, true); // incoming G=1
    let check_lo = make_tlb_lo(0x4B0, TLB_PERM_XWRU, true);
    let result = tlboc(check_hi, check_lo);

    // Should NOT overlap
    check32!(result, 0x8000_0000);

    // Clean up
    tlb_invalidate(EXISTING_IDX);
}

// ---------------------------------------------------------------------------
// Page-size field decoding
//
// The page size of an entry is encoded as the position of the lowest set bit
// of the PPD field -- lo[23:0]. Only that field participates: the bits above
// it (cache attributes, permissions) and the whole hi word belong to other
// fields. An implementation that scans the entire 64-bit entry instead will
// decode a nonsense size whenever the PPD field is empty, and a decoder that
// then trusts the result produces a page large enough to overlap everything.
//
// These entries deliberately have an empty PPD field, which is the shape a
// kernel produces when it parks a bookkeeping value in an unused TLB slot.
// ---------------------------------------------------------------------------

/// A non-zero value with an empty PPD field, i.e. no page-size marker at all.
/// Bits are set only above lo[23:0] so the size field is genuinely empty
/// rather than merely zero-valued.
const EMPTY_PPD_LO: u32 = 0xF700_0000;

/// A lo word whose PPD field encodes the largest page size. The lowest set
/// bit of lo[23:0] is bit 12, which is a perfectly legal encoding -- an entry
/// carrying it spans the entire address space. Used to check that an entry is
/// tested for validity before its span is ever consulted.
const WIDEST_PAGE_LO: u32 = 0x0000_1000;

/// tlboc against an entry whose PPD field carries no size marker.
///
/// tlboc forces the valid bit on for the entry it is handed, so this exercises
/// the size decode on the incoming (probe) side. With an empty PPD the entry
/// covers one small page at its own VPN; it must not be treated as spanning
/// the address space and colliding with unrelated mappings.
fn test_tlboc_empty_ppd_is_not_giant_page() {
    const OTHER_IDX: u32 = 56;

    // A valid, unrelated mapping somewhere else in the address space. A
    // wrongly-inflated probe page would swallow this and report an overlap.
    let other_hi = make_tlb_hi_vg(0x4C0, 0, true);
    let other_lo = make_tlb_lo(0x4C0, TLB_PERM_XWRU, true);
    tlb_write(other_hi, other_lo, OTHER_IDX);
    isync();

    // Probe an unmapped VPN with no size marker in the PPD field.
    let check_hi = make_tlb_hi_vg(0x4D0, 0, true);
    let result = tlboc(check_hi, EMPTY_PPD_LO);

    // Distinct VPN, so no overlap regardless of how the empty size decodes
    // -- unless the entry has been inflated into a huge page.
    check32!(result, 0x8000_0000);

    // Clean up
    tlb_invalidate(OTHER_IDX);
}

/// The overlap scan must skip invalid entries before decoding their fields.
///
/// `test_ctlbw_ignores_invalid_entries` covers the all-zeroes case, which an
/// implementation can special-case its way past. This parks a non-zero value
/// with V=0 -- what a kernel leaves behind when it uses a spare TLB slot as
/// scratch storage. The value is chosen so that, if its span were consulted,
/// it would cover the whole address space and collide with everything.
fn test_ctlbw_ignores_invalid_nonzero_entries() {
    const GARBAGE_IDX: u32 = 56;
    const NEW_IDX: u32 = 57;

    // V=0 (bit 31 clear) but otherwise non-zero in both words.
    let garbage_hi: u32 = 0x0001_2345;
    tlb_write(garbage_hi, WIDEST_PAGE_LO, GARBAGE_IDX);
    isync();

    // Confirm the slot really does hold what we put there, so a later
    // failure cannot be blamed on the write being dropped.
    let (read_hi, read_lo) = tlb_read(GARBAGE_IDX);
    check32!(read_hi, garbage_hi);
    check32!(read_lo, WIDEST_PAGE_LO);

    // A ctlbw for an unrelated VPN must ignore the invalid slot and succeed.
    let new_hi = make_tlb_hi_vg(0x4E0, 0, true);
    let new_lo = make_tlb_lo(0x4E0, TLB_PERM_XWRU, true);
    let result = ctlbw(new_hi, new_lo, NEW_IDX);
    isync();

    check32!(result, 0x8000_0000);

    // And the entry should actually have landed.
    let (written_hi, written_lo) = tlb_read(NEW_IDX);
    check32!(written_hi, new_hi);
    check32!(written_lo, new_lo);

    // Clean up
    tlb_invalidate(GARBAGE_IDX);
    tlb_invalidate(NEW_IDX);
}

/// Bit 27 of the lo word does not select extended addressing for a JTLB entry.
///
/// Extended (HSV39) addressing belongs to the DMA TLB. In an ordinary JTLB
/// entry bit 27 is part of the cache-attribute field, so setting it must not
/// change how the entry's VPN is interpreted: the entry still describes
/// VA = VPN << 12, and a probe for that VA must find it.
fn test_tlb_bit27_does_not_relocate_jtlb_entry() {
    const IDX: u32 = 58;
    const VPN_1M: u32 = 0x4F0;

    let hi = make_tlb_hi_vg(VPN_1M, 0, true);
    // Same entry the other tests build, plus bit 27.
    let lo = make_tlb_lo(VPN_1M, TLB_PERM_XWRU, true) | (1 << 27);

    tlb_write(hi, lo, IDX);
    isync();

    // Round-trips unchanged.
    let (read_hi, read_lo) = tlb_read(IDX);
    check32!(read_hi, hi);
    check32!(read_lo, lo);

    // tlbp takes the hi word; VA comes from the same VPN field as always.
    // An implementation that reads bit 27 as an addressing-mode selector
    // places this entry at VPN << 20 instead and the probe misses.
    let result = tlb_probe(hi);
    check!(result >= 0);
    check32!(result as u32, IDX);

    // Clean up
    tlb_invalidate(IDX);
}

// ---------------------------------------------------------------------------
// Helper: make 4MB TLB entries
// ---------------------------------------------------------------------------

/// Build a TLB lo word for a 4MB page.
/// For 4MB: PPN[5:0] = 10_0000 (0x20), S=0. Size field = bits [5:0] = 0x20.
fn make_tlb_lo_4m(ppn_1m: u32, perm_bits: u32, cached: bool) -> u32 {
    let cache_attr: u32 = if cached { 0x07 } else { 0x04 };
    ((perm_bits & 0xF) << 28) | (cache_attr << 24) | ((ppn_1m & 0x7FFF) << 9) | 0x20
}

// ---------------------------------------------------------------------------
// TLB overlap RESOLUTION tests — what happens when overlapping entries exist
// and actual memory accesses go through the TLB.
//
// Multi-TLB-match raises IMPRECISE_CAUSE_MULTI_TLB_MATCH (cause 0x44),
// delivered as an NMI (event #1). The translation still uses the first
// (lowest-index) match.
//
// These tests assert that the NMI fires.
// ---------------------------------------------------------------------------

const CAUSE_MULTI_TLB_MATCH: u32 = 0x44;

const SENTINEL_A: u32 = 0xDEAD_BEEF;
const SENTINEL_B: u32 = 0xCAFE_BABE;

/// Physical pages used for overlap resolution testing.
/// PA_A = 0x5000_0000 (VPN 0x500 in 1:1 map)
/// PA_B = 0x6000_0000 (VPN 0x600 in 1:1 map)
/// VA_TEST = 0x7000_0000 (VPN 0x700 — mapped to both PA_A and PA_B)
const PA_A: u32 = 0x5000_0000;
const PA_B: u32 = 0x6000_0000;
const VA_TEST: u32 = 0x7000_0000;
const VPN_TEST: u32 = 0x700;
const PPN_A: u32 = 0x500; // PA_A >> 20
const PPN_B: u32 = 0x600; // PA_B >> 20

/// Load through overlapping TLB entries must raise an NMI.
///
/// Two entries map the same VA to different PAs. A multi-TLB-match
/// raises IMPRECISE_CAUSE_MULTI_TLB_MATCH (0x44) via the NMI vector.
fn test_overlap_load_raises_nmi() {
    const LOW_IDX: u32 = 50;
    const HIGH_IDX: u32 = 58;

    unsafe {
        core::ptr::write_volatile(PA_A as *mut u32, SENTINEL_A);
        core::ptr::write_volatile(PA_B as *mut u32, SENTINEL_B);
    }
    syncht();

    let hi = make_tlb_hi_vg(VPN_TEST, 0, true);
    let lo_a = make_tlb_lo(PPN_A, TLB_PERM_XWRU, false);
    let lo_b = make_tlb_lo(PPN_B, TLB_PERM_XWRU, false);

    reset_nmi_state();
    tlb_write(hi, lo_a, LOW_IDX);
    tlb_write(hi, lo_b, HIGH_IDX);
    isync();

    // This load triggers multi-TLB-match → NMI
    let _val = unsafe { core::ptr::read_volatile(VA_TEST as *const u32) };

    let count = get_nmi_count();
    let cause = get_nmi_cause();
    println!("  nmi_count={} nmi_cause=0x{:02x}", count, cause);

    // The NMI MUST have fired
    check32_ne!(count, 0);
    check32!(cause, CAUSE_MULTI_TLB_MATCH);

    tlb_invalidate(LOW_IDX);
    tlb_invalidate(HIGH_IDX);
}

/// Store through overlapping TLB entries must raise an NMI.
///
/// Same principle as the load test but exercises the store path.
fn test_overlap_store_raises_nmi() {
    const LOW_IDX: u32 = 50;
    const HIGH_IDX: u32 = 58;
    const STORE_VAL: u32 = 0x1234_5678;

    unsafe {
        core::ptr::write_volatile(PA_A as *mut u32, 0);
        core::ptr::write_volatile(PA_B as *mut u32, 0);
    }
    syncht();

    let hi = make_tlb_hi_vg(VPN_TEST, 0, true);
    let lo_a = make_tlb_lo(PPN_A, TLB_PERM_XWRU, false);
    let lo_b = make_tlb_lo(PPN_B, TLB_PERM_XWRU, false);

    reset_nmi_state();
    tlb_write(hi, lo_a, LOW_IDX);
    tlb_write(hi, lo_b, HIGH_IDX);
    isync();

    // This store triggers multi-TLB-match → NMI
    unsafe { core::ptr::write_volatile(VA_TEST as *mut u32, STORE_VAL); }
    syncht();

    let count = get_nmi_count();
    let cause = get_nmi_cause();
    println!("  nmi_count={} nmi_cause=0x{:02x}", count, cause);

    check32_ne!(count, 0);
    check32!(cause, CAUSE_MULTI_TLB_MATCH);

    tlb_invalidate(LOW_IDX);
    tlb_invalidate(HIGH_IDX);
}

/// Verify that the translation uses the lowest-index entry (first match).
///
/// This is a secondary check: the NMI fires AND the load returns data
/// from the lowest-index mapping (as the forward linear scan dictates).
fn test_overlap_resolution_lowest_index_wins() {
    const LOW_IDX: u32 = 50;
    const HIGH_IDX: u32 = 58;

    unsafe {
        core::ptr::write_volatile(PA_A as *mut u32, SENTINEL_A);
        core::ptr::write_volatile(PA_B as *mut u32, SENTINEL_B);
    }
    syncht();

    let hi = make_tlb_hi_vg(VPN_TEST, 0, true);
    let lo_a = make_tlb_lo(PPN_A, TLB_PERM_XWRU, false);
    let lo_b = make_tlb_lo(PPN_B, TLB_PERM_XWRU, false);

    reset_nmi_state();
    // LOW_IDX → PA_A, HIGH_IDX → PA_B
    tlb_write(hi, lo_a, LOW_IDX);
    tlb_write(hi, lo_b, HIGH_IDX);
    isync();

    let val = unsafe { core::ptr::read_volatile(VA_TEST as *const u32) };
    println!("  load=0x{:08x} (expect SENTINEL_A=0x{:08x})", val, SENTINEL_A);

    // NMI must have fired
    check32_ne!(get_nmi_count(), 0);
    // Lowest index wins: idx 50 → PA_A → SENTINEL_A
    check32!(val, SENTINEL_A);

    tlb_invalidate(LOW_IDX);
    tlb_invalidate(HIGH_IDX);
}

/// Same as above with reversed index assignment to confirm it's truly
/// lowest-index, not first-written or content-dependent.
fn test_overlap_resolution_lowest_index_reversed() {
    const LOW_IDX: u32 = 50;
    const HIGH_IDX: u32 = 58;

    unsafe {
        core::ptr::write_volatile(PA_A as *mut u32, SENTINEL_A);
        core::ptr::write_volatile(PA_B as *mut u32, SENTINEL_B);
    }
    syncht();

    let hi = make_tlb_hi_vg(VPN_TEST, 0, true);
    let lo_a = make_tlb_lo(PPN_A, TLB_PERM_XWRU, false);
    let lo_b = make_tlb_lo(PPN_B, TLB_PERM_XWRU, false);

    reset_nmi_state();
    // LOW_IDX → PA_B, HIGH_IDX → PA_A (reversed)
    tlb_write(hi, lo_b, LOW_IDX);
    tlb_write(hi, lo_a, HIGH_IDX);
    isync();

    let val = unsafe { core::ptr::read_volatile(VA_TEST as *const u32) };
    println!("  load=0x{:08x} (expect SENTINEL_B=0x{:08x})", val, SENTINEL_B);

    check32_ne!(get_nmi_count(), 0);
    // Lowest index (50) maps to PA_B → SENTINEL_B
    check32!(val, SENTINEL_B);

    tlb_invalidate(LOW_IDX);
    tlb_invalidate(HIGH_IDX);
}

/// Page-size field decode: the size is encoded by the least-significant set
/// bit of PPD[9:0] (entry bits 9:0).  Bits above that field hold the
/// physical page number, cacheability, and permission bits and must not
/// take part in the decode.
///
/// Probing entries whose PPD[9:0] is zero used to abort QEMU: the decode ran
/// ctz64() over the whole 64-bit entry, so the first set bit found was
/// whatever came next -- the cacheability field, an unrelated PPD bit, or
/// even the VPN/ASID/valid bits -- producing an out-of-range page-size index.
/// Per get_pgsize() in the reference simulator, an all-zero field means the
/// smallest page (4KB), not an error.
fn test_tlb_pgsize_field_decode() {
    // PPD[9:0] == 0, with the cacheability field (bits 27:24) set.  This is
    // the entry that used to trip the assertion.
    let lo_zero_size: u32 = 0x0100_0000; // C=0001, PPN=0, size field 0
    let hi = make_tlb_hi(0x1A0, 0, true);

    tlb_write(hi, lo_zero_size, TEST_TLB_IDX);
    isync();

    // Must read back verbatim -- the entry is stored, not reinterpreted.
    let (rh, rl) = tlb_read(TEST_TLB_IDX);
    check32!(rh, hi);
    check32!(rl, lo_zero_size);

    // Probing must terminate and report a sane result rather than aborting.
    // The entry is valid and global, so it should be found at our index.
    let result = tlb_probe(hi);
    check32!(result as u32, TEST_TLB_IDX);

    tlb_invalidate(TEST_TLB_IDX);

    // Bit 10 is one past the top of the size field, so it must be ignored by
    // the decode and treated as a 4KB page rather than a too-large size.
    let lo_bit10: u32 = 0x0100_0400;
    tlb_write(hi, lo_bit10, TEST_TLB_IDX);
    isync();

    let (rh2, rl2) = tlb_read(TEST_TLB_IDX);
    check32!(rh2, hi);
    check32!(rl2, lo_bit10);

    let result2 = tlb_probe(hi);
    check32!(result2 as u32, TEST_TLB_IDX);

    tlb_invalidate(TEST_TLB_IDX);

    // PPD[23:10] is the real physical page number, not a synthetic test-only
    // field like the cache attribute above -- so this is the case a fuzzer
    // (e.g. the h2 hypervisor's TLB test, which writes random entries and
    // reads them back per the commit that introduced this decode fix) is
    // most likely to produce by chance: a nonzero physical page whose size
    // field happens to be all zero. Bit 23 is the top of PPD, as far from
    // the size field as PPD allows.
    let lo_high_ppn: u32 = 0x0080_0000; // PPD[23] set, size field 0
    tlb_write(hi, lo_high_ppn, TEST_TLB_IDX);
    isync();

    let (rh3, rl3) = tlb_read(TEST_TLB_IDX);
    check32!(rh3, hi);
    check32!(rl3, lo_high_ppn);

    let result3 = tlb_probe(hi);
    check32!(result3 as u32, TEST_TLB_IDX);

    tlb_invalidate(TEST_TLB_IDX);
}

/// Each page-size encoding in PPD[9:0] must probe successfully.  Walks the
/// full 4KB..1GB range that HSV32 supports.
fn test_tlb_pgsize_all_encodings() {
    // Bit i of the size field selects page size 4KB * 4^i, so i in 0..=9
    // covers 4KB through 1GB.
    for i in 0..10 {
        let size_bits: u32 = 1 << i;
        // C=0111 (cacheable WB), PPN=0, size field = size_bits.
        let lo: u32 = (0x7 << 24) | size_bits;
        // Use a high VPN so large pages do not overlap the UART or the
        // runtime's own fixed entries.
        let hi = make_tlb_hi(0x800, 0, true);

        tlb_write(hi, lo, TEST_TLB_IDX);
        isync();

        let (rh, rl) = tlb_read(TEST_TLB_IDX);
        check32!(rh, hi);
        check32!(rl, lo);

        // Probe must find the entry for every legal size encoding.
        let result = tlb_probe(hi);
        check32!(result as u32, TEST_TLB_IDX);

        tlb_invalidate(TEST_TLB_IDX);
    }
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
    run_test("tlb_pgsize_field_decode", test_tlb_pgsize_field_decode);
    run_test("tlb_pgsize_all_encodings", test_tlb_pgsize_all_encodings);

    // Overlap detection tests
    run_test("tlboc_no_overlap", test_tlboc_no_overlap);
    run_test("ctlbw_no_overlap_writes", test_ctlbw_no_overlap_writes);
    run_test("ctlbw_single_overlap_same_vpn_asid", test_ctlbw_single_overlap_same_vpn_asid);
    run_test("tlboc_overlap_global_entry", test_tlboc_overlap_global_entry);
    run_test("tlboc_no_overlap_different_asid", test_tlboc_no_overlap_different_asid);
    run_test("ctlbw_multi_overlap", test_ctlbw_multi_overlap);
    run_test("tlboc_multi_overlap", test_tlboc_multi_overlap);
    run_test("tlboc_overlap_different_page_sizes", test_tlboc_overlap_different_page_sizes);
    run_test("tlboc_no_overlap_different_page_sizes", test_tlboc_no_overlap_different_page_sizes);
    run_test("ctlbw_ignores_invalid_entries", test_ctlbw_ignores_invalid_entries);
    run_test("tlboc_incoming_global_no_bypass", test_tlboc_incoming_global_no_bypass);

    // Page-size field decoding
    run_test(
        "tlboc_empty_ppd_is_not_giant_page",
        test_tlboc_empty_ppd_is_not_giant_page,
    );
    run_test(
        "ctlbw_ignores_invalid_nonzero_entries",
        test_ctlbw_ignores_invalid_nonzero_entries,
    );
    run_test(
        "tlb_bit27_does_not_relocate_jtlb_entry",
        test_tlb_bit27_does_not_relocate_jtlb_entry,
    );

    // Overlap resolution tests — assert multi-TLB-match NMI fires
    run_test("overlap_load_raises_nmi", test_overlap_load_raises_nmi);
    run_test("overlap_store_raises_nmi", test_overlap_store_raises_nmi);
    run_test("overlap_lowest_idx_wins", test_overlap_resolution_lowest_index_wins);
    run_test("overlap_lowest_idx_reversed", test_overlap_resolution_lowest_index_reversed);

    test_suite_end() as i32
}
