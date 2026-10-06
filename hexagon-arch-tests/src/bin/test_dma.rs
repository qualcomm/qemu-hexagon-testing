// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! User-DMA descriptor and completion tests.
//!
//! The workloads model linked block movement and a strided rectangle without
//! embedding application-level data structures or interfaces.

#![no_std]
#![no_main]
#![feature(asm_experimental_arch)]

use core::arch::asm;
use hexagon_arch_tests::*;

const DESC_DONE: u32 = 0x8000_0000;
const DESC_ORDERED: u32 = 0x4000_0000;
const DESC_SOURCE_BYPASS: u32 = 0x2000_0000;
const DESC_DESTINATION_BYPASS: u32 = 0x1000_0000;
const DESC_SIZE_32: u32 = 0x0100_0000;

const ROW_BYTES: usize = 37;
const ROWS: usize = 7;
const SOURCE_STRIDE: usize = 61;
const DESTINATION_STRIDE: usize = 71;
const CHAIN_COUNT: usize = 17;
const CHAIN_BYTES: usize = 29;

#[repr(C)]
#[derive(Clone, Copy)]
struct Descriptor0 {
    next: u32,
    info: u32,
    source: u32,
    destination: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Descriptor32 {
    next: u32,
    info: u32,
    source: u32,
    destination: u32,
    attributes: u32,
    shape: u32,
    stride: u32,
    offsets: u32,
}

#[repr(C, align(8192))]
struct DescriptorChain([Descriptor0; CHAIN_COUNT]);

#[repr(C, align(8192))]
struct Descriptor32Slot(Descriptor32);

#[repr(align(128))]
struct AlignedBuffer<const N: usize>([u8; N]);

static mut SOURCE_1D: [u8; CHAIN_COUNT * CHAIN_BYTES] = [0; CHAIN_COUNT * CHAIN_BYTES];
static mut DESTINATION_1D: [u8; CHAIN_COUNT * CHAIN_BYTES] = [0; CHAIN_COUNT * CHAIN_BYTES];
static mut SOURCE_2D: [u8; ROWS * SOURCE_STRIDE] = [0; ROWS * SOURCE_STRIDE];
static mut DESTINATION_2D: [u8; ROWS * DESTINATION_STRIDE] = [0; ROWS * DESTINATION_STRIDE];
static mut CHAIN: DescriptorChain = DescriptorChain([Descriptor0 {
    next: 0,
    info: 0,
    source: 0,
    destination: 0,
}; CHAIN_COUNT]);
static mut RECTANGLE: Descriptor32Slot = Descriptor32Slot(Descriptor32 {
    next: 0, info: 0, source: 0, destination: 0, attributes: 0, shape: 0, stride: 0, offsets: 0,
});
static mut FILL_DESTINATION: AlignedBuffer<{ 4 * 128 }> = AlignedBuffer([0; 4 * 128]);

#[inline(always)]
unsafe fn release_descriptor(descriptor: *const u8) {
    asm!("release({descriptor}):at", descriptor = in(reg) descriptor, options(nostack));
}

#[inline(always)]
unsafe fn start_dma(descriptor: *const u8) {
    asm!("dmstart({descriptor})", descriptor = in(reg) descriptor, options(nostack));
}

#[inline(always)]
unsafe fn link_dma(tail: *const u8, descriptor: *const u8) {
    asm!(
        "dmlink({tail}, {descriptor})",
        tail = in(reg) tail,
        descriptor = in(reg) descriptor,
        options(nostack),
    );
}

#[inline(always)]
unsafe fn wait_dma() -> u32 {
    let status: u32;
    asm!("{status} = dmwait", status = out(reg) status, options(nostack));
    status
}

#[inline(always)]
unsafe fn poll_dma() -> u32 {
    let status: u32;
    asm!("{status} = dmpoll", status = out(reg) status, options(nostack));
    status
}

#[inline(always)]
unsafe fn pause_dma() -> u32 {
    let status: u32;
    asm!("{status} = dmpause", status = out(reg) status, options(nostack));
    status
}

#[inline(always)]
unsafe fn resume_dma(state: u32) {
    asm!("dmresume({state})", state = in(reg) state, options(nostack));
}

unsafe fn fill(buffer: *mut u8, length: usize, salt: u8) {
    for index in 0..length {
        buffer
            .add(index)
            .write((index as u8).wrapping_mul(37).wrapping_add(salt));
    }
}

unsafe fn check_equal(left: *const u8, right: *const u8, length: usize) {
    for index in 0..length {
        check32!(left.add(index).read() as u32, right.add(index).read() as u32);
    }
}

fn test_linked_blocks() {
    unsafe {
        let source = core::ptr::addr_of_mut!(SOURCE_1D) as *mut u8;
        let destination = core::ptr::addr_of_mut!(DESTINATION_1D) as *mut u8;
        let chain = core::ptr::addr_of_mut!(CHAIN) as *mut Descriptor0;
        fill(source, CHAIN_COUNT * CHAIN_BYTES, 0x31);
        fill(destination, CHAIN_COUNT * CHAIN_BYTES, 0xc7);

        for index in 0..CHAIN_COUNT {
            let descriptor = chain.add(index);
            (*descriptor).next = 0;
            (*descriptor).info = ((index & 1 != 0) as u32 * DESC_SOURCE_BYPASS)
                | ((index & 1 == 0) as u32 * DESC_DESTINATION_BYPASS)
                | ((index + 1 == CHAIN_COUNT) as u32 * DESC_ORDERED)
                | CHAIN_BYTES as u32;
            (*descriptor).source = source.add(index * CHAIN_BYTES) as u32;
            (*descriptor).destination = destination.add(index * CHAIN_BYTES) as u32;
            release_descriptor(descriptor as *const u8);
        }
        start_dma(chain as *const u8);
        for index in 1..CHAIN_COUNT {
            link_dma(chain.add(index - 1) as *const u8, chain.add(index) as *const u8);
        }
        wait_dma();

        for index in 0..CHAIN_COUNT {
            check!((*chain.add(index)).info & DESC_DONE != 0);
            check_equal(source.add(index * CHAIN_BYTES), destination.add(index * CHAIN_BYTES), CHAIN_BYTES);
        }
    }
}

/// DMA specification Chapter 4 defines these commands to return DM0. An idle
/// DMA engine therefore returns state 0 for each command.
fn test_idle_commands() {
    unsafe {
        check32!(poll_dma() & 3, 0);
        check32!(wait_dma() & 3, 0);
        check32!(pause_dma() & 3, 0);
    }
}

fn test_resume_idle_state() {
    unsafe {
        // Chapter 4 defines DMResume as restoring DM0 while the engine is idle.
        resume_dma(0);
        check32!(poll_dma() & 3, 0);
    }
}

fn test_strided_blocks() {
    unsafe {
        let source = core::ptr::addr_of_mut!(SOURCE_2D) as *mut u8;
        let destination = core::ptr::addr_of_mut!(DESTINATION_2D) as *mut u8;
        let chain = core::ptr::addr_of_mut!(CHAIN) as *mut Descriptor0;
        fill(source, ROWS * SOURCE_STRIDE, 0x52);
        fill(destination, ROWS * DESTINATION_STRIDE, 0xa4);
        for row in 0..ROWS {
            let descriptor = chain.add(row);
            (*descriptor).next = if row + 1 == ROWS { 0 } else { chain.add(row + 1) as u32 };
            (*descriptor).info = DESC_SOURCE_BYPASS | DESC_DESTINATION_BYPASS | ROW_BYTES as u32;
            (*descriptor).source = source.add(row * SOURCE_STRIDE) as u32;
            (*descriptor).destination = destination.add(row * DESTINATION_STRIDE) as u32;
            release_descriptor(descriptor as *const u8);
        }
        start_dma(chain as *const u8);
        wait_dma();

        for row in 0..ROWS {
            check!((*chain.add(row)).info & DESC_DONE != 0);
            check_equal(source.add(row * SOURCE_STRIDE), destination.add(row * DESTINATION_STRIDE), ROW_BYTES);
            for column in ROW_BYTES..DESTINATION_STRIDE {
                let index = row * DESTINATION_STRIDE + column;
                check32!(destination.add(index).read() as u32,
                         (index as u8).wrapping_mul(37).wrapping_add(0xa4) as u32);
            }
        }
    }
}

fn test_standard_2d() {
    unsafe {
        let source = core::ptr::addr_of_mut!(SOURCE_2D) as *mut u8;
        let destination = core::ptr::addr_of_mut!(DESTINATION_2D) as *mut u8;
        let descriptor = core::ptr::addr_of_mut!(RECTANGLE) as *mut Descriptor32;
        fill(source, ROWS * SOURCE_STRIDE, 0x5a);
        fill(destination, ROWS * DESTINATION_STRIDE, 0xa5);
        (*descriptor).next = 0;
        (*descriptor).info = DESC_SIZE_32 | DESC_SOURCE_BYPASS | DESC_DESTINATION_BYPASS;
        (*descriptor).source = source as u32;
        (*descriptor).destination = destination as u32;
        (*descriptor).attributes = 0;
        (*descriptor).shape = ((ROWS as u32) << 16) | ROW_BYTES as u32;
        (*descriptor).stride = ((DESTINATION_STRIDE as u32) << 16) | SOURCE_STRIDE as u32;
        (*descriptor).offsets = 0;
        release_descriptor(descriptor as *const u8);
        start_dma(descriptor as *const u8);
        check32!(wait_dma() & 3, 0);
        check!((*descriptor).info & DESC_DONE != 0);
        for row in 0..ROWS {
            check_equal(source.add(row * SOURCE_STRIDE), destination.add(row * DESTINATION_STRIDE), ROW_BYTES);
        }
    }
}

fn test_constant_fill() {
    unsafe {
        let destination = core::ptr::addr_of_mut!(FILL_DESTINATION.0) as *mut u8;
        let descriptor = core::ptr::addr_of_mut!(RECTANGLE) as *mut Descriptor32;
        fill(destination, 4 * 128, 0x11);
        (*descriptor).next = 0;
        (*descriptor).info = DESC_SIZE_32 | DESC_DESTINATION_BYPASS;
        (*descriptor).source = 0;
        (*descriptor).destination = destination as u32;
        (*descriptor).attributes = (0x6d << 8) | 8;
        (*descriptor).shape = (4 << 16) | 64;
        (*descriptor).stride = 128 << 16;
        (*descriptor).offsets = 0;
        release_descriptor(descriptor as *const u8);
        start_dma(descriptor as *const u8);
        check32!(wait_dma() & 3, 0);
        check!((*descriptor).info & DESC_DONE != 0);
        for row in 0..4 {
            for byte in 0..64 {
                check32!(destination.add(row * 128 + byte).read() as u32, 0x6d);
            }
        }
    }
}

fn dma_version() -> u32 {
    let version = read_cfgtable_field(CFGTABLE_DMA_VERSION);
    if version != 0 {
        return version;
    }
    match isa_version() {
        0x68 => 1,
        0x73 => 2,
        0x75 => 4,
        0x79 => 6,
        0x81 => 8,
        _ => 0,
    }
}

fn expected_dma_version_from_rev() -> u32 {
    match isa_version() {
        0x68 => 1,
        0x73 => 2,
        0x75 => 4,
        0x79 => 6,
        0x81 => 8,
        _ => 0,
    }
}

fn test_version_discovery() {
    let reported = read_cfgtable_field(CFGTABLE_DMA_VERSION);
    let expected = expected_dma_version_from_rev();

    check32!(reported, expected);
}

#[no_mangle]
pub extern "C" fn rust_main() -> i32 {
    let version = dma_version();
    test_suite_begin("DMA");
    check!(version != 0);
    println!("DMA version {}, REV 0x{:08x}", version, read_rev());
    run_test("version discovery", test_version_discovery);
    run_test("idle commands", test_idle_commands);
    run_test("resume idle state", test_resume_idle_state);
    run_test("linked blocks", test_linked_blocks);
    run_test("strided blocks", test_strided_blocks);
    run_test("standard 2D", test_standard_2d);
    if version >= 2 {
        run_test("constant fill", test_constant_fill);
    }
    test_suite_end() as i32
}
