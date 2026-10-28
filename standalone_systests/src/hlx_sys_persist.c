/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hlx_sys_test.h"

int main(void)
{
    uint16_t *buf_a;
    uint16_t *buf_b;
    const uint16_t pat_a = 0x1234;
    const uint16_t pat_b = 0x5678;
    HLX_Vector va;
    HLX_Vector vb;
    HLX_Vector la;
    HLX_Vector lb;
    HLX_Vector sum;
    uint16_t expected;

    init_vtcm();
    enable_hlx();
    hlx_stage = vtcm_alloc(HLX_ASM_STAGE_BYTES);

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    init_hlx_vsp();

    /* Each intrinsic generates a separate packet. */
    va = Q6_Lh_equals_Rh(pat_a);
    Q6_mem_Ku16_new_AL(buf_a, va);

    vb = Q6_Lh_equals_Rh(pat_b);
    Q6_mem_Ku16_new_AL(buf_b, vb);

    /* Reload and add the patterns. */
    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    sum = Q6_Lh_add_LhLh(la, lb);
    Q6_mem_Ku16_new_AL(buf_a, sum);

    expected = pat_a + pat_b;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        if (buf_a[i] != expected) {
            puts("FAIL");
            return 1;
        }
    }

    puts("PASS");
    return 0;
}
