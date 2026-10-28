/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hlx_sys_test.h"

static inline uint32_t read_hlx_vsp(void)
{
    uint32_t vsp;

    asm volatile("%[vsp] = c29\n" : [vsp] "=r"(vsp));
    return vsp;
}

static inline void write_hlx_vsp(uint32_t vsp)
{
    asm volatile("c29 = %[vsp]\n" : : [vsp] "r"(vsp));
}

int main(void)
{
    uint16_t *buf0;
    uint16_t *buf1;
    uint16_t *buf2;
    uint16_t *buf3;
    uint16_t *out;
    uint8_t *vsp_area;
    uint32_t vsp_top;
    const uint16_t pats[] = { 100, 200, 300, 400 };
    uint8_t *slot;
    HLX_Vector l0;
    HLX_Vector l1;
    HLX_Vector l2;
    HLX_Vector l3;
    HLX_Vector sum01;
    HLX_Vector sum23;
    HLX_Vector total;
    uint16_t expected;

    init_vtcm();
    enable_hlx();
    hlx_stage = vtcm_alloc(HLX_ASM_STAGE_BYTES);

    buf0 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf1 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf2 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf3 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));

    /* VSP grows downward. */
    vsp_area = vtcm_alloc(HLX_VSP_SIZE);
    vsp_top = (uint32_t)(uintptr_t)vsp_area + HLX_VSP_SIZE;

    write_hlx_vsp(vsp_top);
    if (read_hlx_vsp() != vsp_top) {
        puts("FAIL");
        return 1;
    }

    /* Store four patterns. */
    Q6_mem_Ku16_new_AL(buf0, Q6_Lh_equals_Rh(pats[0]));
    Q6_mem_Ku16_new_AL(buf1, Q6_Lh_equals_Rh(pats[1]));
    Q6_mem_Ku16_new_AL(buf2, Q6_Lh_equals_Rh(pats[2]));
    Q6_mem_Ku16_new_AL(buf3, Q6_Lh_equals_Rh(pats[3]));

    /* Reload and spill below VSP. */
    slot = (uint8_t *)(uintptr_t)read_hlx_vsp();
    Q6_mem_Ku16_new_AL(slot - 1 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf0));
    Q6_mem_Ku16_new_AL(slot - 2 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf1));
    Q6_mem_Ku16_new_AL(slot - 3 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf2));
    Q6_mem_Ku16_new_AL(slot - 4 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf3));

    /* Reload and sum the spills. */
    l0 = Q6_L_tmp_mem_A(slot - 1 * HLX_VEC_BYTES);
    l1 = Q6_L_tmp_mem_A(slot - 2 * HLX_VEC_BYTES);
    l2 = Q6_L_tmp_mem_A(slot - 3 * HLX_VEC_BYTES);
    l3 = Q6_L_tmp_mem_A(slot - 4 * HLX_VEC_BYTES);

    sum01 = Q6_Lh_add_LhLh(l0, l1);
    sum23 = Q6_Lh_add_LhLh(l2, l3);
    total = Q6_Lh_add_LhLh(sum01, sum23);
    Q6_mem_Ku16_new_AL(out, total);

    expected = pats[0] + pats[1] + pats[2] + pats[3];
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        if (out[i] != expected) {
            puts("FAIL");
            return 1;
        }
    }

    puts("PASS");
    return 0;
}
