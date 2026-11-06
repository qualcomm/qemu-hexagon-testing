/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmu.h"

#define ADDRESS(x) ((uint32_t)(uintptr_t)(x))

struct packet_fault {
    uint32_t cause;
};

static volatile struct packet_fault faults[3];
static volatile unsigned fault_count;
static uint32_t resume_pc;

/* The shared wrapper preserves registers across the C fault recorder. */
void record_packet_fault(uint32_t ssr)
{
    unsigned n = fault_count++;
    struct packet_fault f = { .cause = ssr & 255 };

    if (n >= 3 || !resume_pc) {
        printf("FAIL unexpected exception 0x%" PRIx32 "\n",
               f.cause);
        exit(1);
    }
    faults[n] = f;
    asm volatile(
        "elr = %0\n"
        :
        : "r"(resume_pc)
        :
    );
}

MY_EVENT_HANDLE(my_event_handle_error, record_packet_fault)
MY_EVENT_HANDLE(my_event_handle_tlbmissrw, record_packet_fault)
MY_EVENT_HANDLE(my_event_handle_tlbmissx, record_packet_fault)
DEFAULT_EVENT_HANDLE(my_event_handle_reset, HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_nmi, HANDLE_NMI_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd, HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap0, HANDLE_TRAP0_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap1, HANDLE_TRAP1_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int, HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror, HANDLE_FPERROR_OFFSET)

static void test_predicate_read(void)
{
    unsigned old, now;
    int previous_errors = err;

    asm volatile(
        "r0 = #0x12\n"
        "p3:0 = r0\n"
        "{\n"
        "    p0 = cmp.eq(r0,r0)\n"
        "    %0 = p3:0\n"
        "}\n"
        "%1 = p3:0\n"
        : "=r"(old), "=r"(now)
        :
        : "r0", "p0", "p1", "p2", "p3"
    );
    /* packet reads old C4 */
    check32(old, 0x12);
    /* packet commits P0 */
    check32(now, 0xff);
    printf("%s predicate_read\n", err == previous_errors ? "PASS" : "FAIL");
}

static void test_usr_mask(void)
{
    unsigned old, value;
    int previous_errors = err;

    asm volatile(
        "%0 = usr\n"
        "r0 = #-1\n"
        "usr = r0\n"
        "%1 = usr\n"
        "usr = %0\n"
        : "=&r"(old), "=&r"(value)
        :
        : "r0"
    );
    /* V85 USR writable bits */
    check32(value, 0x3fffff7f);
    printf("%s usr_mask\n", err == previous_errors ? "PASS" : "FAIL");
}

static void test_call_priority(void)
{
    unsigned lr;
    int previous_errors = err;

    asm volatile(
        "r12 = r31\n"
        "r31 = ##0x12345678\n"
        "r0 = #0\n"
        "p0 = cmp.eq(r0,r0)\n"
        "{\n"
        "    if (p0) jump 1f\n"
        "    call 2f\n"
        "}\n"
        "2:\n"
        "r31 = #0\n"
        "1:\n"
        "%0 = r31\n"
        "r31 = r12\n"
        : "=&r"(lr)
        :
        : "r0", "r12", "r31", "p0"
    );
    /* losing call preserves LR */
    check32(lr, 0x12345678);
    printf("%s call_priority\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char hmx_resume[];
static void test_hmx_address(void)
{
    unsigned marker;
    int previous_errors = err;
    uint32_t saved_ssr;

    asm volatile(
        "%0 = ssr\n"
        : "=r"(saved_ssr)
        :
        :
    );
    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    resume_pc = ADDRESS(hmx_resume);
    asm volatile(
        "r4 = ssr\n"
        "r4 = setbit(r4,#26)\n"
        "ssr = r4\n"
        "r0 = cfgbase\n"
        "r0 = asl(r0,#5)\n"
        "r1 = #56\n"
        "r0 = memw_phys(r1,r0)\n"
        "r0 = asl(r0,#16)\n"
        "r1 = #0\n"
        "r2 = #0\n"
        "r3 = #0\n"
        "r4 = #17\n"
        "{\n"
        "    activation.ub = mxmem(r0,r1)\n"
        "    weight.b = mxmem(r2,r3)\n"
        "    r4 = add(r4,#1)\n"
        "}\n"
        ".global hmx_resume\n"
        "hmx_resume:\n"
        "%0 = r4\n"
        : "=r"(marker)
        :
        : "r0", "r1", "r2", "r3", "r4", "memory"
    );
    /* HMX exception count */
    check32(fault_count, 1);
    /* HMX address error */
    check32(faults[0].cause, 0x26);
    /* faulting HMX packet does not commit */
    check32(marker, 17);
    asm volatile(
        "ssr = %0\n"
        :
        : "r"(saved_ssr)
        : "memory"
    );
    resume_pc = 0;
    printf("%s hmx_address\n", err == previous_errors ? "PASS" : "FAIL");
}

int main(void)
{
    test_predicate_read();
    test_usr_mask();
    test_call_priority();
    enter_kernel_mode();
    install_my_event_vectors();
    test_hmx_address();
    setevb(old_evb);
    printf("%s packet_tests: 4 cases\n", err ? "FAIL" : "PASS");
    return err != 0;
}
