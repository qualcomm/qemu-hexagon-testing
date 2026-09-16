// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Monitor/Guest virtualization register tests for Hexagon v81.
//!
//! Tests guest mode SSR bits, CCR configuration, and virtualization
//! control registers accessible from monitor (supervisor) mode.

#![no_std]
#![no_main]
#![feature(asm_experimental_arch)]

use core::arch::{asm, global_asm};
use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

const GSR_UM: u32 = 1 << 31;
const CAUSE_PRIV_INSN_IN_GUEST: u32 = 0x1b;

#[no_mangle]
static DTG_TRAP0_HIT: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static DTG_ERROR_HIT: AtomicU32 = AtomicU32::new(0);
static VMGETIE_RESULT: AtomicU32 = AtomicU32::new(0);
static VMSETIE_OLD: AtomicU32 = AtomicU32::new(0);
static VMSPSWAP_RESULT: AtomicU32 = AtomicU32::new(0);

global_asm!(
    r#"
    .p2align 8
    .global dtg_event_vectors
dtg_event_vectors:
    jump .
    jump .
    jump dtg_error_handler
    jump .
    jump .
    jump .
    jump .
    jump .
    jump dtg_trap0_handler

dtg_error_handler:
    r0 = #1
    memw(##DTG_ERROR_HIT) = r0
    r0 = gelr
    r0 = add(r0, #4)
    gelr = r0
    trap1(r0, #1)

dtg_trap0_handler:
    r0 = #1
    memw(##DTG_TRAP0_HIT) = r0
    trap1(r0, #0)
    jumpr r31

    .p2align 8
    .global dtg_monitor_vectors
dtg_monitor_vectors:
    jump .
    jump .
    jump .
    jump .
    jump .
    jump .
    jump .
    jump .
    jump .
    jump dtg_monitor_trap1_handler

dtg_monitor_trap1_handler:
    r0 = ccr
    r0 = clrbit(r0, #25)
    ccr = r0
    r0 = ssr
    r0 = clrbit(r0, #16)
    r0 = clrbit(r0, #19)
    ssr = r0
    isync
    rte
"#
);

extern "C" {
    static dtg_event_vectors: u8;
    static dtg_monitor_vectors: u8;
}

fn read_gevb() -> u32 {
    let result: u32;

    unsafe {
        asm!("{result} = s11", result = out(reg) result, options(nostack));
    }
    result
}

fn write_gevb(value: u32) {
    unsafe {
        asm!("s11 = {value}", value = in(reg) value, options(nostack));
    }
}

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

/// Trampoline run in user mode by `enter_user_mode()`: attempts a
/// supervisor-only (A_PRIV) register read -- `modectl` -- which must fault.
/// Put in its own packet so the exception handler's ELR+4 advance lands
/// cleanly on the following instruction, matching the pattern used in
/// test_hvx_context's XE-disable test.
fn priv_insn_probe() {
    unsafe {
        asm!("{{ {0} = modectl }}", out(reg) _, options(nostack));
    }
    exit_user_mode();
}

/// A supervisor-only (A_PRIV) instruction executed from user mode must
/// fault with cause PRIV_INSN_IN_USER (SSR:CAUSE 0x1b), not silently
/// succeed or fault with some other cause.
fn test_priv_insn_faults_in_user_mode() {
    reset_exception_state();
    enter_user_mode(priv_insn_probe);
    check32!(get_exception_count(), 1);
    check32!(get_exception_cause(), CAUSE_PRIV_INSN_IN_USER);
    // We're back in supervisor mode (exit_user_mode ran before the
    // fault path even had a chance not to, but confirm anyway).
    check!(read_ssr() & SSR_UM == 0);
}

/// Trampoline run in user mode: attempts a guest-register (A_GUEST) read
/// -- `gelr` -- which requires monitor-or-guest privilege and must fault
/// from plain user mode.
fn guest_insn_probe() {
    unsafe {
        asm!("{{ {0} = gelr }}", out(reg) _, options(nostack));
    }
    exit_user_mode();
}

/// A guest-register (A_GUEST) instruction executed from user mode must
/// fault with cause GUEST_INSN_IN_USER (SSR:CAUSE 0x1a).
fn test_guest_insn_faults_in_user_mode() {
    reset_exception_state();
    enter_user_mode(guest_insn_probe);
    check32!(get_exception_count(), 1);
    check32!(get_exception_cause(), CAUSE_GUEST_INSN_IN_USER);
    check!(read_ssr() & SSR_UM == 0);
}

/// Trampoline run in guest mode by `enter_guest_mode()`: the same
/// supervisor-only (A_PRIV) register read as `priv_insn_probe`, which must
/// fault even though guest mode is one step up from plain user mode --
/// A_PRIV requires true monitor privilege, guest mode is not enough.
fn priv_insn_probe_guest() {
    unsafe {
        asm!("{{ {0} = modectl }}", out(reg) _, options(nostack));
    }
    exit_user_mode();
}

/// A supervisor-only (A_PRIV) instruction executed from guest mode must
/// fault with cause PRIV_INSN_IN_USER (SSR:CAUSE 0x1b), same as from user
/// mode: guest mode does not grant monitor-only privileges.
fn test_priv_insn_faults_in_guest_mode() {
    reset_exception_state();
    enter_guest_mode(priv_insn_probe_guest);
    check32!(get_exception_count(), 1);
    check32!(get_exception_cause(), CAUSE_PRIV_INSN_IN_USER);
    // Back in supervisor mode: both UM and GM clear.
    check!(read_ssr() & (SSR_UM | SSR_GM) == 0);
}

/// Trampoline run in guest mode: the same guest-register (A_GUEST) read as
/// `guest_insn_probe`. Unlike from user mode, this must NOT fault -- guest
/// mode is sufficient privilege for A_GUEST instructions.
fn guest_insn_probe_guest() {
    unsafe {
        asm!("{{ {0} = gelr }}", out(reg) _, options(nostack));
    }
    exit_user_mode();
}

/// A guest-register (A_GUEST) instruction executed from guest mode must be
/// permitted: no exception at all, unlike the user-mode case above.
fn test_guest_insn_allowed_in_guest_mode() {
    reset_exception_state();
    enter_guest_mode(guest_insn_probe_guest);
    check32!(get_exception_count(), 0);
    check!(read_ssr() & (SSR_UM | SSR_GM) == 0);
}

fn guest_virtual_instructions() {
    let mut value: u32 = 0;

    unsafe {
        asm!("trap1(r0, #4)", inout("r0") value, options(nostack));
    }
    VMGETIE_RESULT.store(value, Ordering::SeqCst);

    value = 1;
    unsafe {
        asm!("trap1(r0, #3)", inout("r0") value, options(nostack));
    }
    VMSETIE_OLD.store(value, Ordering::SeqCst);

    value = 0x1234_5678;
    unsafe {
        asm!("trap1(r0, #6)", inout("r0") value, options(nostack));
    }
    VMSPSWAP_RESULT.store(value, Ordering::SeqCst);
    exit_user_mode();
}

/// GRE enables virtual Trap1 instructions in Guest mode.
fn test_guest_virtual_instructions() {
    let saved_ccr = read_ccr();
    let saved_gsr = read_gsr();
    let saved_gosp = read_gosp();

    write_ccr((saved_ccr & !CCR_GIE) | CCR_GRE);
    // VMSPSWAP swaps only when GSR.UM is set.
    write_gsr(GSR_UM);
    write_gosp(0x89ab_cdef);
    isync();
    enter_guest_mode(guest_virtual_instructions);

    check32!(VMGETIE_RESULT.load(Ordering::SeqCst), 0);
    check32!(VMSETIE_OLD.load(Ordering::SeqCst), 0);
    check32!(VMSPSWAP_RESULT.load(Ordering::SeqCst), 0x89ab_cdef);
    check32!(read_gosp(), 0x1234_5678);

    write_gosp(saved_gosp);
    write_gsr(saved_gsr);
    write_ccr(saved_ccr);
    isync();
}

fn guest_trap0() {
    unsafe {
        asm!("trap0(#2)", options(nostack));
    }
    exit_user_mode();
}

/// GTE vectors Trap0 directly to GEVB.
fn test_direct_guest_trap0() {
    let saved_ccr = read_ccr();
    let saved_gevb = read_gevb();
    let saved_evb = read_evb();

    DTG_TRAP0_HIT.store(0, Ordering::SeqCst);
    reset_exception_state();
    write_gevb(unsafe { &dtg_event_vectors as *const u8 as u32 });
    write_evb(unsafe { &dtg_monitor_vectors as *const u8 as u32 });
    write_ccr(saved_ccr | CCR_GTE | CCR_GRE);
    isync();
    enter_guest_mode(guest_trap0);

    check32!(DTG_TRAP0_HIT.load(Ordering::SeqCst), 1);

    write_evb(saved_evb);
    write_gevb(saved_gevb);
    write_ccr(saved_ccr);
    isync();
}

fn guest_privileged_read() {
    unsafe {
        asm!("{value} = modectl", value = out(reg) _, options(nostack));
    }
    exit_user_mode();
}

/// GEE vectors supported errors to GEVB; VMRTE resumes.
fn test_direct_guest_error() {
    let saved_ccr = read_ccr();
    let saved_gevb = read_gevb();

    DTG_ERROR_HIT.store(0, Ordering::SeqCst);
    reset_exception_state();
    write_gevb(unsafe { &dtg_event_vectors as *const u8 as u32 });
    write_ccr(saved_ccr | CCR_GEE | CCR_GRE);
    isync();
    enter_guest_mode(guest_privileged_read);

    check32!(DTG_ERROR_HIT.load(Ordering::SeqCst), 1);
    check32!(get_exception_count(), 0);
    check32!(read_gsr() & 0xffff, CAUSE_PRIV_INSN_IN_GUEST);

    write_gevb(saved_gevb);
    write_ccr(saved_ccr);
    isync();
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
    run_test(
        "priv_insn_faults_in_user_mode",
        test_priv_insn_faults_in_user_mode,
    );
    run_test(
        "guest_insn_faults_in_user_mode",
        test_guest_insn_faults_in_user_mode,
    );
    run_test(
        "priv_insn_faults_in_guest_mode",
        test_priv_insn_faults_in_guest_mode,
    );
    run_test(
        "guest_insn_allowed_in_guest_mode",
        test_guest_insn_allowed_in_guest_mode,
    );
    run_test("ccr_vv1_bit", test_ccr_vv1_bit);
    run_test(
        "guest_virtual_instructions",
        test_guest_virtual_instructions,
    );
    run_test("direct_guest_trap0", test_direct_guest_trap0);
    run_test("direct_guest_error", test_direct_guest_error);

    test_suite_end() as i32
}
