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
 *     - Alias a 4 KiB-aligned two-page backing buffer at a VA just
 *       above the CRT0's 1 MiB global mapping.  Install an explicit
 *       4 KiB TLB entry for the FIRST alias page only -- the second
 *       alias page starts unmapped.
 *     - Swap in a minimal custom event-vector table whose only
 *       functional change is a TLB-miss RW handler that installs the
 *       second-page mapping with the correct PA (the CRT0's default
 *       handler would install a 1 MiB entry that overlaps the first
 *       alias page we already installed, triggering a multi-TLB
 *       match).  Every other vector forwards to a no-op pass-through
 *       so the semihost trap0 return path doesn't clobber r0.
 *     - Call SYS_READ with a buffer that straddles the alias page
 *       boundary.
 *
 * Pre-fix:  hexagon_touch_memory() probes only the first page, our
 *           handler never fires for the second page,
 *           cpu_memory_rw_debug() returns -1 on it, and SYS_READ
 *           reports a short transfer (swi_ret != 0).
 * Post-fix: both pages are probed, our handler installs the second
 *           page, SYS_READ returns 0 and the second-page TLB probe
 *           now hits.
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
#define HEX_SYS_EXCEPTION 0x18

/* ARM-compat semihost open mode 6 == O_RDWR|O_CREAT|O_TRUNC. */
#define SH_OPEN_WRPLUS   6

static uint32_t swi_ret, swi_err, swi_args[4];

/*
 * Pin r0/r1/r2 explicitly via register-asm variables.  Letting the
 * compiler pick input registers and then shuffling them into place
 * with "r0 = %N" is unsafe when optimization is low: the allocator
 * can assign one input to the register another input ends up
 * reading, so the manual moves self-corrupt.  Binding directly to
 * r0/r1/r2 eliminates the aliasing hazard.
 */
#define DO_SWI(CODE, ARG0, ARG1) \
    do { \
        register uint32_t _r0 asm("r0") = (uint32_t)(CODE); \
        register uint32_t _r1 asm("r1") = (uint32_t)(ARG0); \
        register uint32_t _r2 asm("r2") = (uint32_t)(ARG1); \
        asm volatile( \
                "trap0(#0)\n" \
                : "+r"(_r0), "+r"(_r1) \
                : "r"(_r2) \
                : "memory" \
                ); \
        swi_ret = _r0; \
        swi_err = _r1; \
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

/* Globals used by main; keep them static so they land in BSS/data. */
static char g_fname[256];
static uint8_t g_src[XFER_LEN];
static int g_fd;

static volatile uint32_t expected_miss_va;
static volatile uint32_t expected_miss_pa;
static volatile int      miss_count;

/* TLB-miss helper.  Only accept the one miss we expect. */
void my_tlb_helper(uint32_t ssr)
{
    uint32_t badva = getbadva();
    uint32_t va_page = page_start(badva, TARGET_PAGE_BITS);
    (void)ssr;

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

/* Trap0 passthrough: save/restore everything so QEMU's semihost return
 * value in r0 survives the vector dispatch. */
void my_trap0_helper(uint32_t ssr)
{
    (void)ssr;
}

void my_nmi_helper(uint32_t ssr)
{
    (void)ssr;
    do_coredump();
}

MY_EVENT_HANDLE(my_event_handle_tlbmissrw, my_tlb_helper)
MY_EVENT_HANDLE(my_event_handle_trap0,     my_trap0_helper)
MY_EVENT_HANDLE(my_event_handle_nmi,       my_nmi_helper)

DEFAULT_EVENT_HANDLE(my_event_handle_error,       HANDLE_ERROR_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx,    HANDLE_TLBMISSX_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_reset,       HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd,        HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap1,       HANDLE_TRAP1_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int,         HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror,     HANDLE_FPERROR_OFFSET)

/* Exit via HEX_SYS_EXCEPTION (SWI 0x18) with the test's err code in
 * r2.  Bypasses stdlib exit paths that would touch stack pages our
 * TLB-miss handler isn't prepared to service. */
__attribute__((noreturn))
static void sys_exit(uint32_t rc)
{
    register uint32_t r0 asm("r0") = HEX_SYS_EXCEPTION;
    register uint32_t r2 asm("r2") = rc;
    asm volatile("trap0(#0)\n" :: "r"(r0), "r"(r2) : "memory");
    __builtin_unreachable();
}

int main(void)
{
    /*
     * Alias just past the CRT0's 1 MiB global mapping.  The exact
     * value isn't important as long as (a) it is outside the CRT0
     * global map so the TLB-miss handler fires on the unmapped
     * second page, and (b) it is in a VPN whose entry our custom
     * handler can safely install (no overlap with another TLB
     * entry).
     */
    const uint32_t ALIAS_BASE   = 0x00200000u;
    uint32_t backing_pa     = (uint32_t)&backing[0];
    uint32_t first_page_va  = ALIAS_BASE;
    uint32_t second_page_va = ALIAS_BASE + 0x1000;
    uint32_t second_page_pa = backing_pa + 0x1000;
    uint32_t guest_buf      = first_page_va + XFER_OFFSET;

    /* Create a scratch file and populate it with a known pattern.
     * Run the file setup under the CRT0's event vectors so stray
     * stack / libc-reentrant TLB misses are serviced normally. */
    SWI3(HEX_SYS_TMPNAM, g_fname, 0, sizeof(g_fname));
    check32(swi_ret, 0);

    SWI3(HEX_SYS_OPEN, g_fname, SH_OPEN_WRPLUS, strlen(g_fname));
    g_fd = (int)swi_ret;
    chk_error(g_fd);

    for (int i = 0; i < XFER_LEN; i++) {
        g_src[i] = (uint8_t)(i + 0x37);
    }
    SWI3(HEX_SYS_WRITE, g_fd, g_src, XFER_LEN);
    check32(swi_ret, 0);

    SWI2(HEX_SYS_SEEK, g_fd, 0);
    check32(swi_ret, 0);

    /* NOW swap in our custom event vectors.  Install the first alias
     * page and prime the handler. */
    install_my_event_vectors();

    add_trans(1, first_page_va, backing_pa, PAGE_4K,
              TLB_X | TLB_W | TLB_R | TLB_U, 0, 1, 1);
    check32(tlbp(0, first_page_va),  1);
    check32(tlbp(0, second_page_va), TLB_NOT_FOUND);

    expected_miss_va = second_page_va;
    expected_miss_pa = second_page_pa;
    miss_count       = 0;

    /*
     * Pre-fix:  hexagon_touch_memory() probes only the first page,
     *           our handler never fires for the second page,
     *           cpu_memory_rw_debug() returns -1 on it, and SYS_READ
     *           reports XFER_LEN bytes NOT read (swi_ret != 0).
     * Post-fix: both pages are probed, our handler installs the
     *           second-page mapping, SYS_READ returns 0.
     */
    SWI3(HEX_SYS_READ, g_fd, guest_buf, XFER_LEN);
    uint32_t read_ret = swi_ret;
    int observed_miss = miss_count;
    uint32_t probe_result = tlbp(0, second_page_va);

    /* Restore the CRT0 vectors before further semihost activity. */
    setevb(old_evb);

    check32(read_ret, 0);
    check32(observed_miss, 1);
    check32_ne(probe_result, TLB_NOT_FOUND);

    SWI1(HEX_SYS_CLOSE, g_fd);
    check32(swi_ret, 0);
    SWI2(HEX_SYS_REMOVE, g_fname, strlen(g_fname));

    printf("%s\n", err ? "FAIL" : "PASS");
    sys_exit(err ? 1 : 0);
}
