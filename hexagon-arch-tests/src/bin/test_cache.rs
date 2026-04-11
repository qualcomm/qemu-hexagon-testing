// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! Cache operation tests for Hexagon v81.
//!
//! Tests cache invalidation, zero-allocate, enable/disable, barriers.

#![no_std]
#![no_main]

use hexagon_arch_tests::*;

/// dckill/ickill: execute, verify no crash, memory still accessible.
fn test_dckill_ickill() {
    dckill();
    isync();
    ickill();
    isync();
    // Verify memory is still accessible by reading a known location
    let val: u32 = 0xCAFE;
    let readback = unsafe {
        let p = &val as *const u32;
        core::ptr::read_volatile(p)
    };
    check32!(readback, 0xCAFE);
}

/// l2kill: execute, verify no crash.
fn test_l2kill() {
    l2kill();
    isync();
    // Verify memory still works
    let val: u32 = 0xBEEF;
    let readback = unsafe {
        let p = &val as *const u32;
        core::ptr::read_volatile(p)
    };
    check32!(readback, 0xBEEF);
}

/// dczeroa: fill buffer with 0xDEADBEEF, zero-allocate a cache line, verify zeros.
fn test_dczeroa() {
    // Buffer must be cache-line aligned (32 bytes for L1 dcache line)
    #[repr(align(32))]
    struct Aligned {
        data: [u32; 8],
    }
    let buf = Aligned {
        data: [0xDEAD_BEEF; 8],
    };

    // dczeroa zeroes a 32-byte cache line
    let addr = buf.data.as_ptr() as u32;
    dczeroa(addr);
    syncht();

    // Verify the 32 bytes (8 words) are zeroed
    for i in 0..8 {
        let val = unsafe { core::ptr::read_volatile(&buf.data[i]) };
        check32!(val, 0);
    }
}

/// dccleaninva: write data, clean+invalidate, re-read, verify data persists.
fn test_dccleaninva() {
    #[repr(align(32))]
    struct Aligned {
        data: [u32; 8],
    }
    let mut buf = Aligned { data: [0; 8] };

    // Write known data
    for i in 0..8 {
        unsafe { core::ptr::write_volatile(&mut buf.data[i], (i as u32) + 1) };
    }

    // Clean and invalidate
    let addr = buf.data.as_ptr() as u32;
    dccleaninva(addr);
    syncht();

    // Re-read -- data should persist (it was cleaned to memory)
    for i in 0..8 {
        let val = unsafe { core::ptr::read_volatile(&buf.data[i]) };
        check32!(val, (i as u32) + 1);
    }
}

/// Cache control via SYSCFG: toggle DCACHE_EN bit, verify it can be written/read.
fn test_cache_disable_enable() {
    let syscfg = read_syscfg();

    // Explicitly set DCACHE_EN, then clear it, then set it again
    write_syscfg(syscfg | SYSCFG_DCACHE_EN);
    let with_dcache = read_syscfg();
    check!(with_dcache & SYSCFG_DCACHE_EN != 0);

    // Clear DCACHE_EN
    write_syscfg(with_dcache & !SYSCFG_DCACHE_EN);
    let without_dcache = read_syscfg();
    check!(without_dcache & SYSCFG_DCACHE_EN == 0);

    // Verify memory still works without dcache
    let val: u32 = 0x1234_5678;
    let readback_val = unsafe {
        let p = &val as *const u32;
        core::ptr::read_volatile(p)
    };
    check32!(readback_val, 0x1234_5678);

    // Restore original + DCACHE_EN
    write_syscfg(syscfg | SYSCFG_DCACHE_EN);
}

/// syncht/isync barriers: execute, verify no crash.
fn test_barriers() {
    syncht();
    isync();
    syncht();
    isync();
    // If we get here, barriers didn't crash
    check!(true);
}

/// dcinva: invalidate a cache line, verify memory still readable.
fn test_dcinva() {
    #[repr(align(32))]
    struct Aligned {
        data: [u32; 8],
    }
    let buf = Aligned {
        data: [0x1111_2222; 8],
    };

    let addr = buf.data.as_ptr() as u32;
    dcinva(addr);
    syncht();

    // Data should still be readable (backed by memory)
    let val = unsafe { core::ptr::read_volatile(&buf.data[0]) };
    check32!(val, 0x1111_2222);
}

/// dccleana: clean a cache line (write-back), verify data persists.
fn test_dccleana() {
    #[repr(align(32))]
    struct Aligned {
        data: [u32; 8],
    }
    let mut buf = Aligned { data: [0; 8] };

    // Write data
    for i in 0..8 {
        unsafe { core::ptr::write_volatile(&mut buf.data[i], 0xAA00 + i as u32) };
    }

    // Clean (write back to memory)
    let addr = buf.data.as_ptr() as u32;
    dccleana(addr);
    syncht();

    // Data should persist
    for i in 0..8 {
        let val = unsafe { core::ptr::read_volatile(&buf.data[i]) };
        check32!(val, 0xAA00 + i as u32);
    }
}

/// Icache toggle: disable/enable ICACHE_EN via SYSCFG.
fn test_icache_toggle() {
    let syscfg = read_syscfg();

    // Ensure ICACHE_EN is set
    write_syscfg(syscfg | SYSCFG_ICACHE_EN);
    let with = read_syscfg();
    check!(with & SYSCFG_ICACHE_EN != 0);

    // Clear ICACHE_EN
    write_syscfg(with & !SYSCFG_ICACHE_EN);
    let without = read_syscfg();
    check!(without & SYSCFG_ICACHE_EN == 0);

    // Restore
    write_syscfg(syscfg | SYSCFG_ICACHE_EN);
}

/// dczeroa on multiple cache lines.
fn test_dczeroa_multiple() {
    #[repr(align(32))]
    struct Aligned {
        data: [u32; 16], // 2 cache lines (64 bytes)
    }
    let buf = Aligned {
        data: [0xBAAD_F00D; 16],
    };

    let base = buf.data.as_ptr() as u32;
    // Zero first cache line
    dczeroa(base);
    // Zero second cache line
    dczeroa(base + 32);
    syncht();

    for i in 0..16 {
        let val = unsafe { core::ptr::read_volatile(&buf.data[i]) };
        check32!(val, 0);
    }
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("Cache Operations");

    run_test("dckill_ickill", test_dckill_ickill);
    run_test("l2kill", test_l2kill);
    run_test("dczeroa", test_dczeroa);
    run_test("dccleaninva", test_dccleaninva);
    run_test("cache_disable_enable", test_cache_disable_enable);
    run_test("barriers", test_barriers);
    run_test("dcinva", test_dcinva);
    run_test("dccleana", test_dccleana);
    run_test("icache_toggle", test_icache_toggle);
    run_test("dczeroa_multiple", test_dczeroa_multiple);

    test_suite_end() as i32
}
