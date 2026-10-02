/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>
#include <hexagon_types.h>
#include <hexagon_protos.h>
#include <hexagon_standalone.h>

#include "cfgtable.h"
#include "dma.h"
#include "mmu.h"
#include "vtcm_common.h"

#define SMALL_SIZE (1024 * 1024 * 1)
#define LARGE_SIZE (1024 * 1024 * 4)

unsigned char small_memory[SMALL_SIZE + ALIGN];
unsigned char large_memory[LARGE_SIZE + ALIGN];

unsigned char __attribute__((__aligned__(DESC_ALIGN))) desc_buf1[DESC_ALIGN * 2];
unsigned char __attribute__((__aligned__(DESC_ALIGN))) desc_buf2[DESC_ALIGN * 2];

#define HALF_SIZE(X) ((X) / 2)
#define QUARTER_SIZE(X) ((X) / 4)

#define HEX_CAUSE_VWCTRL_WINDOW_MISS 0x29
#define HEX_CAUSE_PRIV_USER_NO_SINS 0x1b
#define HEX_CAUSE_PRIV_USER_NO_GINS 0x1a

static bool window_miss_seen;

void udma_error_handler(uint32_t ssr)
{
    uint32_t cause = GET_FIELD(ssr, SSR_CAUSE);

    switch (cause) {
    case HEX_CAUSE_VWCTRL_WINDOW_MISS:
        window_miss_seen = true;
        inc_elr(4);
        break;
    case HEX_CAUSE_PRIV_USER_NO_SINS:
    case HEX_CAUSE_PRIV_USER_NO_GINS:
        enter_kernel_mode();
        break;
    default:
        do_coredump();
        break;
    }
}

MY_EVENT_HANDLE(my_event_handle_error, udma_error_handler)

DEFAULT_EVENT_HANDLE(my_event_handle_nmi, HANDLE_NMI_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissrw, HANDLE_TLBMISSRW_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx, HANDLE_TLBMISSX_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_reset, HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd, HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap0, HANDLE_TRAP0_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap1, HANDLE_TRAP1_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int, HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror, HANDLE_FPERROR_OFFSET)

static void get_vwctrl(bool *enable, uint32_t *lo, uint32_t *hi)
{
    uint32_t vwctrl;

    asm volatile("%0 = vwctrl\n" : "=r"(vwctrl));
    *enable = vwctrl >> 31;
    *lo = vwctrl & 0xfff;
    *hi = (vwctrl >> 16) & 0xfff;
}

static void set_vwctrl(bool enable, uint32_t lo, uint32_t hi)
{
    uint32_t vwctrl = (lo & 0xfff) | ((hi & 0xfff) << 16) |
                      (enable << 31);

    asm volatile("vwctrl = %0\n" : : "r"(vwctrl));
}

static void test_vtcm_dma(uint32_t access_addr)
{
    const uint32_t size = 1024;
    uint8_t *src = (uint8_t *)(access_addr + ALIGN);
    uint8_t *dst = src + size / 2;
    hexagon_udma_descriptor_type0_t *desc;

    src = (uint8_t *)((uintptr_t)src & ~(ALIGN - 1));
    desc = aligned_alloc(DESC_ALIGN, DESC_ALIGN);
    if (!desc) {
        printf("FAIL: unable to allocate VTCM DMA descriptor\n");
        exit(-2);
    }
    memset(src, 0x5a, size / 2);
    memset(dst, 0, size / 2);
    *desc = fill_descriptor0(src, dst, size / 2, NULL);

    window_miss_seen = false;
    enter_user_mode();
    do_dmastart(desc);
    check32(window_miss_seen, false);
    check32(memcmp(src, dst, size / 2), 0);
    free(desc);
}

static void test_vwctrl_dma(void)
{
    uint32_t lo, hi;
    uint32_t initial_lo, initial_hi;
    uintptr_t vtcm_base;
    bool enabled;

    install_my_event_vectors();
    setup_default_vtcm();
    vtcm_base = get_vtcm_base();
    get_vwctrl(&enabled, &lo, &hi);
    initial_lo = lo;
    initial_hi = hi;

    printf("VTCM DMA at 0x%08" PRIxPTR " with VWCTRL [%" PRIu32 ", %"
           PRIu32 "]\n", vtcm_base, lo, hi);
    test_vtcm_dma(vtcm_base + lo * 4096);

    set_vwctrl(false, initial_lo, initial_hi);
    get_vwctrl(&enabled, &lo, &hi);
    check32(enabled, false);
    test_vtcm_dma(vtcm_base + initial_lo * 4096);

    set_vwctrl(true, initial_lo + 1, initial_hi);
    get_vwctrl(&enabled, &lo, &hi);
    check32(enabled, true);
    test_vtcm_dma(vtcm_base + initial_lo * 4096);

    set_vwctrl(true, initial_lo, initial_hi);
}

void test(bool use_small, const char *err_msg)
{
    const char *ofname = "memory.dat";
    unsigned char *memory = use_small ? small_memory : large_memory;
    size_t alloc_size = use_small ? SMALL_SIZE : LARGE_SIZE;

    /* init data area */
    memory += ALIGN;
    memory = (unsigned char *)((uintptr_t)memory & (~(ALIGN - 1)));
    unsigned char *src1 = memory;
    unsigned char *src2 = memory + QUARTER_SIZE(alloc_size);
    memset(src1, 0xAA,
           DMA_XFER_SIZE(alloc_size)); /* fill source memory area 1 */
    memset(src2, 0xBB,
           DMA_XFER_SIZE(
               alloc_size)); /* fill source memory area 2 : different value */
    printf("memory at %p: src1 %p: src2 %p\n", memory, src1, src2);

    /* now init descriptors */
    hexagon_udma_descriptor_type0_t *desc0_1, *desc0_2;
    desc0_1 = (hexagon_udma_descriptor_type0_t *)desc_buf1;
    desc0_2 = (hexagon_udma_descriptor_type0_t *)desc_buf2;
    printf("aligned: desc0_1 at %p, desc0_2 at %p\n", desc0_1, desc0_2);

    unsigned char *dst1 = memory + HALF_SIZE(alloc_size);
    unsigned char *dst2 =
        memory + HALF_SIZE(alloc_size) + QUARTER_SIZE(alloc_size);
    printf("malloc memory at %p: dst1 %p: dst2 %p\n", memory, dst1, dst2);
    *desc0_1 = fill_descriptor0(src1, dst1, DMA_XFER_SIZE(alloc_size),
                                desc0_2); /* chain two descriptors together */
    *desc0_2 = fill_descriptor0(src2, dst2, DMA_XFER_SIZE(alloc_size),
                                NULL); /* end of chain */

    /* kick off dma */
    do_dmastart(desc0_1);

    /* validate transfer is correct */
    int fail = 0;
    if (memcmp(src1, dst1, DMA_XFER_SIZE(alloc_size)) != 0) {
        printf("first dma transfer failed\n");
        fail = 1;
    }
    if (memcmp(src2, dst2, DMA_XFER_SIZE(alloc_size)) != 0) {
        printf("second dma transfer failed\n");
        fail = 1;
    }
    if (fail) {
        printf("FAIL\n");
        printf("NOTE: %s\n", err_msg);
        exit(-3);
    }
}

int main(int argc, char **argv)

{
    test(true, "General DMA failure");
    test(false, "Preload of dst buffers probably missing");
    test_vwctrl_dma();
    printf("%s\n", err ? "FAIL" : "PASS");
    exit(err);
}
