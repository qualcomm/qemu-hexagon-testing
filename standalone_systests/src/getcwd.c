/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#include <stdio.h>
#include <unistd.h>

int
main()
{
    int rc = 0;
    char buf[2048];
    char *path;

    path = getcwd((char *)&buf[0], 2048);
    if (path  == (char *)0) {
        printf("getcwd didn't return path.\n");
        rc++;
    }

    return rc;
}
