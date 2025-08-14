/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "invalid_opcode.h"

static void run_v68_instruction(void)
{
    asm volatile("r0 = dmpoll\n" : : : "r0");
}

static uint32_t get_rev(void)
{
    uint32_t rev;
    asm volatile("%0 = rev\n" : "=r"(rev));
    return rev & 0xff;
}

INVALID_OPCODE_MAIN("Invalid insn for rev", run_v68_instruction, get_rev() < 0x68)
