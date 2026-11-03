/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Guest event vector table.
 *
 * GEVB bits 8:0 are hardwired to 0, requiring 512-byte alignment.
 * All events go to guest_event. trap1 #2 is not a virtual instruction,
 * so it traps to the monitor, which resumes in monitor mode at the
 * next instruction.
 */
	.text
	.p2align 9
	.global guest_vectors
guest_vectors:
	.rept 64
	jump guest_event
	.endr

guest_event:
	trap1(r0, #2)
	jump guest_event_done
