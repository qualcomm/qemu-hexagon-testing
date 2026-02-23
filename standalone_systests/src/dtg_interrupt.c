/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Direct-to-Guest Interrupt Test
 *
 * Verifies that interrupts from the VIC1 interface can be delivered
 * directly to Guest mode, bypassing Monitor mode.
 *
 * Test overview:
 *   1. Set up a Guest Event Vector Table (GEVB) with a handler at
 *      event 16 (offset 0x40) for direct-to-guest interrupts.
 *   2. Configure CCR: set VV1 (bit 29) to virtualize VIC1 interrupts,
 *      GIE (bit 24) to enable guest interrupts, and GRE (bit 27) to
 *      enable VMRTE for returning from the guest event handler.
 *   3. Set VID with a known VID1 value so GSR.CAUSE captures it.
 *   4. Post a software interrupt on L1 interrupt 3 (VIC1) from Monitor
 *      mode.  With VV1=1, the interrupt pends until Guest mode.
 *   5. Enter Guest mode via RTE with SSR.GM=1.
 *   6. The hardware delivers the pending interrupt to Guest mode:
 *      - PC jumps to GEVB + 0x40 (event 16)
 *      - GSR.CAUSE = VID1 value
 *      - GSR.UM = ~SSR.GM = 0
 *      - GSR.GIE = old CCR.GIE = 1
 *      - CCR.GIE = 0
 *   7. The guest event handler records delivery, returns via VMRTE.
 *   8. Guest code exits to monitor via trap0 with custom EVB handler.
 *   9. Verify results from monitor mode.
 */

#include <stdio.h>
#include <stdint.h>

static int err;
#include "hex_test.h"

/* ---------- Register access helpers ---------- */

static inline uint32_t get_ccr(void)
{
    uint32_t v;
    asm volatile("%0 = ccr" : "=r"(v));
    return v;
}

static inline void set_ccr(uint32_t v)
{
    asm volatile("ccr = %0" : : "r"(v));
}

static inline void set_gevb(uint32_t v)
{
    asm volatile("gevb = %0" : : "r"(v));
}

static inline uint32_t get_gsr(void)
{
    uint32_t v;
    asm volatile("%0 = g1" : "=r"(v));
    return v;
}

static inline void set_gsr(uint32_t v)
{
    asm volatile("g1 = %0" : : "r"(v));
}

static inline void set_vid(uint32_t v)
{
    asm volatile("vid = %0\n\tisync" : : "r"(v));
}

static inline void set_imask(uint32_t v)
{
    asm volatile("imask = %0" : : "r"(v));
}

static inline void do_isync(void)
{
    asm volatile("isync");
}

/* ---------- CCR bit definitions ---------- */
#define CCR_GIE  (1u << 24)    /* Guest Interrupt Enable */
#define CCR_GTE  (1u << 25)    /* Guest Trap Enable */
#define CCR_GEE  (1u << 26)    /* Guest Error Enable */
#define CCR_GRE  (1u << 27)    /* Guest Return Enable (VMRTE) */
#define CCR_VV1  (1u << 29)    /* Virtualize VIC1 */

/* L1 interrupt 3 = VIC1 interface */
#define VIC1_L1_INT  3
#define VIC1_SWI_BIT (1u << VIC1_L1_INT)

/* Test VID value to program before the interrupt */
#define TEST_VID1_VALUE  0x42

/* GSR field positions */
#define GSR_CAUSE_MASK  0xFFFFu
#define GSR_UM_BIT      31
#define GSR_GIE_BIT     30

/*
 * Flag set by the guest event handler (in assembly).
 * Written directly by the asm DTG handler.
 */
volatile int dtg_isr_entered;

/* Detect if monitor-mode handler fires instead */
static volatile int monitor_isr_entered;

/* Symbols defined in dtg_interrupt_asm.S */
extern char guest_event_vectors[];
extern void enter_guest_mode(uint32_t swi_mask);

/* From the hexagon standalone runtime */
void register_interrupt(int intno, void (*handler)(int intno));

static void monitor_int3_handler(int intno)
{
    monitor_isr_entered = 1;
}

/*
 * Test 1: Basic guest mode entry and exit (no interrupt).
 * Verify we can enter guest mode and return to monitor mode
 * via the trap0 exit mechanism.
 */
static void test_guest_mode_roundtrip(void)
{
    uint32_t ccr;

    printf("guest mode entry/exit roundtrip\n");

    /* Configure CCR: GRE for VMRTE, ensure GTE=0 for trap0 exit */
    ccr = get_ccr();
    ccr |= CCR_GRE;
    ccr &= ~CCR_GTE;
    set_ccr(ccr);
    do_isync();

    /* Set up GEVB (needed even if no interrupt fires) */
    set_gevb((uint32_t)(uintptr_t)guest_event_vectors);
    do_isync();

    /* Enter guest mode with no SWI, just roundtrip */
    enter_guest_mode(0);

    printf("  returned from guest mode successfully\n");

    /* Clean up */
    ccr = get_ccr();
    ccr &= ~CCR_GRE;
    set_ccr(ccr);
    do_isync();
}

/*
 * Test 2: Direct-to-guest interrupt delivery via VIC1.
 */
static void test_dtg_interrupt_vic1(void)
{
    uint32_t ccr, gsr;

    printf("direct-to-guest interrupt delivery (VIC1)\n");

    dtg_isr_entered = 0;
    monitor_isr_entered = 0;

    /* Register a monitor-mode handler as a fallback detector */
    register_interrupt(VIC1_L1_INT, monitor_int3_handler);

    /*
     * Set VID with our test value in the VID1 field.
     * VID register: VID1[25:16] | VID0[9:0].
     * VID1 corresponds to VIC1 (L1 interrupt 3).
     */
    set_vid((TEST_VID1_VALUE & 0x3ff) << 16);

    /* Set up the Guest Event Vector Base */
    set_gevb((uint32_t)(uintptr_t)guest_event_vectors);
    do_isync();

    /* Clear guest status register so we can verify hardware sets it */
    set_gsr(0);

    /*
     * Configure CCR:
     *  - VV1:  virtualize VIC1 -> direct-to-guest
     *  - GIE:  enable guest interrupts
     *  - GRE:  enable VMRTE in guest mode (for DTG handler return)
     *  - ~GTE: ensure trap0 goes to monitor (not guest event table)
     */
    ccr = get_ccr();
    ccr |= CCR_VV1 | CCR_GIE | CCR_GRE;
    ccr &= ~CCR_GTE;
    set_ccr(ccr);
    do_isync();

    /* Unmask interrupt 3 for this thread, mask all others */
    set_imask(~VIC1_SWI_BIT);

    /*
     * Enter guest mode.  The assembly function posts the SWI from
     * monitor mode before doing rte into guest mode.  With VV1=1,
     * the pending VIC1 interrupt fires as a DTG interrupt at
     * GEVB + 0x40 (event 16).
     */
    enter_guest_mode(VIC1_SWI_BIT);

    /*
     * Now back in monitor mode.  Read GSR which retains the values
     * set by hardware on DTG entry -- neither VMRTE nor the trap0
     * exit path modify GSR.
     */
    gsr = get_gsr();

    printf("  dtg_isr_entered: %d (expected 1)\n", dtg_isr_entered);
    check32(dtg_isr_entered, 1);

    printf("  monitor_isr_entered: %d (expected 0)\n", monitor_isr_entered);
    check32(monitor_isr_entered, 0);

    printf("  GSR.CAUSE: 0x%lx (expected 0x%x)\n",
           (unsigned long)(gsr & GSR_CAUSE_MASK), TEST_VID1_VALUE);
    check32(gsr & GSR_CAUSE_MASK, TEST_VID1_VALUE);

    /*
     * GSR.UM = ~SSR.GM at DTG entry.  The DTG fires at the rte->guest
     * transition boundary, so hardware captures the pre-guest SSR.GM=0,
     * giving GSR.UM = ~0 = 1.
     */
    printf("  GSR.UM: %lu (expected 1)\n",
           (unsigned long)((gsr >> GSR_UM_BIT) & 1));
    check32((gsr >> GSR_UM_BIT) & 1, 1);

    /*
     * GSR.GIE (bit 30) should be 1: we had CCR.GIE=1 before
     * the DTG event, and hardware saves it in GSR.
     */
    printf("  GSR.GIE (saved): %lu (expected 1)\n",
           (unsigned long)((gsr >> GSR_GIE_BIT) & 1));
    check32((gsr >> GSR_GIE_BIT) & 1, 1);

    /* Restore CCR */
    ccr = get_ccr();
    ccr &= ~(CCR_VV1 | CCR_GIE | CCR_GRE);
    set_ccr(ccr);
    do_isync();

    set_imask(0);
}

int main(int argc, char *argv[])
{
    test_guest_mode_roundtrip();
    test_dtg_interrupt_vic1();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
