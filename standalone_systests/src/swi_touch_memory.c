/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Regression test for a QEMU off-by-one in hexagon_touch_memory().
 *
 * The ARM-compat semihosting SYS_READ / SYS_WRITE paths touch the guest
 * buffer with probe_access() before performing the host-side I/O so that
 * every page the buffer spans is faulted into the TLB.  The subsequent
 * copy uses cpu_memory_rw_debug(), which does NOT trigger a TLB-miss
 * exception on an unmapped page -- it simply returns -1.
 *
 * Before fded2b15c0, hexagon_touch_memory() used the user-supplied length
 * verbatim after rounding the starting address down to a page boundary.
 * For a non-page-aligned start_addr whose buffer extends into a second
 * page, `MIN(length, pagelen)` consumed all the "length" on the first
 * page and the loop exited without probing the final page.
 *
 * Strategy
 *     - Define a two-page backing buffer at a known physical address.
 *     - Install an explicit TLB entry for the FIRST alias page only.
 *     - Replace the event-vector table: our tlbmissrw helper installs
 *       the second-page mapping on demand; our trap0 vector preserves
 *       r0/r1 so the semihost return value survives.
 *     - Call SYS_READ with a buffer that starts near the end of the
 *       mapped page and crosses the boundary into the un-mapped one.
 *
 * Pre-fix:  hexagon_touch_memory() probes only the first page, our
 *           TLB-miss handler is never invoked for the second page,
 *           cpu_memory_rw_debug() returns -1 on it, and SYS_READ
 *           reports XFER_LEN bytes NOT read (short transfer).
 *
 * Post-fix: both pages are probed, our handler installs the second
 *           page, cpu_memory_rw_debug() succeeds for both, SYS_READ
 *           returns 0 (all bytes read), and miss_count == 1.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mmu.h"

/* Semihost SWI codes we use directly. */
#define HEX_SYS_OPEN     0x01
#define HEX_SYS_CLOSE    0x02
#define HEX_SYS_WRITE    0x05
#define HEX_SYS_READ     0x06
#define HEX_SYS_SEEK     0x0a
#define HEX_SYS_TMPNAM   0x0d
#define HEX_SYS_REMOVE   0x0e

/* ARM-compat semihost open mode 6 == O_RDWR|O_CREAT|O_TRUNC. */
#define SH_OPEN_WRPLUS   6

static uint32_t swi_ret, swi_err, swi_args[4];

#define DO_SWI(CODE, ARG0, ARG1) \
    do { \
        asm volatile( \
                "r0 = %2\n" \
                "r1 = %3\n" \
                "r2 = %4\n" \
                "trap0(#0)\n" \
                "%0 = r0\n" \
                "%1 = r1\n" \
                : "=r"(swi_ret), "=r"(swi_err) \
                : "r"(CODE), "r"(ARG0), "r"(ARG1) \
                : "r0", "r1", "r2", "memory" \
                ); \
    } while (0)

#define SWI0(CODE) DO_SWI(CODE, swi_args, 0)
#define SWI1(CODE, A0) do { swi_args[0] = (uint32_t)(A0); SWI0(CODE); } while (0)
#define SWI2(CODE, A0, A1) \
    do { swi_args[1] = (uint32_t)(A1); SWI1(CODE, A0); } while (0)
#define SWI3(CODE, A0, A1, A2) \
    do { swi_args[2] = (uint32_t)(A2); SWI2(CODE, A0, A1); } while (0)

/* Two-page backing buffer, 4 KiB-aligned. */
static uint8_t backing[2 * 4096] __attribute__((aligned(4096)));

/* 256 bytes straddling a 4 KiB page boundary (0xf80..0x107f). */
#define XFER_LEN     0x100
#define XFER_OFFSET  0xf80

/* Globals read by the TLB-miss handler. */
static volatile uint32_t expected_miss_va;
static volatile uint32_t expected_miss_pa;
static volatile int      miss_count;

/*
 * Our custom TLB-miss RW helper.  Installs the second-page mapping
 * for the alias range used by the test.  Any other miss address is
 * treated as a fatal error.
 */
void my_tlb_helper(uint32_t ssr)
{
    uint32_t cause = GET_FIELD(ssr, SSR_CAUSE);
    uint32_t badva = getbadva();
    uint32_t va_page = page_start(badva, TARGET_PAGE_BITS);

    set_exception_vector_bit(my_exceptions, cause);

    if (cause != HEX_CAUSE_TLBMISSRW_READ &&
        cause != HEX_CAUSE_TLBMISSRW_WRITE) {
        do_coredump();
        return;
    }
    if (va_page != expected_miss_va) {
        do_coredump();
        return;
    }

    uint64_t entry = create_mmu_entry(1, 0, 0, 0, va_page,
                                      1, 1, 1, 1, 0x3,
                                      expected_miss_pa, PAGE_4K);
    tlbw(entry, 2);
    miss_count++;
}

/*
 * Trap0 helper: do nothing.  MY_EVENT_HANDLE saves r0..r31 before
 * entry and restores them before rte, so after QEMU's
 * sim_handle_trap0() has written the semihost return value into r0,
 * the empty helper preserves it on the way out.  This replaces the
 * default DEFAULT_EVENT_HANDLE-generated trampoline whose first
 * instruction clobbers r0 when forwarding to the CRT0 handler.
 */
void my_trap0_helper(uint32_t ssr)
{
    (void)ssr;
}

/*
 * NMI helper (needed by MY_EVENT_HANDLE for nmi).  Just coredump on
 * any NMI we don't expect.
 */
void my_nmi_helper(uint32_t ssr)
{
    (void)ssr;
    do_coredump();
}

MY_EVENT_HANDLE(my_event_handle_tlbmissrw, my_tlb_helper)
MY_EVENT_HANDLE(my_event_handle_trap0,      my_trap0_helper)
MY_EVENT_HANDLE(my_event_handle_nmi,        my_nmi_helper)

DEFAULT_EVENT_HANDLE(my_event_handle_error,       HANDLE_ERROR_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx,    HANDLE_TLBMISSX_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_reset,       HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd,        HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap1,       HANDLE_TRAP1_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int,         HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror,     HANDLE_FPERROR_OFFSET)

int main(void)
{
    uint32_t backing_pa     = (uint32_t)&backing[0];
    uint32_t first_page_va  = backing_pa + ONE_MB;
    uint32_t second_page_va = first_page_va + 0x1000;
    uint32_t second_page_pa = backing_pa + 0x1000;
    uint32_t guest_buf      = first_page_va + XFER_OFFSET;

    install_my_event_vectors();

    /*
     * Explicitly map the FIRST alias page only.  The second page must
     * remain unmapped so that any access to it faults.
     */
    add_trans(1, first_page_va, backing_pa, PAGE_4K,
              TLB_X | TLB_W | TLB_R | TLB_U, 0, 1, 1);
    check32(tlbp(0, first_page_va),  1);
    check32(tlbp(0, second_page_va), TLB_NOT_FOUND);

    expected_miss_va = second_page_va;
    expected_miss_pa = second_page_pa;
    miss_count       = 0;
    clear_exception_vector(my_exceptions);

    /* Create a scratch file and populate it with a known pattern. */
    char fname[256];
    SWI3(HEX_SYS_TMPNAM, fname, 0, sizeof(fname));
    check32(swi_ret, 0);

    SWI3(HEX_SYS_OPEN, fname, SH_OPEN_WRPLUS, strlen(fname));
    int fd = (int)swi_ret;
    chk_error(fd);

    uint8_t src[XFER_LEN];
    for (int i = 0; i < XFER_LEN; i++) {
        src[i] = (uint8_t)(i + 0x37);
    }
    SWI3(HEX_SYS_WRITE, fd, src, XFER_LEN);
    /* SYS_WRITE returns bytes NOT written; expect 0. */
    check32(swi_ret, 0);

    SWI2(HEX_SYS_SEEK, fd, 0);
    check32(swi_ret, 0);

    /* Trash the backing store so a short read is detectable. */
    memset(backing, 0xa5, sizeof(backing));

    SWI3(HEX_SYS_READ, fd, guest_buf, XFER_LEN);
    check32(swi_ret, 0);

    /* The handler must have fired exactly once for the second page. */
    check32(miss_count, 1);
    check32_ne(tlbp(0, second_page_va), TLB_NOT_FOUND);

    /* Verify the data landed in the backing buffer across both pages. */
    for (int i = 0; i < XFER_LEN; i++) {
        check32(backing[XFER_OFFSET + i], src[i]);
    }

    SWI1(HEX_SYS_CLOSE, fd);
    check32(swi_ret, 0);
    SWI2(HEX_SYS_REMOVE, fname, strlen(fname));

    printf("%s\n", err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
