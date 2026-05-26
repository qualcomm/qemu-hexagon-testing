/*
 * Helpers for driving the Hexagon Multi-Cast Widget (MCW) from
 * bare-metal tests running on the sa8797p-nsp machine
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef MCW_H
#define MCW_H

#include <stdint.h>

/* MCW register file bases (per-core, same address in each core's view) */
#define MCW_MASTER_BASE       0x1f048000U
#define MCW_SLAVE_BASE        0x1f028000U
#define MCW_FANOUT_BASE       0xc0000000U

/* MCW master register offsets */
#define MCW_MASTER_MCIDLUT_CMD_L  0x004
#define MCW_MASTER_MCID_MASK_EN   0x4000
#define MCW_MASTER_MCID_MASK      0x4004

/* MCW slave register offsets */
#define MCW_SLAVE_MCIDLUT_CMD    0x004
#define MCW_SLAVE_MCIDLUT_CMD_H  0x0e4

/* MCW LUT command encoding (shared field positions across master/slave) */
#define MASTER_CMD_WRITE      (0U << 17)
#define MASTER_CMD_READ       (1U << 17)
#define SLAVE_CMD_WRITE       (0U << 17)

/*
 * Program the master LUT: set MCID's port mask.
 *
 *   port_mask bit N = fan out to core N's slave.
 */
void mcw_master_set_portmask(uint32_t mcid, uint32_t port_mask);

/*
 * Change the master MCID mask value. The mask is AND-ed onto every
 * LUT-derived port mask, so it acts as a global "turn these ports off"
 * filter without rewriting per-MCID entries.
 */
void mcw_master_set_mask(uint32_t mask);
#define mcw_master_enable_all_mask() mcw_master_set_mask(0xf)

/*
 * Program the slave LUT entry for `mcid`:
 *
 *   base_addr    Byte address of the window start in the slave's TCM
 *                address space. Must be 2 KB-aligned (the hardware
 *                field is in 2 KB units).
 *   window_bytes Length of the accepted window in bytes. Granularity is
 *                2 KB: any value in 1..2048 configures the same single-
 *                page window. Must be > 0.
 */
void mcw_slave_set_entry(uint32_t mcid, uint32_t base_addr,
                         uint32_t window_bytes);

/*
 * Issue a 32-bit multicast store: write `val` via the master's fanout
 * window at the given MCID + byte offset.
 */
void mcw_fanout_write32(uint32_t mcid, uint32_t offset, uint32_t val);

#endif /* HEXAGON_TEST_MCW_H */
