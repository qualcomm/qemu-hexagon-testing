// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Instruction TLB misses taken by a packet that straddles a page boundary.
//!
//! A packet may span two pages.  When the page it continues into has no
//! translation, the fetch raises TLBMISSX with cause 0x61
//! (next-page) and ELR must point at the straddling packet itself, not at
//! some earlier packet in the same page.
//!
//! This matters because miss handlers derive the address to map from ELR:
//! the H2 hypervisor, for instance, fills the TLB for ELR + 16 on cause 0x61
//! (16 bytes being the maximum packet size, so ELR + 16 always lands in the
//! page that is missing).  An ELR naming an earlier packet makes the handler
//! map the page that is already present, and the fetch faults forever.
//!
//! BADVA is deliberately not checked.  On hexagon-sim it is not written by
//! either flavour of instruction TLB miss -- it still holds the address of
//! the last data-side fault -- which is also why crt0's own TLBMISSX handler
//! works off ELR while its TLBMISSRW handler works off BADVA.  Cause and ELR
//! are what a miss handler can rely on here.
//!
//! Everything here is Rust apart from the code under test: a run of packets
//! at the tail of one page followed by a 12-byte packet whose last word
//! lives in the next page.  The linker script places that blob at
//! 0x9b800fe0 (see the `.pagecross` output section), far from the image so
//! the pages around it stay unmapped until this test maps them.  The miss
//! handler is a Rust function dispatched from crt0's TLBMISSX vector via
//! `set_tlbmissx_hook()`.

#![no_std]
#![no_main]
#![feature(asm_experimental_arch)]

use core::arch::{asm, global_asm};
use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

/// Page holding the run-up packets and the straddling packet.
const PAGE0: u32 = 0x9b80_0000;
/// Page the straddling packet continues into.
const PAGE1: u32 = PAGE0 + 0x1000;

/// Addresses the `.pagecross` section is expected to land on.
const RUN_UP: u32 = PAGE0 + 0xfe0;
const CROSSING_PKT: u32 = PAGE0 + 0xff8;
const NEXT_PAGE_PKT: u32 = PAGE1 + 0x4;

/// TLB indices below crt0's `TLB_FIXED_ENTRIES` are never recycled by the
/// default miss handler.  Index 0 holds crt0's 1:1 mapping of the image.
const IDX_PAGE0: u32 = 1;
const IDX_PAGE1: u32 = 2;

/// Values the straddling packet writes.  r8/r9/r10 are deliberately outside
/// the duplex register set (r0-r7, r16-r23): a duplex would fold two of the
/// transfers into a single word and the packet would no longer reach across
/// the page boundary.
const VAL_R8: u32 = 0x11;
const VAL_R9: u32 = 0x22;
const VAL_R10: u32 = 0x33;

global_asm!(
    ".section .pagecross, \"ax\", @progbits",
    ".p2align 2",
    // Six single-instruction packets ending at PAGE0 + 0xff8, so the
    // straddling packet is not the first packet of its translation block.
    ".globl pagecross_run_up",
    "pagecross_run_up:",
    "nop",
    "nop",
    "nop",
    "nop",
    "nop",
    "nop",
    // PAGE0 + 0xff8: a 12-byte packet spanning 0xff8 .. 0x1003, so its last
    // word lives in the page that is not mapped yet.
    ".globl pagecross_pkt",
    "pagecross_pkt:",
    "{{ r8 = #0x11; r9 = #0x22; r10 = #0x33 }}",
    // PAGE1 + 0x4: execution has to carry on into the page the handler
    // mapped.
    ".globl pagecross_next_page",
    "pagecross_next_page:",
    "jumpr lr",
    ".text",
);

extern "C" {
    static pagecross_run_up: u8;
    static pagecross_pkt: u8;
    static pagecross_next_page: u8;
}

fn run_up_addr() -> u32 {
    &raw const pagecross_run_up as u32
}

fn pkt_addr() -> u32 {
    &raw const pagecross_pkt as u32
}

fn next_page_addr() -> u32 {
    &raw const pagecross_next_page as u32
}

// ---------------------------------------------------------------------------
// Recorded miss events
// ---------------------------------------------------------------------------

const MAX_EVENTS: usize = 4;

static MISS_COUNT: AtomicU32 = AtomicU32::new(0);
static MISS_CAUSE: [AtomicU32; MAX_EVENTS] = [const { AtomicU32::new(0) }; MAX_EVENTS];
static MISS_ELR: [AtomicU32; MAX_EVENTS] = [const { AtomicU32::new(0) }; MAX_EVENTS];

fn reset_events() {
    MISS_COUNT.store(0, Ordering::Relaxed);
    for i in 0..MAX_EVENTS {
        MISS_CAUSE[i].store(0, Ordering::Relaxed);
        MISS_ELR[i].store(0, Ordering::Relaxed);
    }
}

fn miss_count() -> u32 {
    MISS_COUNT.load(Ordering::Relaxed)
}

/// Instruction TLB miss handler, dispatched from crt0 with SSR.EX still set.
///
/// Fills the missing page the way a real miss handler does, deriving the
/// address to map from ELR alone: on a next-page miss the page that is
/// missing is the one holding ELR + 16, the largest packet being 16 bytes.
extern "C" fn tlbmissx_handler() {
    let n = MISS_COUNT.fetch_add(1, Ordering::Relaxed) as usize;
    let elr = read_elr();
    let cause = read_ssr() & 0xff;
    if n < MAX_EVENTS {
        MISS_CAUSE[n].store(cause, Ordering::Relaxed);
        MISS_ELR[n].store(elr, Ordering::Relaxed);
    }

    let va = if cause == CAUSE_TLBMISSX_NEXTPAGE {
        (elr + 16) & !0xfff
    } else {
        elr & !0xfff
    };

    // A wrong ELR makes the line above name a page that is already present,
    // and the fetch faults again the moment we return.  Escape after a few
    // rounds so the checks below report the failure instead of spinning.
    let va = if n < MAX_EVENTS { va } else { PAGE1 };

    let idx = if va == PAGE0 { IDX_PAGE0 } else { IDX_PAGE1 };
    tlb_write(
        make_tlb_hi_4k(va),
        make_tlb_lo_4k(va, TLB_PERM_XWRU, true),
        idx,
    );
    isync();
}

/// Sentinels planted in caller-saved registers that neither the blob nor
/// the C ABI would leave alone, so that a miss handler which fails to
/// preserve the interrupted context is caught here rather than corrupting
/// some later test at random.
const SENTINELS: [u32; 5] = [0xa5a5_0011, 0x5a5a_0012, 0xa5a5_0013, 0x5a5a_0014, 0xa5a5_0015];

struct PagecrossResult {
    /// What the straddling packet left in r8/r9/r10.
    written: (u32, u32, u32),
    /// r11-r15 as seen after the call.
    sentinels: [u32; 5],
}

/// Call the run-up and let it fall through the straddling packet.
fn call_pagecross() -> PagecrossResult {
    let a: u32;
    let b: u32;
    let c: u32;
    let s0: u32;
    let s1: u32;
    let s2: u32;
    let s3: u32;
    let s4: u32;
    unsafe {
        // LR cannot be named as an inline asm operand, and rustc has no
        // reason to believe this function makes a call, so save it here.
        asm!(
            "{lr_save} = lr",
            "callr {f}",
            "lr = {lr_save}",
            f = in(reg) run_up_addr(),
            lr_save = out(reg) _,
            out("r8") a,
            out("r9") b,
            out("r10") c,
            inout("r11") SENTINELS[0] => s0,
            inout("r12") SENTINELS[1] => s1,
            inout("r13") SENTINELS[2] => s2,
            inout("r14") SENTINELS[3] => s3,
            inout("r15") SENTINELS[4] => s4,
        );
    }
    PagecrossResult {
        written: (a, b, c),
        sentinels: [s0, s1, s2, s3, s4],
    }
}

/// The straddling packet ran to completion and the TLB miss it took left
/// the interrupted thread's registers alone.
fn check_result(r: &PagecrossResult) {
    check32!(r.written.0, VAL_R8);
    check32!(r.written.1, VAL_R9);
    check32!(r.written.2, VAL_R10);
    for i in 0..SENTINELS.len() {
        check32!(r.sentinels[i], SENTINELS[i]);
    }
}

fn map_page(va: u32, idx: u32) {
    tlb_write(
        make_tlb_hi_4k(va),
        make_tlb_lo_4k(va, TLB_PERM_XWRU, true),
        idx,
    );
    isync();
}

fn unmap_pages() {
    tlb_invalidate(IDX_PAGE0);
    tlb_invalidate(IDX_PAGE1);
    // The pages have been executed from, so drop any instruction cache
    // lines that could satisfy a fetch without consulting the TLB.
    ickill();
    isync();
}

/// The blob has to land exactly on the page boundary or the rest of this
/// file tests nothing.
fn test_pagecross_layout() {
    check32!(run_up_addr(), RUN_UP);
    check32!(pkt_addr(), CROSSING_PKT);
    check32!(next_page_addr(), NEXT_PAGE_PKT);
    // The straddling packet is 12 bytes and its last word is in PAGE1.
    check32!(next_page_addr() - pkt_addr(), 12);
    check!(pkt_addr() + 8 >= PAGE1);
}

/// PAGE0 mapped, PAGE1 missing: fetching the straddling packet raises a
/// next-page TLBMISSX naming the straddling packet, and one round through
/// the handler is enough.
fn test_pagecross_next_page_miss() {
    unmap_pages();
    map_page(PAGE0, IDX_PAGE0);
    // Nothing may translate PAGE1 yet, or the fetch never faults.
    check!(tlb_probe(make_tlb_hi_4k(PAGE1)) < 0);

    reset_events();
    set_tlbmissx_hook(Some(tlbmissx_handler));
    let result = call_pagecross();
    set_tlbmissx_hook(None);

    // Exactly one miss, and the handler's ELR-derived mapping resolved it.
    check32!(miss_count(), 1);
    check32!(MISS_CAUSE[0].load(Ordering::Relaxed), CAUSE_TLBMISSX_NEXTPAGE);
    // ELR names the straddling packet, not an earlier packet in the page.
    check32!(MISS_ELR[0].load(Ordering::Relaxed), CROSSING_PKT);

    // The straddling packet committed all three writes, and execution
    // carried on into the newly mapped page.
    check_result(&result);

    unmap_pages();
}

/// Both pages missing: the plain fetch miss on PAGE0 comes first with cause
/// 0x60 and ELR at the run-up, then the straddling packet takes the
/// next-page miss.  Confirms the two causes are distinguishable and that
/// the ELR checked above is not simply the first packet of the block.
fn test_pagecross_both_pages_miss() {
    unmap_pages();
    check!(tlb_probe(make_tlb_hi_4k(PAGE0)) < 0);
    check!(tlb_probe(make_tlb_hi_4k(PAGE1)) < 0);

    reset_events();
    set_tlbmissx_hook(Some(tlbmissx_handler));
    let result = call_pagecross();
    set_tlbmissx_hook(None);

    check32!(miss_count(), 2);

    check32!(MISS_CAUSE[0].load(Ordering::Relaxed), CAUSE_TLBMISSX_NORMAL);
    check32!(MISS_ELR[0].load(Ordering::Relaxed), RUN_UP);

    check32!(MISS_CAUSE[1].load(Ordering::Relaxed), CAUSE_TLBMISSX_NEXTPAGE);
    check32!(MISS_ELR[1].load(Ordering::Relaxed), CROSSING_PKT);

    check_result(&result);

    unmap_pages();
}

/// The hook is only consulted while it is installed: with it removed, the
/// same fetch is served by crt0's default miss handler.
fn test_default_handler_still_works() {
    unmap_pages();
    reset_events();

    let result = call_pagecross();

    check32!(miss_count(), 0);
    check_result(&result);

    unmap_pages();
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Page-crossing TLBMISSX");

    run_test("pagecross_layout", test_pagecross_layout);
    run_test("pagecross_next_page_miss", test_pagecross_next_page_miss);
    run_test("pagecross_both_pages_miss", test_pagecross_both_pages_miss);
    run_test("default_handler_still_works", test_default_handler_still_works);

    test_suite_end() as i32
}
