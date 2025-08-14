/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "hexagon_standalone.h"


static inline void k0lock(void)
{
    asm volatile("k0lock\n");
}

int main(int argc, char *argv[])
{
    k0lock();
    k0lock();

/* Getting here is not an option, if we do the test has failed. */
    return 1;
}
