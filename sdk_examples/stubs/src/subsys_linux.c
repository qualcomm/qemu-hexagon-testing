/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Stub implementation of subsys.c for Linux builds
 * Provides no-op subsys_enable() function
 */
#include "subsys.h"

void subsys_enable() {
    // No-op for Linux builds - hardware subsystem control not needed
}
