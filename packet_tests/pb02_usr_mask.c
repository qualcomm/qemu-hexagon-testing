/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "common.h"
const char test_name[] = "pb02_usr_mask";
int main(void)
{
    unsigned old, value;
    asm volatile("%0 = usr; r0 = #-1; usr = r0; %1 = usr; usr = %0"
                 : "=&r"(old), "=&r"(value) : : "r0");
    check(value, 0x3fffff7f, "V85 USR writable bits");
    return 0;
}
