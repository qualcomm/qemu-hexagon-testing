/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef HLX_ASM_H
#define HLX_ASM_H

#include <stdint.h>
#include <string.h>

/*
 * These assembly intrinsics require -mv85 -mhlx.
 * The compiler cannot track L registers, so operands and results pass through
 * a 2KB-aligned staging area. These intrinsics are not reentrant.
 * L0 and L2 hold inputs, L6 holds results, and r9 holds the memory address.
 */
#define HLX_ASM_VEC_BYTES 2048

typedef struct {
    uint16_t h[HLX_ASM_VEC_BYTES / sizeof(uint16_t)];
} HLX_Vector;

struct hlx_scalars {
    uint32_t rt;
    uint32_t pad;
    uint64_t rtt;
    uint32_t addr;
};

#define HLX_ASM_SCALARS_OFFSET (8 * HLX_ASM_VEC_BYTES)
#define HLX_ASM_STAGE_BYTES (HLX_ASM_SCALARS_OFFSET + HLX_ASM_VEC_BYTES)

static uint8_t hlx_stage_bss[HLX_ASM_STAGE_BYTES]
    __attribute__((aligned(HLX_ASM_VEC_BYTES), unused));
static uint8_t *hlx_stage __attribute__((unused)) = hlx_stage_bss;

#define HLX_SCALARS \
    ((struct hlx_scalars *)(hlx_stage + HLX_ASM_SCALARS_OFFSET))

static inline void hlx_put_v(int slot, HLX_Vector v)
{
    memcpy(hlx_stage + slot * HLX_ASM_VEC_BYTES, &v, sizeof(v));
}

static inline HLX_Vector hlx_get_v(int slot)
{
    HLX_Vector v;

    memcpy(&v, hlx_stage + slot * HLX_ASM_VEC_BYTES, sizeof(v));
    return v;
}

#define HLX_CLOBBERS "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", \
                     "r9", "r10", "r12", "r13", "memory"

#define HLX_PTRS \
    "r0 = %[stage]\n" \
    "r1 = add(r0, #2048)\n" \
    "r2 = add(r0, #4096)\n" \
    "r3 = add(r0, #6144)\n" \
    "r4 = add(r0, #8192)\n" \
    "r5 = add(r0, #10240)\n" \
    "r6 = add(r0, #12288)\n" \
    "r7 = add(r0, #14336)\n" \
    "r8 = add(r0, #16384)\n" \
    "r10 = memw(r8 + #0)\n" \
    "r13:12 = memd(r8 + #8)\n" \
    "r9 = memw(r8 + #16)\n"

/* L0.tmp = mem(r0), copied to L0 within the packet. */
#define HLX_LD0 \
    "{\n" \
    "L0.h = L0.h\n" \
    "L0.tmp = mem(r0)\n" \
    "}\n"

#define HLX_LD2 \
    "{\n" \
    "L2.h = L2.h\n" \
    "L2.tmp = mem(r2)\n" \
    "}\n"

/* mem(r6) = L6.new */
#define HLX_ST6 \
    "{\n" \
    "L6.h = L6.h\n" \
    "mem(r6) = L6.new\n" \
    "}\n"

/* L6.tmp = mem(r9), and mem(r9) = L0.new. */
#define HLX_LD_MEM \
    "{\n" \
    "L6.h = L6.h\n" \
    "L6.tmp = mem(r9)\n" \
    "}\n"

#define HLX_ST_MEM \
    "{\n" \
    "L0.h = L0.h\n" \
    "mem(r9) = L0.new\n" \
    "}\n"

static inline HLX_Vector Q6_L_tmp_mem_A(void *base)
{
    HLX_SCALARS->addr = (uint32_t)(uintptr_t)base;
    asm volatile(HLX_PTRS HLX_LD_MEM HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

static inline void Q6_mem_Ku16_new_AL(void *base, HLX_Vector ku)
{
    hlx_put_v(0, ku);
    HLX_SCALARS->addr = (uint32_t)(uintptr_t)base;
    asm volatile(HLX_PTRS HLX_LD0 HLX_ST_MEM
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
}

/* L6.h = add(L0.h, L2.h) */
static inline HLX_Vector Q6_Lh_add_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 "{ L6.h = add(L0.h,L2.h) }\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* L6.h = r10.h */
static inline HLX_Vector Q6_Lh_equals_Rh(uint32_t rt)
{
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS
                 "{ L6.h = r10.h }\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

#endif
