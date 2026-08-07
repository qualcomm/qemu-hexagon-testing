/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Minimal linker script for bare-metal Hexagon Rust tests.
 * Provides BSS boundary symbols needed by crt0.S.
 */

SECTIONS {
    .start 0x00000000 : {
        *(.start)
    }

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.* .sdata .sdata.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        __bss_end = .;
    }

    _end = .;

    /*
     * Code that must sit at a known offset inside a 4KB page, far away from
     * the image so the pages around it stay unmapped until a test maps them
     * itself.  0x9b800fe0 puts a run-up of packets at the tail of the page
     * at 0x9b800000 and lets a packet straddle into 0x9b801000; see
     * src/bin/test_pagecross_tlbmissx.rs.  The section is empty (and so
     * dropped by the linker) for every other test.
     */
    .pagecross 0x9b800fe0 : {
        *(.pagecross)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
