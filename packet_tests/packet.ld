/* SPDX-License-Identifier: BSD-3-Clause-Clear */
ENTRY(_start)
SECTIONS {
    . = 0;
    .start : { *(.start) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss : {
        *(.bss .bss.* .sbss .sbss.* .scommon*) *(COMMON)
        . = ALIGN(16);
        . += 65536;
        stack_top = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) }
}
