/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mcw.h"

void mcw_master_set_portmask(uint32_t mcid, uint32_t port_mask)
{
    /* MMIO: master MCIDLUT_CMD_L (write triggers the LUT op). */
    volatile uint32_t *cmd_l = (volatile uint32_t *)
        (MCW_MASTER_BASE + MCW_MASTER_MCIDLUT_CMD_L);
    uint32_t val = (mcid & 0x7fff)
                 | MASTER_CMD_WRITE
                 | ((port_mask & 0xf) << 19);
    *cmd_l = val;
}

void mcw_master_set_mask(uint32_t val)
{
    /* MMIO: master MCID_MASK_EN gates writes to MCID_MASK. */
    volatile uint32_t *mask_en = (volatile uint32_t *)
        (MCW_MASTER_BASE + MCW_MASTER_MCID_MASK_EN);
    /* MMIO: master MCID_MASK (initial value: allow all 4 ports). */
    volatile uint32_t *mask = (volatile uint32_t *)
        (MCW_MASTER_BASE + MCW_MASTER_MCID_MASK);
    *mask_en = 1;
    *mask = val & 0xf;
}

void mcw_slave_set_entry(uint32_t mcid, uint32_t base_addr,
                         uint32_t window_bytes)
{
    /* MMIO: slave MCIDLUT_CMD_H stages the (base, max_offset) data. */
    volatile uint32_t *cmd_h = (volatile uint32_t *)
        (MCW_SLAVE_BASE + MCW_SLAVE_MCIDLUT_CMD_H);
    /* MMIO: slave MCIDLUT_CMD must be written second; it triggers the op. */
    volatile uint32_t *cmd = (volatile uint32_t *)
        (MCW_SLAVE_BASE + MCW_SLAVE_MCIDLUT_CMD);

    /*
     * Both register fields are in 2 KB-page units (the low 11 bits of
     * an incoming offset are the byte offset within the page, the high
     * 12 are bounds-checked against max_offset).
     */
    uint32_t base_pages = base_addr >> 11;
    uint32_t max_offset = (window_bytes - 1) >> 11;

    /* CMD_H: pack base_addr (bits [25:12]) and max_offset (bits [11:0]) */
    *cmd_h = ((base_pages & 0x3fff) << 12) | (max_offset & 0xfff);

    *cmd = (mcid & 0x7fff) | SLAVE_CMD_WRITE;
}

void mcw_fanout_write32(uint32_t mcid, uint32_t offset, uint32_t val)
{
    /* MMIO: master fanout window. The store turns into a NoC multicast. */
    volatile uint32_t *addr = (volatile uint32_t *)
        (MCW_FANOUT_BASE + ((uint32_t)mcid << 23) + offset);
    *addr = val;
}
