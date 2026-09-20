/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 *
 * Qtimer Example
 *
 * This example initializes two timers that cause interrupts at different
 * intervals.  The thread 0 will sit in wait mode till the interrupt is
 * serviced then return to wait mode.
 *
 */
#include <assert.h>


#include "qtimer.h"

int main()
{
    int i;
    uint32_t subsystem_base = GET_SUBSYSTEM_BASE();
    exit_flag = 0;
    printf("\nCSR base=0x%lx; L2VIC base=0x%lx\n",
           (unsigned long)subsystem_base,
           (unsigned long)(subsystem_base + 0x10000));
    printf("QTimer1 will go off 20 times (once every 1/%d sec).\n",
           (QTMR_FREQ) / (ticks_per_qtimer1));
    printf("QTimer2 will go off 2 times (once every 1/%d sec).\n\n",
           (QTMR_FREQ) / (ticks_per_qtimer2));

    add_translation((void *)subsystem_base, (void *)subsystem_base, 4);

    enable_core_interrupt();

    init_l2vic();
    /* initialize qtimers 1 and 2 */
    init_qtimers(3);

    u32 ver1 = read_ver1();
    printf("QTimer, frame 1 version: '%08x'\n", (int)ver1);
    u32 ver2 = read_ver2();
    printf("QTimer, frame 2 version: '%08x'\n", (int)ver2);
    assert(ver2 == ver1);

    while (qtimer2_cnt < 2) {
        /*
         * Thread 0 waits for interrupts
         * Wait disabled, spin instead: qemu timer bug when
         * all threads are waiting.
         */
        asm_wait();
        printf("qtimer_cnt1 = %d, qtimer_cnt2 = %d\n", qtimer1_cnt,
               qtimer2_cnt);
    }
    printf("PASS\n");
    return 0;
}
