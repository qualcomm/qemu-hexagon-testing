// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! FRAMELIMIT (C16) register behavior in monitor, guest and user modes.
//!
//! FRAMELIMIT is readable and writable at every privilege level.
//! `allocframe(#N)` computes EA = SP-8 and new SP = EA-N.  In user or guest
//! mode, EA-N < FRAMELIMIT (unsigned) raises a precise stack-overflow
//! exception (cause 0x27) with BADVA set to N.  Monitor mode is not checked,
//! and EA-N == FRAMELIMIT is not an overflow.

#![no_std]
#![no_main]
#![feature(asm_experimental_arch)]

use core::arch::global_asm;
use core::sync::atomic::{AtomicU32, Ordering::SeqCst};
use hexagon_arch_tests::*;

const CAUSE_STACK_LIMIT: u32 = 0x27;

#[no_mangle]
static FL_ARG: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_ABS: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_SP_BEFORE: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_SP_AFTER: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_FP_BEFORE: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_FP_AFTER: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_RB: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_VARIANT: AtomicU32 = AtomicU32::new(0);
#[no_mangle]
static FL_PATTERN: AtomicU32 = AtomicU32::new(0);

// Defines a probe function that sets FRAMELIMIT, runs one allocframe form in
// its own packet, records SP/FP before and after, then restores SP/FP and
// FRAMELIMIT.
//   FL_ABS == 0: limit = (SP - 8 - size) + (int)FL_ARG
//   FL_ABS != 0: limit = FL_ARG
// The pieces are joined by concat!, so `{{`/`}}` are literal packet braces
// and `$insn` must not contain braces.
macro_rules! define_probe {
    ($name:literal, $pkt:literal, $insn:literal, $tot:expr) => {
        global_asm!(
            concat!(
                "    .text\n    .p2align 4\n    .global ", $name, "\n",
                "    .type ", $name, ", @function\n",
                $name, ":\n",
                r#"
    r0 = memw(##FL_ARG)
    r1 = memw(##FL_ABS)
    r4 = r29
    r5 = r30
    r6 = framelimit
    r2 = add(r29, #{tot})
    r2 = add(r2, r0)
    p0 = cmp.eq(r1, #0)
    r2 = mux(p0, r2, r0)
    framelimit = r2
"#, $pkt, r#":
    {{
        "#, $insn, r#"
    }}
    memw(##FL_SP_AFTER) = r29
    memw(##FL_FP_AFTER) = r30
    memw(##FL_SP_BEFORE) = r4
    memw(##FL_FP_BEFORE) = r5
    r29 = r4
    r30 = r5
    framelimit = r6
    jumpr r31
"#,
                "    .global ", $pkt, "\n    .size ", $name, ", . - ", $name, "\n"
            ),
            tot = const $tot,
        );
    };
}

define_probe!("fl_probe", "fl_probe_pkt", "allocframe(r29, #64):raw", -72);
define_probe!("fl_probe_sp", "fl_probe_sp_pkt", "allocframe(#64)", -72);
define_probe!(
    "fl_probe_big",
    "fl_probe_big_pkt",
    "allocframe(r29, #2040):raw",
    -2048
);

global_asm!(
    r#"
    .text
    .p2align 4
    .global fl_rw
    .type fl_rw, @function
fl_rw:
    r0 = memw(##FL_PATTERN)
    r6 = framelimit
    framelimit = r0
    r1 = framelimit
    memw(##FL_RB) = r1
    framelimit = r6
    jumpr r31
    .size fl_rw, . - fl_rw
"#
);

extern "C" {
    fn fl_probe();
    fn fl_probe_sp();
    fn fl_probe_big();
    static fl_probe_pkt: u8;
    static fl_probe_sp_pkt: u8;
    static fl_probe_big_pkt: u8;
    fn fl_rw();
}

#[derive(Clone, Copy, PartialEq)]
enum Mode {
    Monitor,
    Guest,
    User,
}

/// Allocation size per FL_VARIANT:
///   0: allocframe(Rx,#64):raw   1: allocframe(#64)   2: allocframe(Rx,#2040):raw
const VARIANT_SIZES: [u32; 3] = [64, 64, 2040];

fn run_probe_fn() {
    unsafe {
        match FL_VARIANT.load(SeqCst) {
            0 => fl_probe(),
            1 => fl_probe_sp(),
            _ => fl_probe_big(),
        }
    }
}

fn variant_size() -> u32 {
    VARIANT_SIZES[FL_VARIANT.load(SeqCst) as usize]
}

fn variant_pkt() -> u32 {
    unsafe {
        match FL_VARIANT.load(SeqCst) {
            0 => &raw const fl_probe_pkt as u32,
            1 => &raw const fl_probe_sp_pkt as u32,
            _ => &raw const fl_probe_big_pkt as u32,
        }
    }
}

// Guest/user trampolines can't read SSR, so they just exit via trap0(#1).
fn tramp_probe_lowpriv() {
    run_probe_fn();
    exit_user_mode();
}

fn tramp_rw_lowpriv() {
    unsafe { fl_rw() };
    exit_user_mode();
}

fn tramp_probe_monitor() {
    run_probe_fn();
}

fn tramp_rw_monitor() {
    unsafe { fl_rw() };
}

fn run_in(mode: Mode, mon: fn(), low: fn()) {
    match mode {
        Mode::Monitor => mon(),
        Mode::Guest => enter_guest_mode(low),
        Mode::User => enter_user_mode(low),
    }
    check!(read_ssr() & (SSR_UM | SSR_GM) == 0);
}

fn probe(mode: Mode, arg: u32, abs: bool) {
    reset_exception_state();
    FL_ARG.store(arg, SeqCst);
    FL_ABS.store(abs as u32, SeqCst);
    run_in(mode, tramp_probe_monitor, tramp_probe_lowpriv);
}

fn expect_completed() {
    check32!(get_exception_count(), 0);
    let sp = FL_SP_BEFORE.load(SeqCst);
    check32!(
        FL_SP_AFTER.load(SeqCst),
        sp.wrapping_sub(8 + variant_size())
    );
    check32!(FL_FP_AFTER.load(SeqCst), sp.wrapping_sub(8));
}

fn expect_overflow() {
    check32!(get_exception_count(), 1);
    check32!(get_exception_cause(), CAUSE_STACK_LIMIT);
    check32!(get_exception_badva(), variant_size());
    check32!(get_exception_elr(), variant_pkt());
    // Precise: allocframe must leave SP/FP untouched.
    check32!(FL_SP_AFTER.load(SeqCst), FL_SP_BEFORE.load(SeqCst));
    check32!(FL_FP_AFTER.load(SeqCst), FL_FP_BEFORE.load(SeqCst));
}

fn rw_check(mode: Mode) {
    for &p in &[
        0x0000_0000u32,
        0xA5A5_5A5A,
        0x5A5A_A5A5,
        0xFFFF_FFFF,
        0x1234_5678,
    ] {
        FL_PATTERN.store(p, SeqCst);
        FL_RB.store(!p, SeqCst);
        run_in(mode, tramp_rw_monitor, tramp_rw_lowpriv);
        check32!(FL_RB.load(SeqCst), p);
    }
}

fn limit_check(mode: Mode, enforced: bool) {
    for v in 0..VARIANT_SIZES.len() as u32 {
        FL_VARIANT.store(v, SeqCst);
        limit_check_variant(mode, enforced);
    }
}

fn limit_check_variant(mode: Mode, enforced: bool) {
    // limit == EA-N
    probe(mode, 0, false);
    expect_completed();
    // limit below new SP
    probe(mode, (-1i32) as u32, false);
    expect_completed();
    // limit one byte above new SP
    probe(mode, 1, false);
    if enforced {
        expect_overflow();
    } else {
        expect_completed();
    }
    // limit 0
    probe(mode, 0, true);
    expect_completed();
    // limit 0xFFFFFFFF
    probe(mode, 0xFFFF_FFFF, true);
    if enforced {
        expect_overflow();
    } else {
        expect_completed();
    }
}

fn test_rw_monitor() {
    rw_check(Mode::Monitor);
}
fn test_rw_guest() {
    rw_check(Mode::Guest);
}
fn test_rw_user() {
    rw_check(Mode::User);
}
fn test_limit_monitor() {
    limit_check(Mode::Monitor, false);
}
fn test_limit_guest() {
    limit_check(Mode::Guest, true);
}
fn test_limit_user() {
    limit_check(Mode::User, true);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("FRAMELIMIT");

    run_test("framelimit_rw_monitor", test_rw_monitor);
    run_test("framelimit_rw_guest", test_rw_guest);
    run_test("framelimit_rw_user", test_rw_user);
    run_test("framelimit_check_monitor", test_limit_monitor);
    run_test("framelimit_check_guest", test_limit_guest);
    run_test("framelimit_check_user", test_limit_user);

    test_suite_end() as i32
}
