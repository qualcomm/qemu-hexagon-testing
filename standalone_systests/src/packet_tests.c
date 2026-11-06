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

int main(void)
{
    test_predicate_read();
    printf("%s packet_tests: 1 case\n", err ? "FAIL" : "PASS");
    return err != 0;
}
