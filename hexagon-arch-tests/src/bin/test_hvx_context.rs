// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! HVX context management tests for Hexagon v81.
//!
//! Tests HVX presence detection, SSR.XA assignment and readback, that
//! distinct extension contexts hold distinct register files, that a
//! context keeps its contents while another one is selected, that a
//! context can be handed from one hardware thread to another, and the
//! exception raised on HVX access with SSR.XE=0.
//!
//! The register-file checks read V0 back out of the architectural
//! register file *after* changing SSR.XA, using explicit inline asm.
//! Reading a buffer that was stored before the switch, or a vector the
//! compiler happens to be holding, would pass even on an implementation
//! that ignored SSR.XA altogether.

#![no_std]
#![no_main]
#![feature(stdarch_hexagon)]
#![feature(asm_experimental_arch)]

use core::arch::asm;
use core::arch::hexagon::v128::{Q6_V_vsplat_R, HvxVector};
use core::sync::atomic::{AtomicU32, Ordering};
use hexagon_arch_tests::*;

/// 128-byte aligned buffer for HVX vmem operations.
#[repr(C, align(128))]
struct HvxBuf([u8; 128]);

impl HvxBuf {
    const fn zeroed() -> Self {
        HvxBuf([0u8; 128])
    }

    fn as_hvx_mut_ptr(&mut self) -> *mut HvxVector {
        self.0.as_mut_ptr() as *mut HvxVector
    }

    /// Check that all 32 words in the buffer match `expected`.
    fn check_all_words(&self, expected: u32) {
        let words = self.0.as_ptr() as *const u32;
        for i in 0..32 {
            let w = unsafe { core::ptr::read_volatile(words.add(i)) };
            if w != expected {
                println!(
                    "FAIL: buf word[{}] = 0x{:08x}, expected 0x{:08x}",
                    i, w, expected
                );
                record_error();
                return;
            }
        }
    }
}

/// Splat a scalar into an HVX vector and store to an aligned buffer.
unsafe fn hvx_splat_store(buf: *mut HvxVector, val: u32) {
    let v = Q6_V_vsplat_R(val as i32);
    core::ptr::write_volatile(buf, v);
}

// -----------------------------------------------------------------------
// V0 access through explicit inline asm
// -----------------------------------------------------------------------
//
// These name V0 directly so the value under test lives in the
// architectural register file, and therefore in whichever extension
// context SSR.XA selects, rather than wherever the compiler would have
// kept an HvxVector value.  Context selection, V0 access, and disabling
// HVX are one asm block: separate blocks do not reserve V0 from register
// allocation in intervening compiler-generated code.

/// Scratch buffer for reading V0 back out.  Only ever written by
/// v0_readback(), on one thread at a time.
static mut V0_OUT: HvxBuf = HvxBuf::zeroed();

/// Select `xa`, set V0, then disable HVX before returning to Rust.
fn v0_splat(xa: u32, val: u32) {
    let ssr = read_ssr();
    let selected = (ssr & !SSR_XA_MASK) | SSR_XE
        | ((xa << SSR_XA_SHIFT) & SSR_XA_MASK);
    let disabled = ssr & !SSR_XE;
    unsafe {
        asm!(
            "ssr = {selected}",
            "isync",
            "v0 = vsplat({val})",
            "ssr = {disabled}",
            "isync",
            selected = in(reg) selected,
            val = in(reg) val,
            disabled = in(reg) disabled,
            lateout("v0") _,
            options(nostack),
        );
    }
}

/// Select `xa`, store V0, disable HVX, and return its first word.
fn v0_readback(xa: u32) -> u32 {
    let ssr = read_ssr();
    let selected = (ssr & !SSR_XA_MASK) | SSR_XE
        | ((xa << SSR_XA_SHIFT) & SSR_XA_MASK);
    let disabled = ssr & !SSR_XE;
    unsafe {
        let p = (&raw mut V0_OUT) as *mut u8;
        asm!(
            "ssr = {selected}",
            "isync",
            "vmem({p}+#0) = v0",
            "ssr = {disabled}",
            "isync",
            selected = in(reg) selected,
            p = in(reg) p,
            disabled = in(reg) disabled,
            lateout("v0") _,
            options(nostack),
        );

        let words = p as *const u32;
        let w0 = core::ptr::read_volatile(words);
        for i in 1..32 {
            let w = core::ptr::read_volatile(words.add(i));
            if w != w0 {
                println!(
                    "FAIL: v0 lane {} = 0x{:08x} but lane 0 = 0x{:08x}",
                    i, w, w0
                );
                record_error();
                break;
            }
        }
        w0
    }
}

// Shared state for the multi-thread tests.
static T1_FLAG: AtomicU32 = AtomicU32::new(0);
static T1_SAW_CTX0: AtomicU32 = AtomicU32::new(0);
static T1_SAW_CTX1: AtomicU32 = AtomicU32::new(0);

fn wait_for_flag(flag: &AtomicU32, expected: u32, max_iters: u32) -> bool {
    for _ in 0..max_iters {
        if flag.load(Ordering::SeqCst) == expected {
            return true;
        }
        busy_loop(10);
    }
    false
}

fn wait_for_thread_stopped(tid: u32, max_iters: u32) -> bool {
    let mask = 1u32 << tid;
    for _ in 0..max_iters {
        if read_modectl() & mask == 0 {
            return true;
        }
        busy_loop(10);
    }
    false
}

// -----------------------------------------------------------------------
// Test 1: HVX present
// -----------------------------------------------------------------------

/// Read config table offset 0x30, verify bit 0 set (HVX present).
/// Read offset 0x34, verify >= 1 context.
fn test_hvx_present() {
    let present = read_cfgtable_field(CFGTABLE_COPROC_PRESENT);
    check!(present & 1 != 0);

    let contexts = read_cfgtable_field(CFGTABLE_EXT_CONTEXTS);
    check!(contexts >= 1);
}

// -----------------------------------------------------------------------
// Test 2: SSR.XA readback
// -----------------------------------------------------------------------

/// SSR.XA is three bits at 29:27 whatever EXT_CONTEXTS says, so all
/// eight encodings must round-trip, and none of them may disturb SSR.SS
/// at bit 30 or SSR.XE at bit 31.  HVX stays disabled here: an encoding
/// above EXT_CONTEXTS - 1 need not name a usable context.
fn test_ssr_xa_readback() {
    let saved = read_ssr();
    let base = saved & !(SSR_XA_MASK | SSR_XE);

    for xa in 0..8u32 {
        write_ssr(base | (xa << SSR_XA_SHIFT));

        let rb = read_ssr();
        check32!((rb & SSR_XA_MASK) >> SSR_XA_SHIFT, xa);
        // XA must not have bled into the neighbouring bits.
        check32!(rb & SSR_SS, base & SSR_SS);
        check32!(rb & SSR_XE, 0);
    }

    write_ssr(saved);
}

// -----------------------------------------------------------------------
// Test 3: HVX vsplat + store
// -----------------------------------------------------------------------

/// Splat 0xDEADBEEF into a vector, store to aligned buffer, verify all
/// 32 words match.
fn test_hvx_vsplat_store() {
    let mut buf = HvxBuf::zeroed();

    unsafe {
        hvx_splat_store(buf.as_hvx_mut_ptr(), 0xDEAD_BEEF);
    }

    buf.check_all_words(0xDEAD_BEEF);
}

// -----------------------------------------------------------------------
// Test 4: HVX context isolation (multi-thread)
// -----------------------------------------------------------------------

/// Thread 1 entry: set SSR.XA=1 and splat 0xBBBBBBBB.
extern "C" fn thread1_hvx_context() {
    v0_splat(1, 0xBBBB_BBBB);
    let _ = v0_readback(1);

    T1_FLAG.store(1, Ordering::SeqCst);
}

/// Multi-thread context isolation:
/// - T0 on context 0 puts 0xAAAAAAAA in V0.
/// - T1 on context 1 puts 0xBBBBBBBB in its V0.
/// - T0 reads V0 back and must still see 0xAAAAAAAA.
fn test_hvx_context_isolation() {
    T1_FLAG.store(0, Ordering::SeqCst);

    let saved_ssr = read_ssr();

    v0_splat(0, 0xAAAA_AAAA);
    check32!(v0_readback(0), 0xAAAA_AAAA);

    // Start T1 on its own HVX context.
    set_thread_entry(1, Some(thread1_hvx_context));
    start_threads(1 << 1);

    let ok = wait_for_flag(&T1_FLAG, 1, 50000);
    check!(ok);
    wait_for_thread_stopped(1, 50000);

    // T1 wrote a different context, so T0's V0 is untouched.
    check32!(v0_readback(0), 0xAAAA_AAAA);

    write_ssr(saved_ssr);
}

// -----------------------------------------------------------------------
// Test 5: HVX XE disable exception
// -----------------------------------------------------------------------

/// Clear SSR.XE, execute an HVX insn (should fault with cause 0x16),
/// re-enable SSR.XE.
fn test_hvx_xe_disable_exception() {
    reset_exception_state();

    let saved_ssr = read_ssr();

    // Clear XE bit
    write_ssr(saved_ssr & !SSR_XE);

    // Execute an HVX instruction in its own packet so the exception
    // handler's ELR+4 advance cleanly skips it.
    unsafe {
        asm!(
            "{{ v0 = vsplat({val}) }}",
            val = in(reg) 0u32,
            options(nostack),
        );
    }

    // Verify exception was taken
    check32!(get_exception_count(), 1);
    check32!(get_exception_cause(), CAUSE_NO_COPROC_ENABLE);

    // Re-enable XE
    write_ssr(saved_ssr);
}

// -----------------------------------------------------------------------
// Test 6: HVX context switch on a single thread
// -----------------------------------------------------------------------

/// One thread moving between two contexts.  Each read of V0 comes out of
/// the register file after the switch, so a core that ignored SSR.XA and
/// gave every context the same register file would fail the last two
/// checks.
fn test_hvx_context_switch_xa() {
    let saved_ssr = read_ssr();

    // Context 0 gets a pattern.
    v0_splat(0, 0xAAAA_AAAA);
    check32!(v0_readback(0), 0xAAAA_AAAA);

    // Context 1 must not see it, and gets its own.
    check32_ne!(v0_readback(1), 0xAAAA_AAAA);
    v0_splat(1, 0xBBBB_BBBB);
    check32!(v0_readback(1), 0xBBBB_BBBB);

    // Context 0 kept its pattern while context 1 was selected.
    check32!(v0_readback(0), 0xAAAA_AAAA);

    // And so did context 1.
    check32!(v0_readback(1), 0xBBBB_BBBB);

    write_ssr(saved_ssr);
}

// -----------------------------------------------------------------------
// Test 7: HVX context handed between hardware threads
// -----------------------------------------------------------------------

/// Thread 1 entry: report what it sees in context 0 and in context 1,
/// then leave a new value behind in context 1.
extern "C" fn thread1_hvx_handoff() {
    T1_SAW_CTX0.store(v0_readback(0), Ordering::SeqCst);

    T1_SAW_CTX1.store(v0_readback(1), Ordering::SeqCst);
    v0_splat(1, 0xD00D_FEED);

    T1_FLAG.store(1, Ordering::SeqCst);
}

/// A context is not private to a hardware thread: an operating system
/// hands one between threads, and whoever selects it next finds the
/// register file as the previous owner left it.  T0 fills context 1 and
/// releases it, T1 must find T0's value in context 1 but not in context
/// 0, and T0 then takes context 1 back and must see T1's value.
fn test_hvx_context_handoff() {
    T1_FLAG.store(0, Ordering::SeqCst);
    T1_SAW_CTX0.store(0, Ordering::SeqCst);
    T1_SAW_CTX1.store(0, Ordering::SeqCst);

    let saved_ssr = read_ssr();

    // T0 leaves a value in context 1, then releases the coprocessor.
    v0_splat(1, 0xC0FF_EE00);
    check32!(v0_readback(1), 0xC0FF_EE00);

    set_thread_entry(1, Some(thread1_hvx_handoff));
    start_threads(1 << 1);

    let ok = wait_for_flag(&T1_FLAG, 1, 50000);
    check!(ok);
    wait_for_thread_stopped(1, 50000);

    // T1 found what T0 left in context 1, and context 0 is a different
    // register file, so the value must not have shown up there too.
    check32_ne!(T1_SAW_CTX0.load(Ordering::SeqCst), 0xC0FF_EE00);
    check32!(T1_SAW_CTX1.load(Ordering::SeqCst), 0xC0FF_EE00);

    // T0 takes the context back and finds what T1 left.
    check32!(v0_readback(1), 0xD00D_FEED);

    write_ssr(saved_ssr);
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("HVX Context");

    if !require_threads(0x3) {
        return test_suite_end() as i32;
    }
    if !require_hvx_contexts(2) {
        return test_suite_end() as i32;
    }

    run_test("hvx_present", test_hvx_present);
    run_test("ssr_xa_readback", test_ssr_xa_readback);
    run_test("hvx_vsplat_store", test_hvx_vsplat_store);
    run_test("hvx_context_isolation", test_hvx_context_isolation);
    run_test("hvx_xe_disable_exception", test_hvx_xe_disable_exception);
    run_test("hvx_context_switch_xa", test_hvx_context_switch_xa);
    run_test("hvx_context_handoff", test_hvx_context_handoff);

    test_suite_end() as i32
}
