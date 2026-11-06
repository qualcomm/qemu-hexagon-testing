/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#include <stdio.h>
#include <stdint.h>

int err;
#include "hex_test.h"

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

int main(void)
{
    test_predicate_read();
    test_usr_mask();
    test_call_priority();
    printf("%s packet_tests: 3 cases\n", err ? "FAIL" : "PASS");
    return err != 0;
}
