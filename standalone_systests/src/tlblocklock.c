/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "hexagon_standalone.h"


static inline void tlblock(void)
{
    asm volatile("tlblock\n");
}

int main(int argc, char *argv[])
{
    tlblock();
    tlblock();

/* Getting here is not an option, if we do the test has failed. */
    return 1;
}
