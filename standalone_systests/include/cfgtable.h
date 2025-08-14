/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef CFGTABLE_H
#define CFGTABLE_H

#include <stdint.h>

static uint32_t read_cfgtable_field(uint32_t offset)
{
    uint32_t val;
    asm volatile("r0 = cfgbase\n\t"
                 "r0 = asl(r0, #5)\n\t"
                 "%0 = memw_phys(%1, r0)\n\t"
                 : "=r"(val)
                 : "r"(offset)
                 : "r0");
    return val;
}

#define GET_SUBSYSTEM_BASE() (read_cfgtable_field(0x8) << 16)
#define GET_FASTL2VIC_BASE() (read_cfgtable_field(0x28) << 16)

static uintptr_t get_vtcm_base(void)
{
#ifdef __hexagon__
    int vtcm_offset = 0x038;

#if __HEXAGON_ARCH__ == 65
    return 0xD8200000L;
#elif __HEXAGON_ARCH__ >= 66
    return read_cfgtable_field(vtcm_offset) << 16;
#endif /* __HEXAGON_ARCH__ */
#else
    void *vtcm =
        calloc(1024 * 1024 * 8, sizeof(char)); /* 8mb largest vtcm for now */
    if (!vtcm) {
        printf("FAIL\n");
        printf("%s: out of memory allocating vtcm\n", __func__);
        exit(-1);
    }
    return (uintptr_t)vtcm;
#endif
}

#endif /* CFGTABLE_H */
