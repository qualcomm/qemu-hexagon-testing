// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! System timer tests for Hexagon v81.
//!
//! Tests TIMERLO/TIMERHI (QTimer) at S56/S57 and PCYCLE-based timers.
//! QTimer is required — a missing QTimer is treated as a fatal error.

#![no_std]
#![no_main]

use hexagon_arch_tests::*;

/// Require QTimer to be present. Records a fatal error if S56/S57 are
/// both zero, since any system that models real hardware must provide QTimer.
fn require_qtimer() -> bool {
    let lo = read_timerlo();
    let hi = read_timerhi();
    if lo == 0 && hi == 0 {
        println!("FATAL: QTimer not available (TIMERLO/TIMERHI both zero)");
        record_error();
        return false;
    }
    true
}

/// TIMERLO/TIMERHI: verify QTimer is present and readable.
fn test_qtimer_read() {
    let lo = read_timerlo();
    let hi = read_timerhi();
    check!(lo != 0 || hi != 0);
}

/// Verify QTimer monotonicity.
fn test_qtimer_monotonic() {
    if !require_qtimer() {
        return;
    }
    let a = read_timerlo();
    busy_loop(20);
    let b = read_timerlo();
    busy_loop(20);
    let c = read_timerlo();
    check!(b >= a);
    check!(c >= b);
}

/// Verify QTimer increments over time.
/// The qtimer.so cosim updates the timer counter lazily (not every pcycle),
/// so we poll until the value changes rather than relying on a fixed delay.
fn test_qtimer_increments() {
    if !require_qtimer() {
        return;
    }
    let before = read_timerlo();
    let mut changed = false;
    for _ in 0..100_000u32 {
        let now = read_timerlo();
        if now != before {
            changed = true;
            break;
        }
    }
    if !changed {
        println!(
            "    timer stuck at 0x{:08x} after 100k polls",
            before
        );
    }
    check!(changed);
}

/// Verify QTimer MMIO version register matches the expected value.
/// Discovers the QTimer base from the config table's subsystem_base field,
/// installs a device TLB mapping, and reads the version register.
///
/// NOTE: The hexagon-sim qtimer.so cosim only provides S56/S57 system register
/// injection and does not model the full MMIO register space. On hexagon-sim,
/// this test will read 0x0 and skip. On QEMU (which models the full QTimer
/// device), this test verifies the MMIO version register.
fn test_qtimer_version_register() {
    if !require_qtimer() {
        return;
    }
    let subsys_raw = read_cfgtable_field(CFGTABLE_SUBSYSTEM_BASE);
    if subsys_raw == 0 {
        println!("FATAL: subsystem_base is 0 in config table");
        record_error();
        return;
    }
    let subsys_base = subsys_raw << 16;
    let qtimer_base = subsys_base + QTIMER_DEVICE_OFFSET;
    let vpn_1m = qtimer_base >> 20;
    install_device_mapping(vpn_1m, vpn_1m, 2);

    let version_addr = qtimer_base + QTIMER_FRAME_OFFSET + QTIMER_VERSION_OFFSET;
    let version = mmio_read(version_addr);
    if version == 0 {
        // The cosim doesn't model MMIO registers — only S56/S57 system registers.
        // This is expected on hexagon-sim with qtimer.so; skip without error.
        println!("(skip: MMIO not modeled by cosim)");
    } else {
        check32!(version, QTIMER_EXPECTED_VERSION);
    }

    tlb_invalidate(2);
}

/// PCYCLE-based timer: PCYCLE always works as a local cycle counter.
/// Verify PCYCLE monotonicity (this is already tested in test_pmu,
/// but included here for completeness as a timer source).
fn test_pcycle_as_timer() {
    let a = read_pcycle();
    busy_loop(50);
    let b = read_pcycle();
    busy_loop(50);
    let c = read_pcycle();
    check!(b > a);
    check!(c > b);
}

/// PCYCLE enable/disable acts as timer gating.
fn test_pcycle_timer_gate() {
    let syscfg = read_syscfg();

    // Disable pcycle
    write_syscfg(syscfg & !SYSCFG_PCYCLE_EN);
    let before = read_pcycle();
    busy_loop(100);
    let after = read_pcycle();
    check!(after == before);

    // Re-enable
    write_syscfg(syscfg | SYSCFG_PCYCLE_EN);
    let before2 = read_pcycle();
    busy_loop(100);
    let after2 = read_pcycle();
    check!(after2 > before2);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    test_suite_begin("System Timer");

    run_test("qtimer_read", test_qtimer_read);
    run_test("qtimer_monotonic", test_qtimer_monotonic);
    run_test("qtimer_increments", test_qtimer_increments);
    run_test("qtimer_version_register", test_qtimer_version_register);
    run_test("pcycle_as_timer", test_pcycle_as_timer);
    run_test("pcycle_timer_gate", test_pcycle_timer_gate);

    test_suite_end() as i32
}
