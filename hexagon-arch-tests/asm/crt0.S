/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Minimal standalone crt0 for bare-metal Hexagon Rust tests.
 * Provides hardware initialization, TLB miss handling, and event vectors.
 * Calls extern rust_main() and terminates via stop().
 */

	.macro AddrOf Var, To = r0
	\To\() = ## (\Var)
	.endm

	.macro ReadFrom Var, To = r0
	AddrOf \Var, \To
	\To = memw (\To)
	.endm

	.macro WriteTo Var, From = r0, Ptr = r1
	.ifnc "\From", "\Ptr"
	AddrOf \Var, \Ptr
	memw (\Ptr) = \From
	\From = memw (\Ptr)
	.else
	.print "Macro arguments From and Ptr cannot be the same."
	.err
	.endif
	.endm

	.equ TLB_FIXED_ENTRIES, 6
	.equ STACK_SIZE, 0x100000  /* 1MB */

/*======================================================================
 * Entry point - must be at address 0
 *======================================================================*/
	.section .start, "awx", @progbits
	.subsection 0
	.org 0

	.global _start
	.type _start, @function
	.p2align 5
_start:
	jump hexagon_start_init
	jump hexagon_start_main
	.size _start, . - _start

	/* Fixed location data at offset 0x20 */
	.org 0x20
EventVectorBase:
	.word .EventVectors

_NumTLBEntries:
	.word 127

TLBMapTable:
	.word UPTE_START

CoreDump:
	.word RegDump

	.subsection 0

	/* Prevent data/code from sharing an L2 cache-line */
	.p2align 6, 0

/*======================================================================
 * Hardware initialization (runs once at boot on thread 0)
 *======================================================================*/
	.global hexagon_start_init
	.type hexagon_start_init, @function
hexagon_start_init:
.Init:
	/* Clean caches */
	dckill
	isync
	ickill
	isync

.InitSSR:
	r0 = #0
	ssr = r0
	isync

.InitVector:
	ReadFrom EventVectorBase
	evb = r0

.InitStack:
	ReadFrom exc_stack_tops
	sgp0 = r0

.InitFramekey:
	r0 = #0
	framekey = r0

.InitPcycle:
	r1 = #1
	r0 = syscfg
	r0 = insert (r1, #1, #6)
	syscfg = r0

.InitDMT:
	r1 = #1
	r0 = syscfg
	r0 = insert (r1, #1, #15)
	syscfg = r0

.InitQoS:
	r1 = #1
	r0 = syscfg
	r0 = insert (r1, #1, #13)
	syscfg = r0

.InitXE:
	r1 = #1
	r0 = ssr
	r0 = insert (r1, #1, #31)
	ssr = r0

	/* Read JTLB size from cfgtable */
	{
		r0 = #0x2c
		r2 = cfgbase
	}
	r1 = asl(r2, #5)
	r0 = memw_phys(r0, r1)
	{
		r0 = add(r0, #-1);
		memw(##_tlbmax) = r0.new
	}

	/* Read L2 tag size */
	{
		r0 = #0x40
		r2 = cfgbase
	}
	r0 = memw_phys(r0, r1)
	r1 = #0;
	p0 = cmp.eq(r0, #0x400)
	{
		if (p0) r1 = #5
		if (p0) jump 1f
	}
	p0 = cmp.eq(r0, #0x200)
	{
		if (p0) r1 = #4
		if (p0) jump 1f
	}
	p0 = cmp.eq(r0, #0x100)
	{
		if (p0) r1 = #3
		if (p0) jump 1f
	}
	p0 = cmp.eq(r0, #0x080)
	{
		if (p0) r1 = #2
		if (p0) jump 1f
	}
1:
	memw(##_l2cfg) = r1

	/* L2 config: disable prefetching */
	r0 = ccr
	r3 = #0
	r0 = insert (r3, #4, #16)
	ccr = r0

	r0 = syscfg
	r0 = insert (r3, #3, #16)

	.p2alignl 5, 0x7f00c000
	isync
	syncht

	syscfg = r0
	isync

	/* L2KILL to initialize cache */
	l2kill
	syncht

	/* Set desired L2 size */
	r2 = memw(##_l2cfg)
	r3 = #0x5
	r3 = min (r2, r3)
	r0 = insert (r3, #4, #16)
	syscfg = r0
	isync

	/* L1 cache config */
.InitCache:
	r1 = #0
	r1 = #1
	r2 = syscfg
	r2 = insert (r1, #1, #1)
	r2 = insert (r0, #1, #2)
	r1 = #1
	r2 = insert (r1, #1, #23)
	syscfg = r2
	isync

	/* TLB initialization */
.InitTLB:
	r0 = memw(##_tlbmax)
	{
		r3:2 = combine(#0,#0)
		memw(##_NumTLBEntries) = r0
	}

	loop0(.InitTLBLoop, r0)
.falign
.InitTLBLoop:
	tlbw(r3:2,r0)
	r0 = add (r0, #-1)
	{}:endloop0
	isync

	/* Global 1:1 TLB mapping */
.InitTLBGlobal:
	AddrOf _start, r2
	r2 = lsr (r2, #12)
	AddrOf 0xc3f00000, r1
	AddrOf 0xf7000000, r0
	r1 = or (r1, r2)
	r0 |= asl (r2,#1)
	r0 = setbit(r0,#4)
	r0 = and(r0,#-16)
	r2 = #0
	tlbw(r1:0,r3)

	/* Turn on MMU */
	r0 = syscfg
	r0 = setbit (r0, #0)
	syscfg = r0
	isync

.InitInt:
	r0 = #0
	imask = r0
	r1 = #-1
	cswi (r1)

	/* Enable interrupts */
	r0 = ssr
	r0 = setbit (r0, #18)
	ssr= r0

	r0 = syscfg
	r0 = setbit (r0, #4)
	syscfg = r0
	isync

	/* Set up semihosting heap (Angel call) */
	r0 = #22
	AddrOf setHeapAngelCallParams, r1
	trap0 (#0)

.PreMain:
	AddrOf hexagon_pre_main, r28
	jumpr r28
	.size hexagon_start_init, . - hexagon_start_init

.global	qdsp6_start_init
.set	qdsp6_start_init, hexagon_start_init

/*======================================================================
 * Pre-main: set up stack, zero BSS, call rust_main
 *======================================================================*/
	.global hexagon_pre_main
	.type hexagon_pre_main, @function
hexagon_pre_main:
	fp = #0

	/* Set up stack at end of program image + heap space */
	AddrOf _end, r4
	r4 = add(r4, #0x10000)     /* 64KB gap after .bss */
	r4 = and(r4, #-16)         /* align to 16 */

	AddrOf STACK_SIZE
	r5 = r0
	r6 = add(r4, r5)
	r6 = and(r6, #-16)
	sp = r6                     /* stack top */

	/* Zero BSS */
	AddrOf __bss_start, r0
	AddrOf __bss_end, r2
	r1 = #0
	r3 = sub(r2, r0)
	p0 = cmp.gtu(r3, #0)
	if (!p0) jump .Lbss_done
.Lbss_loop:
	memw(r0++#4) = r1
	r3 = add(r3, #-4)
	p0 = cmp.gt(r3, #0)
	if (p0) jump .Lbss_loop
.Lbss_done:
	.size hexagon_pre_main, . - hexagon_pre_main

	/* Fall through to hexagon_start_main */

/*======================================================================
 * Main entry: call rust_main, then stop
 *======================================================================*/
	.global hexagon_start_main
	.type hexagon_start_main, @function
hexagon_start_main:
	/* Call the Rust main function */
	call rust_main

	/* rust_main returns error count in R0 (0 = pass, >0 = fail) */
	/* Semihosting SYS_EXIT: report exit code to host */
	r1 = r0               /* exit code */
	r0 = #0x18            /* SYS_EXIT */
	trap0(#0)
	/* Fallback: stop all threads (reached if SYS_EXIT not supported) */
	r0 = #0x3f
	stop (r0)
	.size hexagon_start_main, . - hexagon_start_main

/*======================================================================
 * Thread stop (for secondary threads)
 *======================================================================*/
	.p2align 4
	.weak thread_stop
	.type thread_stop, @function
thread_stop:
	{
		r0 = htid
		r1 = #1
	}
	r1 = lsl (r1, r0)
	stop (r1)

/*======================================================================
 * Event handlers
 *======================================================================*/
	.p2align 4
	.type event_handle_reset, @function
event_handle_reset:
	r0 = htid
	/* Thread 0 goes to _start */
	p0 = cmp.eq(r0, #0)
	if (p0) jump _start

	/*
	 * Secondary threads: check __thread_entry[htid] for a function
	 * pointer.  If non-zero, set up stack and call it, then stop.
	 * If zero, just stop immediately.
	 */
	r1 = ##(__thread_entry)
	r2 = memw(r1 + r0<<#2)           /* r2 = __thread_entry[htid] */
	p0 = cmp.eq(r2, #0)
	if (p0) jump .Lreset_stop

	/* Set up this thread's exception stack pointer */
	r3 = ##(exc_stack_tops)
	r3 = memw(r3 + r0<<#2)           /* exc_stack_tops[htid] */
	sgp0 = r3

	/* Set up a minimal stack: use _end + 64KB + (htid * STACK_SIZE) */
	AddrOf _end, r4
	r4 = add(r4, #0x10000)
	r4 = and(r4, #-16)
	r5 = ##STACK_SIZE
	/* Thread 0 uses the first STACK_SIZE region; thread N at offset N */
	r6 = mpyi(r0, r5)
	r6 = add(r6, r5)                 /* skip thread 0's stack */
	r4 = add(r4, r6)
	r4 = and(r4, #-16)
	sp = r4
	fp = #0

	/* Clear the entry so it's one-shot */
	r3 = #0
	memw(r1 + r0<<#2) = r3

	/*
	 * Clear SSR.EX so the thread runs in normal (non-exception) mode.
	 * The start instruction enters the reset vector with EX=1; we must
	 * leave exception mode before calling user code so that interrupts
	 * can be accepted once IE is enabled.
	 */
	r3 = ssr
	r3 = clrbit(r3, #17)
	ssr = r3
	isync

	/* Call the entry function */
	callr r2

.Lreset_stop:
	/* Stop this thread */
	r0 = htid
	r1 = #1
	r1 = lsl(r1, r0)
	stop(r1)
	.size event_handle_reset, . - event_handle_reset

	.type event_handle_nmi, @function
event_handle_nmi:
	jump coredump
	.size event_handle_nmi, . - event_handle_nmi

	.type event_handle_error, @function
event_handle_error:
	crswap (sp, sgp0)
	sp = add (sp, #-32)
	memd (sp + #0) = r1:0
	memd (sp + #8) = r3:2
	r0 = ssr
	r1 = extractu(r0, #8, #0)  /* SSR.CAUSE */
	r2 = elr
	r3 = badva
	memw(##exception_cause) = r1
	memw(##exception_elr) = r2
	memw(##exception_badva) = r3
	/* Increment exception_count */
	r3 = memw(##exception_count)
	r3 = add(r3, #1)
	memw(##exception_count) = r3
	/* Advance ELR past faulting instruction (4 bytes) */
	r2 = add(r2, #4)
	elr = r2
	r3:2 = memd (sp + #8)
	r1:0 = memd (sp + #0)
	sp = add (sp, #32)
	crswap (sp, sgp0)
	rte
	.size event_handle_error, . - event_handle_error

	.type event_handle_rsvd, @function
event_handle_rsvd:
	jump coredump
	.size event_handle_rsvd, . - event_handle_rsvd

	.global coredump
	.type coredump, @function
coredump:
	r0 = ssr
	r0 = clrbit (r0, #16)
	r0 = clrbit (r0, #17)
	ssr = r0
	isync
	r0 = #-1
	stop (r0)
	.size coredump, . - coredump

/*======================================================================
 * TLB miss handlers
 *======================================================================*/
	.subsection 0
	.p2align 6
	.global event_handle_tlbmissx
	.type event_handle_tlbmissx, @function
event_handle_tlbmissx:
	crswap (sp, sgp0)
	sp = add (sp, #-64)
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
	}
	{
		memd (sp + #16) = r5:4
		memd (sp + #24) = r7:6
	}
	{
		memd (sp + #32) = r9:8
		r9 = p3:0
	}
	r8 = ssr
	r7 = elr
	p1 = tstbit (r8, #0)
	{
		r7 = lsr (r7, #12)
		if (!p1) jump 1f
		r0 = ##(__tlb_idx)
	}
	r7 = add (r7, #1)
1:
	{
		r1 = memw(##_tlb_fixed_entries)
		r3 = memw(##_NumTLBEntries)
	}
1:
	r6 = memw_locked (r0)
	{
		r6 = add (r6, #1)
		p0 = !cmp.gt (r3, r6)
	}
	r6 = mux (p0, r1, r6)
	memw_locked (r0, p0) = r6
	if (!p0) jump 1b

	{
		r7 = lsr (r7, #8)
		r3 = memw (##TLBMapTable)
	}
	r3 = addasl (r3, r7, #1)
	{
		r3 = memh (r3)
		r7 = asl (r7, #8)
	}
	r5 = extractu (r3, #12, #4)
	{
		r4 = extractu (r3, #4, #0)
		r0 = #0x0010
		r1 = #0
	}
	{
		r4 = asl (r4, #24)
		r1.h = #0xc000
		r0.h = #0xf000
	}
	{
		r1 = or (r1, r7)
		r0 |= asl(r5,#9)
	}
	r0 = or (r0, r4)
	tlblock
	r5 = tlbp(r1)
	p0 = tstbit (r5, #31)
	if (!p0) jump 1f
	tlbw(r1:0,r6)
	isync
1:
	tlbunlock
	p3:0 = r9
	{
		r9:8 = memd (sp + #32)
		r7:6 = memd (sp + #24)
	}
	{
		r5:4 = memd (sp + #16)
		r3:2 = memd (sp + #8)
	}
	{
		r1:0 = memd (sp + #0)
		sp = add (sp, #64)
	}
	crswap (sp, sgp0)
	rte
	.size event_handle_tlbmissx, . - event_handle_tlbmissx

	.p2align 6
	.global event_handle_tlbmissrw
	.type event_handle_tlbmissrw, @function
event_handle_tlbmissrw:
	crswap (sp, sgp0)
	sp = add (sp, #-64)
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
	}
	{
		memd (sp + #16) = r5:4
		memd (sp + #24) = r7:6
	}
	{
		memd (sp + #32) = r9:8
		r8 = ssr
	}
	r7 = badva
	r9 = p3:0
	{
		r0 = ##__tlb_idx
		r1 = memw(##_tlb_fixed_entries)
	}
	{
		r7 = lsr (r7, #20)
		r3 = memw(##_NumTLBEntries)
	}
1:
	r6 = memw_locked (r0)
	{
		r6 = add (r6, #1)
		p0 = !cmp.gt (r3, r6)
	}
	r6 = mux (p0, r1, r6)
	memw_locked (r0, p0) = r6
	if (!p0) jump 1b

	r3 = memw (##TLBMapTable)
	r3 = addasl (r3, r7, #1)
	{
		r3 = memh (r3)
		r7 = asl (r7, #8)
	}
	r4 = extractu (r3, #4, #0)
	{
		r5 = extractu (r3, #12, #4)
		r0 = #0x0010
		r1 = #0
	}
	{
		r4 = asl (r4, #24)
		r1.h = #0xc000
		r0.h = #0xf000
	}
	{
		r1 = or (r1, r7)
		r0 |= asl(r5,#9)
	}
	r0 = or (r0, r4)
	tlblock
	r5 = tlbp(r1)
	p0 = tstbit (r5, #31)
	if (!p0) jump 1f
	tlbw(r1:0,r6)
	isync
	jump 2f
1:
	r4 = memw(##_tlb_fixed_entries)
	p0 = cmp.gt(r4, r5)
	if (p0) jump .
2:
	tlbunlock
	p3:0 = r9
	{
		r9:8 = memd (sp + #32)
		r7:6 = memd (sp + #24)
	}
	{
		r5:4 = memd (sp + #16)
		r3:2 = memd (sp + #8)
	}
	{
		r1:0 = memd (sp + #0)
		sp = add (sp, #64)
	}
	crswap (sp, sgp0)
	rte
	.size event_handle_tlbmissrw, . - event_handle_tlbmissrw

/*======================================================================
 * Trap handlers
 *======================================================================*/
	.p2align 4
	.type event_handle_trap0, @function
event_handle_trap0:
	crswap (sp, sgp0)
	sp = add (sp, #-48)
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
	}
	memd (sp + #16) = r5:4
	r0 = ssr
	r1 = extractu(r0, #8, #0)  /* SSR.CAUSE */
	/* cause == 0: semihosting -- pass through */
	{
		p0 = cmp.eq(r1, #0)
		if (p0.new) jump:nt .Ltrap0_semihost
	}
	/* cause == 1: exit user mode request */
	{
		p0 = cmp.eq(r1, #1)
		if (p0.new) jump:nt .Ltrap0_exit_user
	}
	/* other causes: record exception info */
	r2 = elr
	memw(##exception_cause) = r1
	memw(##exception_elr) = r2
	jump .Ltrap0_done
.Ltrap0_exit_user:
	/* Clear UM bit (bit 16) in SSR to return to supervisor */
	r0 = clrbit(r0, #16)
	ssr = r0
	jump .Ltrap0_done
.Ltrap0_semihost:
	/* pass through -- semihosting trap0(#0) */
.Ltrap0_done:
	{
		r5:4 = memd (sp + #16)
		r3:2 = memd (sp + #8)
	}
	{
		r1:0 = memd (sp + #0)
		sp = add (sp, #48)
	}
	crswap (sp, sgp0)
	rte
	.size event_handle_trap0, . - event_handle_trap0

	.p2align 4
	.type event_handle_trap1, @function
event_handle_trap1:
	crswap (sp, sgp0)
	sp = add (sp, #-32)
	memd (sp + #0) = r1:0
	memd (sp + #8) = r3:2
	r0 = ssr
	r1 = extractu(r0, #8, #0)  /* SSR.CAUSE */
	r2 = elr
	memw(##exception_cause) = r1
	memw(##exception_elr) = r2
	r3:2 = memd (sp + #8)
	r1:0 = memd (sp + #0)
	sp = add (sp, #32)
	crswap (sp, sgp0)
	rte
	.size event_handle_trap1, . - event_handle_trap1

/*======================================================================
 * Interrupt handler
 *======================================================================*/
	.type event_handle_int, @function
event_handle_int:
	crswap (sp, sgp0)
	allocframe (#160)
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
		r0 = SA0
	}
	{
		memd (sp + #16) = r5:4
		memd (sp + #24) = r7:6
		r1 = LC0
	}
	{
		memd (sp + #32) = r9:8
		memd (sp + #40) = r11:10
		r2 = SA1
	}
	{
		memd (sp + #48) = r13:12
		memd (sp + #56) = r15:14
		r3 = LC1
	}
	{
		memd (sp + #64) = r17:16
		memd (sp + #72) = r19:18
		r6 = p3:0
	}
	{
		memd (sp + #80) = r21:20
		memd (sp + #88) = r23:22
		r5:4 = C7:6
	}
	{
		memd (sp + #96) = r25:24
		memd (sp + #104) = r27:26
		r7 = USR
	}
	{
		memd (sp + #112) = r1:0
		memd (sp + #136) = r7:6
		r8 = UGP
	}
	r0 = ssr
	{
		memd (sp + #120) = r3:2
		r2 = r0
		r7 = insert (r0, #8, #16)
	}
	{
		r9 = ELR
		memd (sp + #128) = r5:4
		r0 = and (r0, #0x1f)
		r1 = ##(__IntHandlers)
	}
	{
		r1 = addasl (r1, r0, #2)
	}
	{
		memd (sp + #144) = r9:8
		r1 = memw (r1)
		r3 = #0
		lr = r28
	}
	{
		memd (sp + #152) = lr:fp
		r2 = insert (r3, #3, #16)
		p0 = cmp.eq (r1, #0)
	}
	if (p0) jump 1f
	ssr = r2
	crswap (sp, sgp0)
	callr r1
	crswap (sp, sgp0)
	r0 = ssr
	/* Extract interrupt number from SSR.CAUSE[4:0] for ciad mask */
	r7 = and (r0, #0x1f)
	r1 = #6
	r0 = insert(r1, #3, #16)
	r26 = #1
	r26 = lsl (r26, r7)
	ssr = r0
	ciad (r26)
1:
	{
		lr:fp = memd (sp + #152)
		r9:8 = memd (sp + #144)
	}
	elr = r9
	{
		r7:6 = memd (sp + #136)
		r5:4 = memd (sp + #128)
		UGP = r8
	}
	{
		r3:2 = memd (sp + #120)
		r1:0 = memd (sp + #112)
		usr = r7
		r28 = lr
	}
	{
		r27:26 = memd (sp + #104)
		r25:24 = memd (sp + #96)
		m0 = r4
	}
	{
		r23:22 = memd (sp + #88)
		r21:20 = memd (sp + #80)
		m1 = r5
	}
	{
		r19:18 = memd (sp + #72)
		r17:16 = memd (sp + #64)
		p3:0 = r6
	}
	{
		r15:14 = memd (sp + #56)
		r13:12 = memd (sp + #48)
		lc1 = r3
	}
	{
		r11:10 = memd (sp + #40)
		r9:8   = memd (sp + #32)
		sa1 = r2
	}
	{
		r7:6   = memd (sp + #24)
		r5:4   = memd (sp + #16)
		lc0 = r1
	}
	{
		r3:2   = memd (sp + #8)
		r1:0   = memd (sp + #0)
		sa0 = r0
	}
	deallocframe
	crswap (sp, sgp0)
	rte
	.size event_handle_int, . - event_handle_int

	.p2align 4
	.type .NoHandler, @function
.NoHandler:
	jumpr lr
	.size .NoHandler, . - .NoHandler

/*======================================================================
 * Event vector table (must be 4K-aligned)
 *======================================================================*/
	.text
	.p2align 12, 0
	.type .EventVectors, @function
.EventVectors:
	jump event_handle_reset       /* 0  Reset */
	jump event_handle_nmi         /* 1  NMI */
	jump event_handle_error       /* 2  Error */
	jump event_handle_rsvd        /* 3  Reserved */
	jump event_handle_tlbmissx    /* 4  TLB miss X */
	jump event_handle_rsvd        /* 5  Reserved */
	jump event_handle_tlbmissrw   /* 6  TLB miss RW */
	jump event_handle_rsvd        /* 7  Reserved */
	jump event_handle_trap0       /* 8  Trap0 */
	jump event_handle_trap1       /* 9  Trap1 */
	jump event_handle_rsvd        /* 10 Reserved */
	jump event_handle_rsvd        /* 11 Reserved */
	jump event_handle_rsvd        /* 12 Reserved */
	jump event_handle_rsvd        /* 13 Reserved */
	jump event_handle_rsvd        /* 14 Reserved */
	jump event_handle_rsvd        /* 15 Reserved */
	jump event_handle_int         /* 16 INT0 */
	jump event_handle_int         /* 17 INT1 */
	jump event_handle_int         /* 18 INT2 */
	jump event_handle_int         /* 19 INT3 */
	jump event_handle_int         /* 20 INT4 */
	jump event_handle_int         /* 21 INT5 */
	jump event_handle_int         /* 22 INT6 */
	jump event_handle_int         /* 23 INT7 */
	jump event_handle_int         /* 24 INT8 */
	jump event_handle_int         /* 25 INT9 */
	jump event_handle_int         /* 26 INT10 */
	jump event_handle_int         /* 27 INT11 */
	jump event_handle_int         /* 28 INT12 */
	jump event_handle_int         /* 29 INT13 */
	jump event_handle_int         /* 30 INT14 */
	jump event_handle_int         /* 31 INT15 */
	jump event_handle_int         /* 32 INT16 */
	jump event_handle_int         /* 33 INT17 */
	jump event_handle_int         /* 34 INT18 */
	jump event_handle_int         /* 35 INT19 */
	jump event_handle_int         /* 36 INT20 */
	jump event_handle_int         /* 37 INT21 */
	jump event_handle_int         /* 38 INT22 */
	jump event_handle_int         /* 39 INT23 */
	jump event_handle_int         /* 40 INT24 */
	jump event_handle_int         /* 41 INT25 */
	jump event_handle_int         /* 42 INT26 */
	jump event_handle_int         /* 43 INT27 */
	jump event_handle_int         /* 44 INT28 */
	jump event_handle_int         /* 45 INT29 */
	jump event_handle_int         /* 46 INT30 */
	jump event_handle_int         /* 47 INT31 */
	.size .EventVectors, . - .EventVectors

/*======================================================================
 * Data section
 *======================================================================*/
	.section .start, "awx"
	.subsection 1

	.p2align 4, 0

	.global __IntHandlers
	.set __IntHandlers, .IntHandlers
.IntHandlers:
	.rept 32
	.word .NoHandler
	.endr

	.p2align 5, 0
RegDump:
	.space 4 * (32 + 10 + 29)

	/* Thread entry function pointers (set by Rust before start()) */
	.global __thread_entry
	.set __thread_entry, .ThreadEntry
	.p2align 2, 0
.ThreadEntry:
	.rept 16
	.word 0
	.endr

	/* Exception stacks for threads 0-15 */
	.p2align 3, 0
	.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
exc_stack_lim\n:	.space 384
exc_stack_top\n:	.word 0
	.p2align 3, 0
	.endr

exc_stack_tops:
	.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.word exc_stack_top\n
	.endr

	.p2align 6, 0

	.global __tlb_lock
	.set __tlb_lock, tlb_lock
tlb_lock:
	.word 0
	.global __tlb_idx
	.set __tlb_idx, tlb_idx
tlb_idx:
	.word TLB_FIXED_ENTRIES - 1

	.global _tlb_fixed_entries
_tlb_fixed_entries:
	.word TLB_FIXED_ENTRIES

_l2cfg:
	.word 0
_tlbmax:
	.word 0

	/* Exception recording globals */
	.global exception_cause
	.p2align 2
exception_cause:
	.word 0
	.global exception_elr
exception_elr:
	.word 0
	.global exception_badva
exception_badva:
	.word 0
	.global exception_count
exception_count:
	.word 0

	/* Semihosting heap params */
.HeapParams:
heapBase:
	.word _end
heapLimit:
	.word 0
stackBase:
	.word 0
stackLimit:
	.word 0

setHeapAngelCallParams:
	.word .HeapParams
