/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Stub header for hwio.h
 * Provides hardware I/O macros for Hexagon subsystem control
 */
#pragma once

#ifdef __linux__
// Stub hardware I/O macros for Linux builds
#define HWIO_OUT(reg, val) do { (void)(reg); (void)(val); } while(0)
#define HWIO_IN(reg) 0
#else
// For QURT/standalone builds, provide actual macros
// These write to memory-mapped hardware registers
#define HWIO_OUT(reg, val) (*((volatile unsigned int *)(reg)) = (val))
#define HWIO_IN(reg) (*((volatile unsigned int *)(reg)))
#endif
