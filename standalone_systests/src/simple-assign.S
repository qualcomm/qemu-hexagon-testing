/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

	.text
	.global main
main:
	r0 = #0xbabebeef
return:
	r2 = #0
	stop(r1)
