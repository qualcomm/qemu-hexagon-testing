/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmu.h"

#define ADDRESS(x) ((uint32_t)(uintptr_t)(x))

struct packet_fault {
    uint32_t cause, pc, ssr, va0, va1, badva;
};

static volatile struct packet_fault faults[3];
static volatile unsigned fault_count;
static uint32_t resume_pc;

/* The shared wrapper preserves registers across the C fault recorder. */
void record_packet_fault(uint32_t ssr)
{
    unsigned n = fault_count++;
    struct packet_fault f = { .ssr = ssr, .cause = ssr & 255 };

    if (n >= 3 || !resume_pc) {
        printf("FAIL unexpected exception 0x%" PRIx32 "\n",
               f.cause);
        exit(1);
    }
    asm volatile(
        "%0 = elr\n"
        "%1 = badva0\n"
        "%2 = badva1\n"
        "%3 = badva\n"
        : "=r"(f.pc), "=r"(f.va0), "=r"(f.va1), "=r"(f.badva)
        :
        :
    );
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

static void map_page(unsigned index, uint32_t va, uint32_t pa)
{
    uint64_t entry = create_mmu_entry(1, 0, 0, 0, va, 1, 1, 1, 1,
                                      7, pa, PAGE_4K);
    tlbw(entry, index);
    asm volatile(
        "isync\n"
    );
}

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

extern char hlx_resume[];
static void __attribute__((optnone)) test_hlx_disable(void)
{
    static uint16_t input[1024] __attribute__((aligned(2048)));
    static uint16_t output[1024] __attribute__((aligned(2048)));
    int previous_errors = err;
    uint32_t saved_ccr;

    asm volatile(
        "%0 = ccr\n"
        : "=r"(saved_ccr)
        :
        :
    );
    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    for (unsigned i = 0; i < 1024; i++) {
        input[i] = i;
    }
    resume_pc = ADDRESS(hlx_resume);
    asm volatile(
        "r0 = ccr\n"
        "r0 = setbit(r0,#28)\n"
        "ccr = r0\n"
        "r0 = %0\n"
        "r26 = ccr\n"
        "r26 = clrbit(r26,#28)\n"
        "{\n"
        "    ccr = r26\n"
        "    L0.tmp = mem(r0)\n"
        "    L1.h = add(L0.h,L0.h)\n"
        "}\n"
        "{\n"
        "    L0.tmp = mem(r0)\n"
        "    L1.h = add(L0.h,L0.h)\n"
        "}\n"
        ".global hlx_resume\n"
        "hlx_resume:\n"
        "r26 = setbit(r26,#28)\n"
        "ccr = r26\n"
        "r1 = %1\n"
        "{\n"
        "    L2.h = add(L1.h,L1.h)\n"
        "    mem(r1) = L2.new\n"
        "}\n"
        :
        : "r"(input), "r"(output)
        : "r0", "r1", "r26", "memory"
    );
    /* HLX exception count */
    check32(fault_count, 1);
    /* HLX disabled in next packet */
    check32(faults[0].cause, 0x19);
    for (unsigned i = 0; i < 1024; i++) {
        /* first packet HLX result committed */
        check32(output[i], i * 4);
    }
    asm volatile(
        "ccr = %0\n"
        :
        : "r"(saved_ccr)
        : "memory"
    );
    resume_pc = 0;
    printf("%s hlx_disable\n", err == previous_errors ? "PASS" : "FAIL");
}

extern uint32_t fetch_sample[];
extern char fetch_resume[];
asm(
    ".text\n"
    ".global fetch_sample\n"
    "fetch_sample:\n"
    "{\n"
    "    r8 = #1\n"
    "    r9 = #2\n"
    "}\n"
    "{\n"
    "    r8 = #3\n"
    "    r9 = #4\n"
    "}\n"
);
static void test_fetch_cause(void)
{
    static uint32_t code[2048] __attribute__((aligned(4096)));
    int previous_errors = err;
    uint64_t entry = tlbr(1);

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    for (unsigned i = 0; i < 4; i++) {
        code[1022 + i] = fetch_sample[i];
    }
    map_page(1, 0x9ba00000, ADDRESS(code));
    resume_pc = ADDRESS(fetch_resume);
    asm volatile(
        "r0 = ##0x9ba00ff8\n"
        "jumpr r0\n"
        ".global fetch_resume\n"
        "fetch_resume:\n"
        :
        :
        : "r0", "r8", "r9", "memory"
    );
    /* fetch exception count */
    check32(fault_count, 1);
    /* packet starts on missing page */
    check32(faults[0].cause, 0x60);
    /* fetch restart PC */
    check32(faults[0].pc, 0x9ba01000);
    tlbw(entry, 1);
    resume_pc = 0;
    printf("%s fetch_cause\n", err == previous_errors ? "PASS" : "FAIL");
}

extern uint32_t straddle_sample[];
extern char straddle_resume[];
asm(
    ".text\n"
    ".global straddle_sample\n"
    "straddle_sample:\n"
    "{\n"
    "    r8 = #7\n"
    "    r9 = #8\n"
    "}\n"
    "{\n"
    "    r8 = #1\n"
    "    r9 = #2\n"
    "    r10 = #3\n"
    "    r11 = #4\n"
    "}\n"
);
static void test_fetch_pc(void)
{
    static uint32_t code[2048] __attribute__((aligned(4096)));
    unsigned marker;
    int previous_errors = err;
    uint64_t entry = tlbr(1);

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    for (unsigned i = 0; i < 6; i++) {
        code[1020 + i] = straddle_sample[i];
    }
    map_page(1, 0x9ba00000, ADDRESS(code));
    resume_pc = ADDRESS(straddle_resume);
    asm volatile(
        "r8 = #0\n"
        "r0 = ##0x9ba00ff0\n"
        "jumpr r0\n"
        ".global straddle_resume\n"
        "straddle_resume:\n"
        "%0 = r8\n"
        : "=r"(marker)
        :
        : "r0", "r8", "r9", "r10", "r11", "memory"
    );
    /* straddling fetch exception count */
    check32(fault_count, 1);
    /* straddling fetch cause */
    check32(faults[0].cause, 0x61);
    /* restart at straddling packet */
    check32(faults[0].pc, 0x9ba00ff8);
    /* preceding packet retired */
    check32(marker, 7);
    tlbw(entry, 1);
    resume_pc = 0;
    printf("%s fetch_pc\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char slot_resume[];
static void test_fault_slot(void)
{
    int previous_errors = err;

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    resume_pc = ADDRESS(slot_resume);
    asm volatile(
        "r0 = ##0x9b900008\n"
        "r1 = ##0x9b901010\n"
        "p0 = cmp.eq(r0,r1)\n"
        "{\n"
        "    r2 = memw(r0)\n"
        "    if (p0) memw(r1) = r0\n"
        "}:mem_noshuf\n"
        ".global slot_resume\n"
        "slot_resume:\n"
        :
        :
        : "r0", "r1", "r2", "p0", "memory"
    );
    /* slot 1 fault count */
    check32(fault_count, 1);
    /* slot 1 load miss */
    check32(faults[0].cause, 0x70);
    /* slot 1 BVS and validity */
    check32((faults[0].ssr >> 20) & 7, 6);
    /* slot 1 BADVA1 */
    check32(faults[0].va1, 0x9b900008);
    /* slot 1 BADVA alias */
    check32(faults[0].badva, 0x9b900008);
    resume_pc = 0;
    printf("%s fault_slot\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char validity_resume[];
extern char mirror_resume[], predicate_resume[];
static void test_fault_validity(void)
{
    static uint32_t data[8];
    int previous_errors = err;

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    resume_pc = ADDRESS(validity_resume);
    asm volatile(
        "r0 = %0\n"
        "r1 = ##0x9b901010\n"
        "{\n"
        "    r2 = memw(r0)\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global validity_resume\n"
        "validity_resume:\n"
        :
        : "r"(data)
        : "r0", "r1", "r2", "r3", "memory"
    );
    /* dual load fault count */
    check32(fault_count, 1);
    /* slot 0 miss */
    check32(faults[0].cause, 0x70);
    /* both slot addresses valid */
    check32((faults[0].ssr >> 20) & 7, 3);
    /* slot 0 BADVA0 */
    check32(faults[0].va0, 0x9b901010);
    /* slot 1 BADVA1 */
    check32(faults[0].va1, ADDRESS(data));
    resume_pc = ADDRESS(mirror_resume);
    asm volatile(
        "r0 = ##0x9b900008\n"
        "r1 = %0\n"
        "{\n"
        "    r2 = memw(r0)\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global mirror_resume\n"
        "mirror_resume:\n"
        :
        : "r"(data)
        : "r0", "r1", "r2", "r3", "memory"
    );
    /* slot 1 fault with both valid */
    check32((faults[1].ssr >> 20) & 7, 7);
    /* unexecuted slot 0 address */
    check32(faults[1].va0, ADDRESS(data));
    /* slot 1 fault address */
    check32(faults[1].va1, 0x9b900008);
    resume_pc = ADDRESS(predicate_resume);
    asm volatile(
        "r0 = %0\n"
        "r1 = ##0x9b901010\n"
        "{\n"
        "    p0 = cmp.eq(r0,r1)\n"
        "    if (p0.new) r2 = memw(r0++#4)\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global predicate_resume\n"
        "predicate_resume:\n"
        :
        : "r"(data)
        : "r0", "r1", "r2", "r3", "p0", "memory"
    );
    /* all validity cases fault */
    check32(fault_count, 3);
    /* cancelled new-predicate load invalid */
    check32((faults[2].ssr >> 20) & 7, 1);
    resume_pc = 0;
    printf("%s fault_validity\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char inactive_resume[], inactive_mirror[];
static void test_inactive_badva(void)
{
    int previous_errors = err;

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    resume_pc = ADDRESS(inactive_resume);
    asm volatile(
        "r0 = ##0xbad10000\n"
        "badva1 = r0\n"
        "r0 = ##0x9b900008\n"
        "r2 = memw(r0)\n"
        ".global inactive_resume\n"
        "inactive_resume:\n"
        :
        :
        : "r0", "r2", "memory"
    );
    /* inactive BADVA1 preserved */
    check32(faults[0].va1, 0xbad10000);
    /* only slot 0 valid */
    check32((faults[0].ssr >> 20) & 7, 1);
    resume_pc = ADDRESS(inactive_mirror);
    asm volatile(
        "r0 = ##0xbad00000\n"
        "badva0 = r0\n"
        "r0 = ##0x9b900008\n"
        "r1 = ##0x9b901010\n"
        "p0 = cmp.eq(r0,r1)\n"
        "{\n"
        "    r2 = memw(r0)\n"
        "    if (p0) memw(r1) = r0\n"
        "}:mem_noshuf\n"
        ".global inactive_mirror\n"
        "inactive_mirror:\n"
        :
        :
        : "r0", "r1", "r2", "p0", "memory"
    );
    /* inactive address fault count */
    check32(fault_count, 2);
    /* inactive BADVA0 preserved */
    check32(faults[1].va0, 0xbad00000);
    /* only slot 1 valid */
    check32((faults[1].ssr >> 20) & 7, 6);
    resume_pc = 0;
    printf("%s inactive_badva\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char priority_resume[], priority_second[];
static void test_store_priority(void)
{
    static uint32_t data[1024] __attribute__((aligned(4096)));
    int previous_errors = err;
    uint64_t entry = tlbr(1);

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    resume_pc = ADDRESS(priority_resume);
    asm volatile(
        "r0 = ##0x9b900008\n"
        "r1 = ##0x9b901010\n"
        "{\n"
        "    memw(r0) = r0\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global priority_resume\n"
        "priority_resume:\n"
        :
        :
        : "r0", "r1", "r3", "memory"
    );
    /* slot 1 store miss wins */
    check32(faults[0].cause, 0x71);
    /* store slot wins with both valid */
    check32((faults[0].ssr >> 20) & 7, 7);
    /* winning store address */
    check32(faults[0].va1, 0x9b900008);
    map_page(1, 0x9b900000, ADDRESS(data));
    resume_pc = ADDRESS(priority_second);
    asm volatile(
        "r0 = ##0x9b900008\n"
        "r1 = ##0x9b901010\n"
        "{\n"
        "    memw(r0) = r0\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global priority_second\n"
        "priority_second:\n"
        :
        :
        : "r0", "r1", "r3", "memory"
    );
    /* two priority fault cases */
    check32(fault_count, 2);
    /* slot 0 load miss after mapping store */
    check32(faults[1].cause, 0x70);
    /* store not committed before load fault */
    check32(data[2], 0);
    tlbw(entry, 1);
    resume_pc = 0;
    printf("%s store_priority\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char alignment_resume[], alignment_store[], alignment_cross[];
static void test_alignment_priority(void)
{
    static uint32_t data[8];
    int previous_errors = err;

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    resume_pc = ADDRESS(alignment_resume);
    asm volatile(
        "r0 = %0\n"
        "r1 = ##0x9b901011\n"
        "{\n"
        "    memw(r0) = r0\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global alignment_resume\n"
        "alignment_resume:\n"
        :
        : "r"(data)
        : "r0", "r1", "r3", "memory"
    );
    /* misaligned missing load beats TLB miss */
    check32(faults[0].cause, 0x20);
    /* store rolled back on alignment fault */
    check32(data[0], 0);
    resume_pc = ADDRESS(alignment_store);
    asm volatile(
        "r0 = ##0x9b900009\n"
        "r1 = ##0x9b901010\n"
        "{\n"
        "    memw(r0) = r0\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global alignment_store\n"
        "alignment_store:\n"
        :
        :
        : "r0", "r1", "r3", "memory"
    );
    /* misaligned store beats load miss */
    check32(faults[1].cause, 0x21);
    /* alignment fault slot 1 */
    check32((faults[1].ssr >> 20) & 7, 7);
    resume_pc = ADDRESS(alignment_cross);
    asm volatile(
        "r0 = ##0x9b900008\n"
        "r1 = ##0x9b901011\n"
        "{\n"
        "    r2 = memw(r0)\n"
        "    r3 = memw(r1)\n"
        "}:mem_noshuf\n"
        ".global alignment_cross\n"
        "alignment_cross:\n"
        :
        :
        : "r0", "r1", "r2", "r3", "memory"
    );
    /* alignment case count */
    check32(fault_count, 3);
    /* alignment precedes other slot TLB miss */
    check32(faults[2].cause, 0x20);
    /* alignment fault slot 0 */
    check32((faults[2].ssr >> 20) & 7, 3);
    resume_pc = 0;
    printf("%s alignment_priority\n", err == previous_errors ? "PASS" : "FAIL");
}

extern char rollback_resume[];
static void test_store_rollback(void)
{
    static uint32_t data[8] __attribute__((aligned(8)));
    int previous_errors = err;

    fault_count = 0;
    memset((void *)faults, 0, sizeof(faults));
    for (unsigned i = 0; i < 8; i++) {
        data[i] = 0xffe2c5a8;
    }
    resume_pc = ADDRESS(rollback_resume);
    asm volatile(
        "r0 = %0\n"
        "r1 = add(r0,#9)\n"
        "r2 = ##0xc0de0011\n"
        "{\n"
        "    memw(r0) = r2\n"
        "    memw(r1) = r2\n"
        "}:mem_noshuf\n"
        ".global rollback_resume\n"
        "rollback_resume:\n"
        :
        : "r"(data)
        : "r0", "r1", "r2", "memory"
    );
    /* store rollback fault count */
    check32(fault_count, 1);
    /* slot 0 store alignment fault */
    check32(faults[0].cause, 0x21);
    for (unsigned i = 0; i < 8; i++) {
        /* neither store changes memory */
        check32(data[i], 0xffe2c5a8);
    }
    resume_pc = 0;
    printf("%s store_rollback\n", err == previous_errors ? "PASS" : "FAIL");
}

int main(void)
{
    test_predicate_read();
    test_usr_mask();
    test_call_priority();
    enter_kernel_mode();
    install_my_event_vectors();
    test_hmx_address();
    test_hlx_disable();
    test_fetch_cause();
    test_fetch_pc();
    test_fault_slot();
    test_fault_validity();
    test_inactive_badva();
    test_store_priority();
    test_alignment_priority();
    test_store_rollback();
    setevb(old_evb);
    printf("%s packet_tests: 13 cases\n", err ? "FAIL" : "PASS");
    return err != 0;
}
