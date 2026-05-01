// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Bare-metal Hexagon system test runtime library.
//!
//! Provides a test harness, semihosting output, system register accessors,
//! interrupt/TLB/cache helpers, and privilege mode transitions for writing
//! standalone Hexagon system architecture verification tests.

#![no_std]
#![feature(asm_experimental_arch)]

use core::arch::asm;
use core::fmt;
use core::panic::PanicInfo;
use core::sync::atomic::{AtomicU32, Ordering};

// ---------------------------------------------------------------------------
// SSR bit positions
// ---------------------------------------------------------------------------
pub const SSR_CAUSE_SHIFT: u32 = 0;
pub const SSR_ASID_SHIFT: u32 = 8;
pub const SSR_UM_BIT: u32 = 16;
pub const SSR_EX_BIT: u32 = 17;
pub const SSR_IE_BIT: u32 = 18;
pub const SSR_GM_BIT: u32 = 19;
pub const SSR_CE_BIT: u32 = 23;
pub const SSR_PE_BIT: u32 = 24;
pub const SSR_SS_BIT: u32 = 30;
pub const SSR_XE_BIT: u32 = 31;

pub const SSR_UM: u32 = 1 << SSR_UM_BIT;
pub const SSR_EX: u32 = 1 << SSR_EX_BIT;
pub const SSR_IE: u32 = 1 << SSR_IE_BIT;
pub const SSR_GM: u32 = 1 << SSR_GM_BIT;
pub const SSR_CE: u32 = 1 << SSR_CE_BIT;
pub const SSR_PE: u32 = 1 << SSR_PE_BIT;
pub const SSR_XA_SHIFT: u32 = 28;
pub const SSR_XA_MASK: u32 = 0x7 << SSR_XA_SHIFT; // bits 30:28
pub const SSR_SS: u32 = 1 << SSR_SS_BIT;
pub const SSR_XE: u32 = 1 << SSR_XE_BIT;

// ---------------------------------------------------------------------------
// SYSCFG bit positions
// ---------------------------------------------------------------------------
pub const SYSCFG_MMU_EN: u32 = 1 << 0;
pub const SYSCFG_ICACHE_EN: u32 = 1 << 1;
pub const SYSCFG_DCACHE_EN: u32 = 1 << 2;
pub const SYSCFG_INT_EN: u32 = 1 << 4;
pub const SYSCFG_PCYCLE_EN: u32 = 1 << 6;

// ---------------------------------------------------------------------------
// STID field positions
// ---------------------------------------------------------------------------
pub const STID_PRIO_SHIFT: u32 = 16;
pub const STID_PRIO_MASK: u32 = 0x00FF_0000;

// ---------------------------------------------------------------------------
// SCHEDCFG field positions
// ---------------------------------------------------------------------------
pub const SCHEDCFG_EN: u32 = 1 << 8;

// ---------------------------------------------------------------------------
// Config table field offsets (cfgbase << 5 + offset via memw_phys)
// ---------------------------------------------------------------------------
pub const CFGTABLE_SUBSYSTEM_BASE: u32 = 0x08;
pub const CFGTABLE_COPROC_PRESENT: u32 = 0x30;
pub const CFGTABLE_EXT_CONTEXTS: u32 = 0x34;
pub const CFGTABLE_THREAD_ENABLE_MASK: u32 = 0x48;

// ---------------------------------------------------------------------------
// QTimer device constants
// ---------------------------------------------------------------------------
pub const QTIMER_DEVICE_OFFSET: u32 = 0x2_0000; // from subsystem base
pub const QTIMER_FRAME_OFFSET: u32 = 0x1000; // timer frame within QTimer
pub const QTIMER_VERSION_OFFSET: u32 = 0xfd0; // version reg within frame
pub const QTIMER_EXPECTED_VERSION: u32 = 0x2002_0000;

// ---------------------------------------------------------------------------
// L2VIC register offsets (from l2vic.h)
// ---------------------------------------------------------------------------
pub const L2VIC_INT_ENABLE: u32 = 0x100;
pub const L2VIC_INT_ENABLE_CLR: u32 = 0x180;
pub const L2VIC_INT_ENABLE_SET: u32 = 0x200;
pub const L2VIC_INT_TYPE: u32 = 0x280;
pub const L2VIC_INT_STATUS: u32 = 0x380;
pub const L2VIC_INT_CLEAR: u32 = 0x400;
pub const L2VIC_SOFT_INT: u32 = 0x480;
pub const L2VIC_INT_PENDING: u32 = 0x500;

// ---------------------------------------------------------------------------
// CCR bit positions
// ---------------------------------------------------------------------------
pub const CCR_GIE_BIT: u32 = 24;
pub const CCR_GTE_BIT: u32 = 25;
pub const CCR_GEE_BIT: u32 = 26;
pub const CCR_GRE_BIT: u32 = 27;
pub const CCR_VV1_BIT: u32 = 29;

pub const CCR_GIE: u32 = 1 << CCR_GIE_BIT;
pub const CCR_GTE: u32 = 1 << CCR_GTE_BIT;
pub const CCR_GEE: u32 = 1 << CCR_GEE_BIT;
pub const CCR_GRE: u32 = 1 << CCR_GRE_BIT;
pub const CCR_VV1: u32 = 1 << CCR_VV1_BIT;

// ---------------------------------------------------------------------------
// Exception cause codes
// ---------------------------------------------------------------------------
pub const CAUSE_PRECISE_BUS_ERROR: u32 = 0x01;
pub const CAUSE_NPC_ERROR: u32 = 0x02;
pub const CAUSE_PRIV_NO_UREAD: u32 = 0x24;
pub const CAUSE_PRIV_NO_UWRITE: u32 = 0x25;
pub const CAUSE_PRIV_NO_UXEC: u32 = 0x14;
pub const CAUSE_PRIV_INSN_IN_USER: u32 = 0x1B;
pub const CAUSE_NO_COPROC_ENABLE: u32 = 0x16;

// ---------------------------------------------------------------------------
// TLB entry helpers
// ---------------------------------------------------------------------------
pub const TLB_FIXED_ENTRIES: u32 = 6;

/// Build a TLB hi word (matches crt0.S TLB miss handler format).
/// VPN is the 1MB page number (VA >> 20), placed at bits [19:8].
/// ASID in bits [7:1], global bit at [0].
/// Top bits [31:30] = 0b11 (1MB page size encoding).
pub fn make_tlb_hi(vpn_1m: u32, asid: u32, global: bool) -> u32 {
    0xC000_0000 | ((vpn_1m & 0xFFF) << 8) | ((asid & 0x7F) << 1) | (global as u32)
}

/// Build a TLB lo word for a 1MB page.
/// Format: [31:28]=XWRU perms, [27:24]=CCC cache attr, [23:9]=PPN, [4:0]=0x10 (1MB size).
pub fn make_tlb_lo(ppn_1m: u32, perm_bits: u32, cached: bool) -> u32 {
    let cache_attr: u32 = if cached { 0x07 } else { 0x04 };
    ((perm_bits & 0xF) << 28) | (cache_attr << 24) | ((ppn_1m & 0x7FFF) << 9) | 0x10
}

/// PTE permission bits (encoded in TLB lo word via ppn_field)
pub const TLB_PERM_XWRU: u32 = 0xF; // All permissions
pub const TLB_PERM_XWR: u32 = 0xE; // Supervisor only (no User)
pub const TLB_PERM_RU: u32 = 0x3; // Read + User (no write/exec)
pub const TLB_PERM_WRU: u32 = 0x7; // Write + Read + User (no exec)

// ---------------------------------------------------------------------------
// Error tracking
// ---------------------------------------------------------------------------

/// Global error counter. Each failed check increments this.
static ERR_COUNT: AtomicU32 = AtomicU32::new(0);

/// Increment the error counter and return the new value.
pub fn record_error() -> u32 {
    ERR_COUNT.fetch_add(1, Ordering::Relaxed) + 1
}

/// Get the current error count.
pub fn error_count() -> u32 {
    ERR_COUNT.load(Ordering::Relaxed)
}

/// Reset the error counter to zero.
pub fn reset_errors() {
    ERR_COUNT.store(0, Ordering::Relaxed);
}

// ---------------------------------------------------------------------------
// Console output — semihosting (default) or UART (feature = "uart")
// ---------------------------------------------------------------------------

/// Write a single byte to the simulator console via semihosting.
#[cfg(not(feature = "uart"))]
#[inline(never)]
pub fn putc(c: u8) {
    let buf: [u8; 1] = [c];
    unsafe {
        asm!(
            "r0 = #3",        // SYS_WRITEC
            "r1 = {addr}",
            "trap0(#0)",
            addr = in(reg) buf.as_ptr(),
            out("r0") _,
            out("r1") _,
        );
    }
}

/// UART transmit data register address.
/// Set this to match your target machine's UART base.
#[cfg(feature = "uart")]
pub const UART_TX: u32 = 0x1000_0000;

/// Write a single byte to the console via UART MMIO.
#[cfg(feature = "uart")]
#[inline(never)]
pub fn putc(c: u8) {
    unsafe {
        core::ptr::write_volatile(UART_TX as *mut u32, c as u32);
    }
}

/// Write a string to the simulator console character by character.
#[inline(never)]
pub fn puts(s: &str) {
    for &b in s.as_bytes() {
        putc(b);
    }
}

/// Write a string followed by a newline.
pub fn println(s: &str) {
    puts(s);
    putc(b'\n');
}

/// A writer that sends output to the semihosting console.
pub struct SemihostWriter;

impl fmt::Write for SemihostWriter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        puts(s);
        Ok(())
    }
}

/// Print formatted output to the semihosting console.
#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::SemihostWriter, $($arg)*);
    }};
}

/// Print formatted output with a trailing newline.
#[macro_export]
macro_rules! println {
    () => { $crate::putc(b'\n') };
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::SemihostWriter, $($arg)*);
        $crate::putc(b'\n');
    }};
}

// ---------------------------------------------------------------------------
// Check macros
// ---------------------------------------------------------------------------

/// Check that a 32-bit value matches expected. Logs error on mismatch.
#[macro_export]
macro_rules! check32 {
    ($val:expr, $exp:expr) => {{
        let val: u32 = $val;
        let exp: u32 = $exp;
        if val != exp {
            $crate::println!(
                "FAIL at {}:{}: got 0x{:08x}, expected 0x{:08x}",
                file!(),
                line!(),
                val,
                exp
            );
            $crate::record_error();
        }
    }};
}

/// Check that a 64-bit value matches expected.
#[macro_export]
macro_rules! check64 {
    ($val:expr, $exp:expr) => {{
        let val: u64 = $val;
        let exp: u64 = $exp;
        if val != exp {
            $crate::println!(
                "FAIL at {}:{}: got 0x{:016x}, expected 0x{:016x}",
                file!(),
                line!(),
                val,
                exp
            );
            $crate::record_error();
        }
    }};
}

/// Check that a 32-bit value is within a range [min, max].
#[macro_export]
macro_rules! check32_range {
    ($val:expr, $min:expr, $max:expr) => {{
        let val: u32 = $val;
        let min: u32 = $min;
        let max: u32 = $max;
        if val < min || val > max {
            $crate::println!(
                "FAIL at {}:{}: 0x{:08x} not in [0x{:08x}, 0x{:08x}]",
                file!(),
                line!(),
                val,
                min,
                max
            );
            $crate::record_error();
        }
    }};
}

/// Check that a 32-bit value does NOT equal another.
#[macro_export]
macro_rules! check32_ne {
    ($val:expr, $ne:expr) => {{
        let val: u32 = $val;
        let ne: u32 = $ne;
        if val == ne {
            $crate::println!(
                "FAIL at {}:{}: got 0x{:08x}, expected != 0x{:08x}",
                file!(),
                line!(),
                val,
                ne
            );
            $crate::record_error();
        }
    }};
}

/// Check a boolean condition.
#[macro_export]
macro_rules! check {
    ($cond:expr) => {{
        if !$cond {
            $crate::println!(
                "FAIL at {}:{}: condition '{}' was false",
                file!(),
                line!(),
                stringify!($cond)
            );
            $crate::record_error();
        }
    }};
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

/// Run a named test function and report results.
pub fn run_test(name: &str, f: fn()) {
    print!("  {} ... ", name);
    let before = error_count();
    f();
    let after = error_count();
    if after == before {
        println!("ok");
    } else {
        println!("FAILED ({} errors)", after - before);
    }
}

/// Print test suite header.
pub fn test_suite_begin(name: &str) {
    println!("=== Test Suite: {} ===", name);
}

/// Print test suite summary and return the error count.
pub fn test_suite_end() -> u32 {
    let errs = error_count();
    if errs == 0 {
        println!("PASS");
    } else {
        println!("FAIL ({} errors)", errs);
    }
    errs
}

// ---------------------------------------------------------------------------
// System register accessors (inline asm)
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn read_ssr() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = ssr", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_ssr(val: u32) {
    unsafe {
        asm!("ssr = {0}", "isync", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_evb() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = evb", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_syscfg() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = syscfg", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_syscfg(val: u32) {
    unsafe {
        asm!("syscfg = {0}", "isync", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_ccr() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = ccr", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_ccr(val: u32) {
    unsafe {
        asm!("ccr = {0}", in(reg) val, options(nomem, nostack));
    }
}

#[inline(always)]
pub fn read_imask() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = imask", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_imask(val: u32) {
    unsafe {
        asm!("imask = {0}", in(reg) val, options(nostack));
    }
}

/// Read IPEND (interrupt pending) via system register number (S17).
#[inline(always)]
pub fn read_ipend() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = s17", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_vid() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = vid", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_vid(val: u32) {
    unsafe {
        asm!("vid = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_modectl() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = modectl", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_elr() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = elr", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_badva() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = badva", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_htid() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = htid", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_framekey() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = framekey", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_framekey(val: u32) {
    unsafe {
        asm!("framekey = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_cfgbase() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = cfgbase", out(reg) val, options(nomem, nostack));
    }
    val
}

// ---------------------------------------------------------------------------
// Additional supervisor registers
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn read_sgp0() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = sgp0", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_sgp0(val: u32) {
    unsafe {
        asm!("sgp0 = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_sgp1() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = sgp1", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_sgp1(val: u32) {
    unsafe {
        asm!("sgp1 = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_stid() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = stid", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_stid(val: u32) {
    unsafe {
        asm!("stid = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn write_evb(val: u32) {
    unsafe {
        asm!("evb = {0}", "isync", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_diag() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = diag", out(reg) val, options(nomem, nostack));
    }
    val
}

// ---------------------------------------------------------------------------
// Guest mode registers
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn read_gelr() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = gelr", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_gelr(val: u32) {
    unsafe {
        asm!("gelr = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_gsr() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = gsr", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_gsr(val: u32) {
    unsafe {
        asm!("gsr = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_gosp() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = gosp", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_gosp(val: u32) {
    unsafe {
        asm!("gosp = {0}", in(reg) val, options(nostack));
    }
}

#[inline(always)]
pub fn read_gbadva() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = gbadva", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn write_gbadva(val: u32) {
    unsafe {
        asm!("gbadva = {0}", in(reg) val, options(nostack));
    }
}

// ---------------------------------------------------------------------------
// Pcycle / Upcycle
// ---------------------------------------------------------------------------

/// Read the pcycle counter (64-bit) using explicit register pair r1:0.
#[inline(always)]
pub fn read_pcycle() -> u64 {
    let lo: u32;
    let hi: u32;
    unsafe {
        asm!(
            "r1:0 = pcycle",
            out("r0") lo,
            out("r1") hi,
            options(nostack),
        );
    }
    ((hi as u64) << 32) | (lo as u64)
}

#[inline(always)]
pub fn read_pcyclelo() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = pcyclelo", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_pcyclehi() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = pcyclehi", out(reg) val, options(nomem, nostack));
    }
    val
}

#[inline(always)]
pub fn read_upcycle() -> u64 {
    let lo: u32;
    let hi: u32;
    unsafe {
        asm!(
            "r1:0 = upcycle",
            out("r0") lo,
            out("r1") hi,
            options(nostack),
        );
    }
    ((hi as u64) << 32) | (lo as u64)
}

// ---------------------------------------------------------------------------
// Timer registers (QTimer — read-only)
// ---------------------------------------------------------------------------

/// Read TIMERLO (S56) — lower 32 bits of the system timer.
#[inline(always)]
pub fn read_timerlo() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = s56", out(reg) val, options(nomem, nostack));
    }
    val
}

/// Read TIMERHI (S57) — upper 32 bits of the system timer.
#[inline(always)]
pub fn read_timerhi() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = s57", out(reg) val, options(nomem, nostack));
    }
    val
}

// ---------------------------------------------------------------------------
// Scheduler registers
// ---------------------------------------------------------------------------

/// Read BESTWAIT (S23).
#[inline(always)]
pub fn read_bestwait() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = s23", out(reg) val, options(nomem, nostack));
    }
    val
}

/// Write BESTWAIT (S23).
#[inline(always)]
pub fn write_bestwait(val: u32) {
    unsafe {
        asm!("s23 = {0}", in(reg) val, options(nostack));
    }
}

/// Read SCHEDCFG (S25).
#[inline(always)]
pub fn read_schedcfg() -> u32 {
    let val: u32;
    unsafe {
        asm!("{0} = s25", out(reg) val, options(nomem, nostack));
    }
    val
}

/// Write SCHEDCFG (S25).
#[inline(always)]
pub fn write_schedcfg(val: u32) {
    unsafe {
        asm!("s25 = {0}", in(reg) val, options(nostack));
    }
}

// ---------------------------------------------------------------------------
// MODECTL — read-only; modified indirectly by start/stop/wait/resume
// ---------------------------------------------------------------------------

/// Write MODECTL — note: may not be directly writable on all implementations.
/// MODECTL is typically modified indirectly via start/stop/wait/resume.
#[inline(always)]
pub fn write_modectl(val: u32) {
    unsafe {
        asm!("modectl = {0}", in(reg) val, options(nostack));
    }
}

// ---------------------------------------------------------------------------
// Thread control instructions (solo — monitor mode only)
// ---------------------------------------------------------------------------

/// Start threads specified by the bitmask in `mask`.
/// Each bit corresponds to a thread number. Started threads begin at their
/// reset vector (EVB+0x00).
#[inline(always)]
pub fn start_threads(mask: u32) {
    unsafe {
        asm!("start({0})", in(reg) mask, options(nostack));
    }
}

/// Stop the calling thread. Rs is legacy (not used).
#[inline(always)]
pub fn stop_self(mask: u32) {
    unsafe {
        asm!("stop({0})", in(reg) mask, options(nostack));
    }
}

/// Enter wait mode for the calling thread. Thread resumes on interrupt
/// or when another thread issues `resume`.
#[inline(always)]
pub fn wait_self(mask: u32) {
    unsafe {
        asm!("wait({0})", in(reg) mask, options(nostack));
    }
}

/// Resume threads specified by the bitmask (wake from wait mode).
#[inline(always)]
pub fn resume_threads(mask: u32) {
    unsafe {
        asm!("resume({0})", in(reg) mask, options(nostack));
    }
}

// ---------------------------------------------------------------------------
// Interrupt auto-disable (IAD) and assignment
// ---------------------------------------------------------------------------

/// Clear interrupt auto-disable bits specified by the mask.
/// `ciad(Rs)` clears IAD bits for interrupts indicated in Rs.
#[inline(always)]
pub fn ciad(mask: u32) {
    unsafe {
        asm!("ciad({0})", in(reg) mask, options(nostack));
    }
}

/// Assign interrupt to threads. Rs encodes the interrupt number in bits
/// [20:16] and the thread assignment mask in the lower bits.
#[inline(always)]
pub fn iassignw(val: u32) {
    unsafe {
        asm!("iassignw({0})", in(reg) val, options(nostack));
    }
}

/// Read interrupt assignment. Rs encodes the interrupt number in bits
/// [20:16]. Returns per-thread assignment mask.
#[inline(always)]
pub fn iassignr(val: u32) -> u32 {
    let result: u32;
    unsafe {
        asm!(
            "{res} = iassignr({val})",
            val = in(reg) val,
            res = out(reg) result,
            options(nostack),
        );
    }
    result
}

// ---------------------------------------------------------------------------
// Thread entry infrastructure (defined in crt0.S)
// ---------------------------------------------------------------------------

extern "C" {
    static mut __thread_entry: [u32; 16];
}


/// Set the entry function for a secondary thread. When the thread is
/// started via `start_threads()`, it will execute this function then stop.
/// Pass 0 to clear the entry (thread will just stop immediately).
pub fn set_thread_entry(tid: u32, entry: Option<extern "C" fn()>) {
    assert!(tid < 16);
    unsafe {
        core::ptr::write_volatile(
            &mut __thread_entry[tid as usize] as *mut u32,
            entry.map_or(0, |f| f as *const () as u32),
        );
    }
}

// ---------------------------------------------------------------------------
// Barriers
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn isync() {
    unsafe {
        asm!("isync", options(nostack));
    }
}

#[inline(always)]
pub fn syncht() {
    unsafe {
        asm!("syncht", options(nostack));
    }
}

// ---------------------------------------------------------------------------
// Interrupt management
// ---------------------------------------------------------------------------

extern "C" {
    static mut __IntHandlers: [u32; 32];
}

/// Register an interrupt handler for the given interrupt number (0-31).
/// The handler is a `extern "C" fn(intno: u32)` function.
pub fn register_interrupt(intno: u32, handler: extern "C" fn(u32)) {
    assert!(intno < 32);
    unsafe {
        core::ptr::write_volatile(
            &mut __IntHandlers[intno as usize] as *mut u32,
            handler as *const () as u32,
        );
    }
}

/// Trigger a software interrupt with the given mask.
#[inline(always)]
pub fn trigger_swi(mask: u32) {
    unsafe {
        asm!("swi({0})", in(reg) mask, options(nostack));
    }
}

/// Clear a software interrupt with the given mask.
#[inline(always)]
pub fn clear_swi(mask: u32) {
    unsafe {
        asm!("cswi({0})", in(reg) mask, options(nostack));
    }
}

// ---------------------------------------------------------------------------
// TLB operations (inline asm with register pairs)
// ---------------------------------------------------------------------------

/// Write a TLB entry: hi/lo form a 64-bit entry, idx is the TLB index.
#[inline(always)]
pub fn tlb_write(hi: u32, lo: u32, idx: u32) {
    unsafe {
        asm!(
            "r1:0 = combine({hi}, {lo})",
            "tlbw(r1:0, {idx})",
            hi = in(reg) hi,
            lo = in(reg) lo,
            idx = in(reg) idx,
            out("r0") _,
            out("r1") _,
            options(nostack),
        );
    }
}

/// Read a TLB entry at the given index. Returns (hi, lo).
#[inline(always)]
pub fn tlb_read(idx: u32) -> (u32, u32) {
    let lo: u32;
    let hi: u32;
    unsafe {
        asm!(
            "r1:0 = tlbr({idx})",
            idx = in(reg) idx,
            out("r0") lo,
            out("r1") hi,
            options(nostack),
        );
    }
    (hi, lo)
}

/// Probe the TLB for a virtual address. Returns index if found,
/// or a value with bit 31 set if not found.
#[inline(always)]
pub fn tlb_probe(va: u32) -> i32 {
    let result: i32;
    unsafe {
        asm!(
            "{res} = tlbp({va})",
            va = in(reg) va,
            res = out(reg) result,
            options(nostack),
        );
    }
    result
}

/// Invalidate a TLB entry by writing zeros at the given index.
#[inline(always)]
pub fn tlb_invalidate(idx: u32) {
    tlb_write(0, 0, idx);
    isync();
}

// ---------------------------------------------------------------------------
// Cache operations
// ---------------------------------------------------------------------------

#[inline(always)]
pub fn dczeroa(addr: u32) {
    unsafe {
        asm!("dczeroa({0})", in(reg) addr, options(nostack));
    }
}

#[inline(always)]
pub fn dccleana(addr: u32) {
    unsafe {
        asm!("dccleana({0})", in(reg) addr, options(nostack));
    }
}

#[inline(always)]
pub fn dcinva(addr: u32) {
    unsafe {
        asm!("dcinva({0})", in(reg) addr, options(nostack));
    }
}

#[inline(always)]
pub fn dccleaninva(addr: u32) {
    unsafe {
        asm!("dccleaninva({0})", in(reg) addr, options(nostack));
    }
}

#[inline(always)]
pub fn dckill() {
    unsafe {
        asm!("dckill", options(nostack));
    }
}

#[inline(always)]
pub fn ickill() {
    unsafe {
        asm!("ickill", options(nostack));
    }
}

#[inline(always)]
pub fn l2kill() {
    unsafe {
        asm!("l2kill", options(nostack));
    }
}

// ---------------------------------------------------------------------------
// Exception recording globals (defined in crt0.S)
// ---------------------------------------------------------------------------

extern "C" {
    pub static mut exception_cause: u32;
    pub static mut exception_elr: u32;
    pub static mut exception_badva: u32;
    pub static mut exception_count: u32;
}

/// Reset all exception recording globals to zero.
pub fn reset_exception_state() {
    unsafe {
        core::ptr::write_volatile(&raw mut exception_cause, 0);
        core::ptr::write_volatile(&raw mut exception_elr, 0);
        core::ptr::write_volatile(&raw mut exception_badva, 0);
        core::ptr::write_volatile(&raw mut exception_count, 0);
    }
}

/// Read exception cause (volatile).
pub fn get_exception_cause() -> u32 {
    unsafe { core::ptr::read_volatile(&raw const exception_cause) }
}

/// Read exception ELR (volatile).
pub fn get_exception_elr() -> u32 {
    unsafe { core::ptr::read_volatile(&raw const exception_elr) }
}

/// Read exception BADVA (volatile).
pub fn get_exception_badva() -> u32 {
    unsafe { core::ptr::read_volatile(&raw const exception_badva) }
}

/// Read exception count (volatile).
pub fn get_exception_count() -> u32 {
    unsafe { core::ptr::read_volatile(&raw const exception_count) }
}

// ---------------------------------------------------------------------------
// Privilege mode transitions
// ---------------------------------------------------------------------------

/// Enter user mode: sets SSR.UM=1, SSR.EX=1, ELR to the provided function,
/// then executes `rte` to enter user mode. The user function should call
/// `trap0(#1)` to return to supervisor mode.
///
/// # Safety
/// The function pointer must be valid and must eventually call trap0(#1).
#[inline(never)]
pub fn enter_user_mode(user_fn: fn()) {
    unsafe {
        asm!(
            // Set ELR to the user function address
            "elr = {func}",
            // Read SSR, set UM (bit 16) and EX (bit 17)
            "r0 = ssr",
            "r0 = setbit(r0, #16)",
            "r0 = setbit(r0, #17)",
            "ssr = r0",
            "isync",
            "rte",
            func = in(reg) user_fn as *const () as u32,
            out("r0") _,
            options(nostack),
        );
    }
}

/// Exit user mode by executing trap0(#1).
/// The trap0 handler in crt0.S recognizes cause==1 as "exit user mode".
#[inline(always)]
pub fn exit_user_mode() {
    unsafe {
        asm!("trap0(#1)", options(nostack));
    }
}

// ---------------------------------------------------------------------------
// Trap instructions for testing
// ---------------------------------------------------------------------------

/// Execute trap0 with the given immediate (0-255).
/// Note: trap0(#0) is semihosting, trap0(#1) is exit-user-mode.
/// Use values >= 2 for general exception testing.
#[inline(always)]
pub fn trap0(imm: u32) {
    // We use specific known immediates; Hexagon requires compile-time constants.
    // For testing, we provide wrappers for common values.
    unsafe {
        match imm {
            1 => asm!("trap0(#1)", options(nostack)),
            2 => asm!("trap0(#2)", options(nostack)),
            3 => asm!("trap0(#3)", options(nostack)),
            _ => asm!("trap0(#2)", options(nostack)),
        }
    }
}

/// Execute trap1 with the given immediate.
#[inline(always)]
pub fn trap1(imm: u32) {
    unsafe {
        match imm {
            0 => asm!("trap1(#0)", options(nostack)),
            1 => asm!("trap1(#1)", options(nostack)),
            2 => asm!("trap1(#2)", options(nostack)),
            3 => asm!("trap1(#3)", options(nostack)),
            _ => asm!("trap1(#2)", options(nostack)),
        }
    }
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

/// Volatile read from a memory address.
#[inline(always)]
pub unsafe fn read_volatile_u32(addr: *const u32) -> u32 {
    core::ptr::read_volatile(addr)
}

/// Volatile write to a memory address.
#[inline(always)]
pub unsafe fn write_volatile_u32(addr: *mut u32, val: u32) {
    core::ptr::write_volatile(addr, val);
}

// ---------------------------------------------------------------------------
// Delay helper
// ---------------------------------------------------------------------------

static DELAY_SCRATCH: AtomicU32 = AtomicU32::new(0);

/// Execute `n` volatile store iterations to create a delay.
/// Unlike NOP instructions which QEMU translates away, volatile memory
/// operations force actual instruction execution.
#[inline(never)]
pub fn busy_loop(n: u32) {
    for i in 0..n {
        DELAY_SCRATCH.store(i, Ordering::Relaxed);
    }
}

// ---------------------------------------------------------------------------
// Physical memory access (memw_phys)
// ---------------------------------------------------------------------------

/// Read a 32-bit word from physical memory using `memw_phys(Rs, Rt)`.
/// Physical address = `(offset & 0x7FF) + (base << 11)`.
#[inline(always)]
pub fn memw_phys_read(offset: u32, base: u32) -> u32 {
    let val: u32;
    unsafe {
        asm!(
            "{res} = memw_phys({off}, {base})",
            off = in(reg) offset,
            base = in(reg) base,
            res = out(reg) val,
            options(nostack),
        );
    }
    val
}

/// Read a field from the config table. `offset` is the byte offset within
/// the config table (e.g. 0x08 for SUBSYSTEM_BASE, 0x2c for JTLB size).
/// Uses cfgbase register shifted left by 5 as the base argument to memw_phys.
#[inline(always)]
pub fn read_cfgtable_field(offset: u32) -> u32 {
    let base = read_cfgbase() << 5;
    memw_phys_read(offset, base)
}

// ---------------------------------------------------------------------------
// Precondition checks
// ---------------------------------------------------------------------------

/// Check that the required hardware threads are present.
/// Returns false (and records an error) if any thread in `mask` is missing.
pub fn require_threads(mask: u32) -> bool {
    let available = read_cfgtable_field(CFGTABLE_THREAD_ENABLE_MASK);
    if (available & mask) != mask {
        println!(
            "FAIL: required thread mask 0x{:x} but hardware has 0x{:x}",
            mask, available
        );
        record_error();
        return false;
    }
    true
}

/// Check that HVX is present and at least `n` contexts exist.
/// Returns false (and records an error) if insufficient.
pub fn require_hvx_contexts(n: u32) -> bool {
    let present = read_cfgtable_field(CFGTABLE_COPROC_PRESENT);
    if present & 1 == 0 {
        println!("FAIL: HVX not present (COPROCESSOR_PRESENT=0x{:x})", present);
        record_error();
        return false;
    }
    let contexts = read_cfgtable_field(CFGTABLE_EXT_CONTEXTS);
    if contexts < n {
        println!("FAIL: need {} HVX contexts but hardware has {}", n, contexts);
        record_error();
        return false;
    }
    true
}

// ---------------------------------------------------------------------------
// Generic MMIO helpers
// ---------------------------------------------------------------------------

/// Volatile 32-bit read from an arbitrary MMIO address.
#[inline(always)]
pub fn mmio_read(addr: u32) -> u32 {
    unsafe { core::ptr::read_volatile(addr as *const u32) }
}

// ---------------------------------------------------------------------------
// Device TLB mapping
// ---------------------------------------------------------------------------

/// Install a 1MB device (uncached) TLB entry mapping `vpn_1m` -> `ppn_1m`
/// at the given TLB index. Uses global bit, all permissions, uncached attr.
pub fn install_device_mapping(vpn_1m: u32, ppn_1m: u32, tlb_idx: u32) {
    let hi = make_tlb_hi(vpn_1m, 0, true);
    let lo = make_tlb_lo(ppn_1m, TLB_PERM_XWRU, false);
    tlb_write(hi, lo, tlb_idx);
    isync();
}

// ---------------------------------------------------------------------------
// L2VIC MMIO helpers
// ---------------------------------------------------------------------------

/// Volatile read from L2VIC register at `base_va + reg_offset`.
#[inline(always)]
pub fn l2vic_read(base_va: u32, reg_offset: u32) -> u32 {
    unsafe { core::ptr::read_volatile((base_va + reg_offset) as *const u32) }
}

/// Volatile write to L2VIC register at `base_va + reg_offset`.
#[inline(always)]
pub fn l2vic_write(base_va: u32, reg_offset: u32, val: u32) {
    unsafe { core::ptr::write_volatile((base_va + reg_offset) as *mut u32, val) }
}

// ---------------------------------------------------------------------------
// Semihosting exit
// ---------------------------------------------------------------------------

/// Terminate the program via semihosting SYS_EXIT, then stop as fallback.
/// `code` is the exit status: 0 = success, nonzero = failure.
///
/// When semihosting is available (hexagon-sim, QEMU -nographic), this
/// causes the host to exit with `code`.  When semihosting is not
/// intercepted (e.g. UART-only / real hardware), the trap0 handler
/// advances ELR and the program falls through to `stop`.
pub fn semihosting_exit(code: u32) -> ! {
    unsafe {
        asm!(
            "r0 = #0x18",     // SYS_EXIT
            "trap0(#0)",
            // Fallback: stop all threads
            "r0 = #0x3f",
            "stop(r0)",
            in("r1") code,
            options(nostack, noreturn),
        );
    }
}

// ---------------------------------------------------------------------------
// Panic handler
// ---------------------------------------------------------------------------

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("PANIC: {}", info);
    semihosting_exit(1);
}
