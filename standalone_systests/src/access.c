/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <unistd.h>
int
main()
{
    int pass = access ("_testfile_access", (R_OK | W_OK));
    int fail = access ((const char *)0, (R_OK | W_OK | X_OK));
    return (pass | !fail);
}
