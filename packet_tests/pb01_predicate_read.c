/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb01_predicate_read";

int main(void)
{
    unsigned old, now;
    asm volatile("r0 = #0x12; p3:0 = r0\n"
                 "{ p0 = cmp.eq(r0,r0); %0 = p3:0 }\n"
                 "%1 = p3:0"
                 : "=r"(old), "=r"(now) : : "r0", "p0", "p1", "p2", "p3");
    check(old, 0x12, "packet reads old C4");
    check(now, 0xff, "packet commits P0");
    return 0;
}
