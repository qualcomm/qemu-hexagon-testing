/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mmu.h"

#define GSR_CFI (1 << 28)
#define CCR_GEE (1 << 26)
#define CAUSE_STACK_LIMIT 0x27

#define READ_REG(reg) ({ \
    uint32_t value; \
    asm volatile("%0 = " #reg : "=r"(value) :: "memory"); \
    value; \
})
#define WRITE_REG(reg, value) \
    asm volatile(#reg " = %0" :: "r"(value) : "memory")

extern char guest_vectors[];

/* Return to monitor mode after trap1 #2 from the guest. */
void monitor_return(uint32_t ssr)
{
    SET_FIELD(ssr, SSR_UM, 0);
    SET_FIELD(ssr, SSR_GM, 0);
    setssr(ssr);
}
MY_EVENT_HANDLE(my_event_handle_trap1, monitor_return)
DEFAULT_EVENT_HANDLE(my_event_handle_error,     HANDLE_ERROR_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_nmi,       HANDLE_NMI_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissrw, HANDLE_TLBMISSRW_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx,  HANDLE_TLBMISSX_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_reset,     HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd,      HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap0,     HANDLE_TRAP0_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int,       HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror,   HANDLE_FPERROR_OFFSET)

/*
 * Slot 0 takes a stack-limit fault with CFI active; slot 1 takes a load
 * permission fault. Check both BADVA selections and the saved CFI state.
 */
static void __attribute__((noinline)) test_guest_event(unsigned slot)
{
    uint32_t sp = READ_REG(sp);
    uint32_t fault_addr = (uint32_t)&data + ONE_MB;
    uint32_t expected_badva = slot == 0 ? sp - 8 : fault_addr;
    uint32_t expected_cause = slot == 0 ? CAUSE_STACK_LIMIT :
                                        HEX_CAUSE_PRIV_NO_READ;

    if (slot == 1) {
        /* A readable slot-0 load leaves the permission fault in slot 1. */
        uint32_t page = page_start((uint32_t)&data, TARGET_PAGE_BITS);
        uint64_t mapping = create_mmu_entry(1, 0, 0, 0, page + ONE_MB,
                                           0, 0, 0, 1, 0, page, PAGE_4K);
        tlbw(mapping, ONE_MB_ENTRY);
    }

    WRITE_REG(s11, (uint32_t)guest_vectors); /* GEVB */
    WRITE_REG(badva0, 0xbad00000);
    WRITE_REG(badva1, 0xbad00001);
    WRITE_REG(g3, 0xbad00002); /* Do not accept a stale GBADVA. */
    WRITE_REG(framelimit, slot == 0 ? sp : 0);
    asm volatile("r0 = ssr\n"
                 "r0 = setbit(r0, #%[um])\n"
                 "r0 = clrbit(r0, #%[bvs])\n"
                 "ssr = r0\n"
                 "p0 = cmp.eq(%[slot], #0)\n"
                 "if (!p0) jump 2f\n"
                 "r0 = ##1f\n"
                 ".word 0x5200c000\n"     /* jumpr_rb r0: CFI active */
                 "1:\n"
                 ".word 0xa09de000\n"     /* allocframe_rb(r29, #0):raw */
                 "jump guest_event_done\n"
                 "2:\n"
                 "{\n"
                 "    r1 = memw(%[bad])\n"  /* slot 1 */
                 "    r2 = memw(%[good])\n" /* slot 0 */
                 "}\n"
                 ".global guest_event_done\n"
                 "guest_event_done:\n"
                 :
                 : [slot] "r"(slot), [bad] "r"(fault_addr),
                   [good] "r"(&data),
                   [um] "i"(reg_field_info[SSR_UM].offset),
                   [bvs] "i"(reg_field_info[SSR_BVS].offset)
                 : "r0", "r1", "r2", "p0", "memory");
    check32(READ_REG(sp), sp);
    WRITE_REG(framelimit, 0);
    check32(READ_REG(g1) & (GSR_CFI | 0xff),
            expected_cause | (slot == 0 ? GSR_CFI : 0));
    check32(READ_REG(g3), expected_badva);
    if (slot == 1) {
        tlbw(0, ONE_MB_ENTRY);
    }
}

int main(void)
{
    install_my_event_vectors();
    WRITE_REG(ccr, READ_REG(ccr) | CCR_GEE);
    test_guest_event(0);
    test_guest_event(1);
    puts(err ? "FAIL" : "PASS");
    return err;
}
