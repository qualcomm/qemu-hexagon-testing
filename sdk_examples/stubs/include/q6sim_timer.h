/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Stub header for q6sim_timer.h for Linux builds
 */
#pragma once

// Stub function declarations for Linux
static inline unsigned long long q6sim_read_pcycles(void) {
    return 0ULL;
}

static inline int acquire_vector_unit(int wait) { return 1; }
static inline void set_double_vector_mode(void) { }

#define HEXAGON_VECTOR_WAIT 1
#define SIM_ACQUIRE_HVX acquire_vector_unit(HEXAGON_VECTOR_WAIT)
#define SIM_SET_HVX_DOUBLE_MODE set_double_vector_mode()
