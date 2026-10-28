/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hlx_sys_test.h"

int main(void)
{
    uint16_t *buf;
    const uint16_t addend = 1000;
    HLX_Vector ldata;
    HLX_Vector lconst;
    HLX_Vector lresult;

    init_vtcm();
    enable_hlx();
    hlx_stage = vtcm_alloc(HLX_ASM_STAGE_BYTES);

    buf = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    init_hlx_vsp();

    /* Write VTCM scalars. */
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        buf[i] = (uint16_t)i;
    }

    /* Load, add, and store VTCM data. */
    ldata = Q6_L_tmp_mem_A(buf);
    lconst = Q6_Lh_equals_Rh(addend);
    lresult = Q6_Lh_add_LhLh(ldata, lconst);
    Q6_mem_Ku16_new_AL(buf, lresult);

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        uint16_t expected = (uint16_t)(i + addend);
        if (buf[i] != expected) {
            puts("FAIL");
            return 1;
        }
    }

    puts("PASS");
    return 0;
}
