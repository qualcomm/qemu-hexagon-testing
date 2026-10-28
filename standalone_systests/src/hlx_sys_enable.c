/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hlx_sys_test.h"

int main(void)
{
    uint16_t *buf;
    const uint16_t pattern = 0xBEEF;
    HLX_Vector v;

    init_vtcm();
    enable_hlx();
    hlx_stage = vtcm_alloc(HLX_ASM_STAGE_BYTES);

    buf = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    init_hlx_vsp();

    v = Q6_Lh_equals_Rh(pattern);
    Q6_mem_Ku16_new_AL(buf, v);

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        if (buf[i] != pattern) {
            puts("FAIL");
            return 1;
        }
    }

    puts("PASS");
    return 0;
}
