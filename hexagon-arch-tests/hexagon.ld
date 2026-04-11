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

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
