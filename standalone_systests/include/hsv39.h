/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef HSV39_H
#define HSV39_H

#include <assert.h>
#include <hexagon_standalone.h>
#define NO_DEFAULT_EVENT_HANDLES
#include "mmu.h"

/* HSV39 (Hexagon System Virtualization 39-bit) definitions */

/* Returns 4 ^ n */
static inline uint64_t pow4(uint64_t n)
{
    return 1ull << (n * 2);
}

/* HSV39 page size enumeration - powers of 4 encoding */
typedef enum {
    HSV39_PAGE_1M   = 0x01,
    HSV39_PAGE_4M   = 0x04,
    HSV39_PAGE_16M  = 0x10,
    HSV39_PAGE_256M = 0x100,
    HSV39_PAGE_1G   = 0x400,
    HSV39_PAGE_4G   = 0x1000,
    HSV39_PAGE_16G  = 0x4000,
    HSV39_PAGE_64G  = 0x10000,
} HSV39_PageSize;

/* Extended TLB entry for 64-bit addressing */
typedef union {
    struct {
        uint64_t V:1;       /* Valid bit */
        uint64_t G:1;       /* Global bit */
        uint64_t ASID:7;    /* Address Space ID */
        uint64_t VPN:28;    /* Virtual Page Number */
        uint64_t U:1;       /* User bit */
        uint64_t R:1;       /* Read bit */
        uint64_t W:1;       /* Write bit */
        uint64_t X:1;       /* Execute bit */
        uint64_t C:3;       /* Cache attributes */
        uint64_t PPN:36;    /* Physical Page Number (36-bit) */
        uint64_t reserved:14;
    };
    uint64_t raw;
} TLBEntry64;

/* HSV39 helper functions */
static inline TLBEntry64 hsv39_make_tlb_entry(uint64_t va, uint64_t pa,
                                               HSV39_PageSize page_size,
                                               uint32_t xwru, uint32_t asid,
                                               bool G)
{
    TLBEntry64 entry = {0};
    uint64_t page_mask = pow4(__builtin_ctzll(page_size)) * 1024 * 1024 - 1;

    entry.V = 1;
    entry.G = G ? 1 : 0;
    entry.ASID = asid & 0x7f;
    entry.VPN = (va & ~page_mask) >> 12;
    entry.U = (xwru & 0x1) ? 1 : 0;
    entry.R = (xwru & 0x2) ? 1 : 0;
    entry.W = (xwru & 0x4) ? 1 : 0;
    entry.X = (xwru & 0x8) ? 1 : 0;
    entry.C = 0x3; /* Default cache attributes */
    entry.PPN = (pa & ~page_mask) >> 12;

    return entry;
}

static inline void hsv39_write_tlb_entry(uint64_t entry_raw, int index)
{
    tlbw(entry_raw, index);
}

static inline void remove_hsv39_trans(int index)
{
    TLBEntry64 entry;
    entry.raw = tlbr(index);
    entry.V = 0;
    tlbw(entry.raw, index);
}

static inline uint32_t tlbp64(uint32_t asid, uint64_t VA)
{
    uint32_t lookup_hi = ((VA >> 32) & 0xfffff) | ((asid & 0x7f) << 20);
    uint32_t lookup_lo = (VA >> 12) & 0xfffff;
    uint32_t ret;

    /* For 64-bit TLB lookup, we need to handle the upper bits appropriately */
    /* This is a simplified implementation - actual HW may differ */
    asm volatile ("%0 = tlbp(%1)\n\t" : "=r"(ret) : "r"(lookup_hi));
    if (ret == TLB_NOT_FOUND) {
        asm volatile ("%0 = tlbp(%1)\n\t" : "=r"(ret) : "r"(lookup_lo));
    }
    return ret;
}

static TLBEntry64 add_hsv39_tlb_entry(int index, uint64_t va, uint64_t pa,
					            HSV39_PageSize page_size,
					            uint32_t xwru, uint32_t asid,
					            bool G, bool V)
{

    TLBEntry64 entry = hsv39_make_tlb_entry(va, pa, page_size, xwru, asid, G);
    entry.V = V;
    int32_t lookup_index = tlbp64(asid, va);
    if (lookup_index != TLB_NOT_FOUND) {
        remove_hsv39_trans(lookup_index);
    }
    hsv39_write_tlb_entry(entry.raw, index);
    return entry;
}

static inline uint64_t page_start64(uint64_t addr, uint64_t page_size)
{
    return addr & ~(page_size - 1ull);
}

#endif /* HSV39_H */
