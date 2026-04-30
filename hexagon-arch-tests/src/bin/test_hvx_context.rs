// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! HVX context management tests for Hexagon v81.
//!
//! Tests HVX presence detection, per-thread XE enable/disable, SSR.XA
//! context assignment and readback, vector register isolation between
//! threads using different HVX contexts, and exception on HVX access
//! with SSR.XE=0.

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

// Shared state for multi-thread test.
static T1_FLAG: AtomicU32 = AtomicU32::new(0);
static mut BUF_T1: HvxBuf = HvxBuf::zeroed();

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

/// Write SSR.XA=1, read back, verify bits 30:28 = 1. Restore original.
fn test_ssr_xa_readback() {
    let saved = read_ssr();

    // Set XA = 1 (bits 30:28)
    let new_ssr = (saved & !SSR_XA_MASK) | (1 << SSR_XA_SHIFT);
    write_ssr(new_ssr);

    let rb = read_ssr();
    let xa = (rb & SSR_XA_MASK) >> SSR_XA_SHIFT;
    check32!(xa, 1);

    // Restore
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

/// Thread 1 entry: set SSR.XA=1, splat 0xBBBBBBBB, store to BUF_T1.
extern "C" fn thread1_hvx_context() {
    let ssr = read_ssr();
    let new_ssr = (ssr & !SSR_XA_MASK) | (1 << SSR_XA_SHIFT) | SSR_XE;
    write_ssr(new_ssr);

    unsafe {
        let buf_ptr = (&raw mut BUF_T1) as *mut HvxVector;
        hvx_splat_store(buf_ptr, 0xBBBB_BBBB);
    }

    T1_FLAG.store(1, Ordering::SeqCst);
}

/// Multi-thread context isolation:
/// - T0: SSR.XA=0, splat 0xAAAAAAAA into V0, store to buf_t0.
/// - Start T1 with SSR.XA=1: splat 0xBBBBBBBB into V0, store to buf_t1.
/// - T0: re-read V0 into buf_t0_after.
/// - Verify buf_t0_after still has 0xAAAAAAAA (T1 used different context).
/// - Verify buf_t1 has 0xBBBBBBBB.
fn test_hvx_context_isolation() {
    let mut buf_t0 = HvxBuf::zeroed();
    let mut buf_t0_after = HvxBuf::zeroed();

    // Clear thread 1 state
    T1_FLAG.store(0, Ordering::SeqCst);
    unsafe {
        core::ptr::write_bytes((&raw mut BUF_T1) as *mut u8, 0, 128);
    }

    // T0: set XA=0, splat pattern into a vector, hold the value
    let saved_ssr = read_ssr();
    let t0_ssr = (saved_ssr & !SSR_XA_MASK) | SSR_XE; // XA=0, XE=1
    write_ssr(t0_ssr);

    let v_t0 = unsafe { Q6_V_vsplat_R(0xAAAA_AAAA_u32 as i32) };
    unsafe {
        core::ptr::write_volatile(buf_t0.as_hvx_mut_ptr(), v_t0);
    }
    buf_t0.check_all_words(0xAAAA_AAAA);

    // Start T1 with its own HVX context
    set_thread_entry(1, Some(thread1_hvx_context));
    start_threads(1 << 1);

    let ok = wait_for_flag(&T1_FLAG, 1, 50000);
    check!(ok);
    wait_for_thread_stopped(1, 50000);

    // T0: store the held vector again — should still be 0xAAAA_AAAA
    // (T1 ran on a different HW thread with a different XA context)
    unsafe {
        core::ptr::write_volatile(buf_t0_after.as_hvx_mut_ptr(), v_t0);
    }
    buf_t0_after.check_all_words(0xAAAA_AAAA);

    // Verify T1's buffer
    unsafe {
        let buf_t1_ref = &*(&raw const BUF_T1);
        buf_t1_ref.check_all_words(0xBBBB_BBBB);
    }

    // Restore SSR
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
// Test 6: HVX context switch on single thread
// -----------------------------------------------------------------------

/// Single thread switches between contexts:
/// - SSR.XA=0: splat 0x11111111, store to buf0.
/// - SSR.XA=1: splat 0x22222222, store to buf1.
/// - Verify buf0 still has 0x11111111 (context 1 ops didn't corrupt it).
/// - Verify buf1 has 0x22222222.
fn test_hvx_context_switch_xa() {
    let mut buf0 = HvxBuf::zeroed();
    let mut buf1 = HvxBuf::zeroed();

    let saved_ssr = read_ssr();
    let ssr_ctx0 = (saved_ssr & !SSR_XA_MASK) | SSR_XE; // XA=0
    let ssr_ctx1 = (saved_ssr & !SSR_XA_MASK) | (1 << SSR_XA_SHIFT) | SSR_XE; // XA=1

    // Context 0: splat pattern A
    write_ssr(ssr_ctx0);
    unsafe {
        hvx_splat_store(buf0.as_hvx_mut_ptr(), 0x1111_1111);
    }

    // Context 1: splat pattern B
    write_ssr(ssr_ctx1);
    unsafe {
        hvx_splat_store(buf1.as_hvx_mut_ptr(), 0x2222_2222);
    }

    // Verify context 0's store wasn't corrupted by context 1's operations
    buf0.check_all_words(0x1111_1111);
    // Verify context 1's store
    buf1.check_all_words(0x2222_2222);

    // Restore SSR
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

    test_suite_end() as i32
}
