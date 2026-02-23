/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Assembly support for the direct-to-guest interrupt test.
 *
 * Guest mode restrictions:
 *   - Cannot read/write supervisor registers (ssr, ccr, evb, etc.)
 *   - Cannot read/write guest registers via CR transfer (g0-g3, gelr, gsr)
 *   - CAN use VM instructions: trap1(#0)=VMVERSION, trap1(#1)=VMRTE,
 *     trap1(#2)=VMSETIE, trap1(#3)=VMGETIE, trap1(#4)=VMSPSWAP
 *   - CAN execute general-purpose instructions (ALU, load/store, etc.)
 *
 * Strategy for exiting guest mode:
 *   Both the guest code and the DTG handler exit via trap0(#0) with
 *   CCR.GTE=0.  This sends the trap to the monitor's event vector
 *   table (EVB).  We temporarily install a custom monitor EVB whose
 *   trap0 handler clears SSR.GM and redirects execution to a saved
 *   monitor return address.
 */

	.text

/*
 * Custom Monitor Event Vector Table
 *
 * Installed temporarily while the test runs.  Only event 8 (trap0)
 * has a real handler; all others spin (should never be reached).
 *
 * EVB bits 8:0 are hardwired to 0, requiring 512-byte alignment.
 */
	.p2align 9
	.type custom_monitor_vectors, @function
custom_monitor_vectors:
	jump .                           /* Event 0:  Reset */
	jump .                           /* Event 1:  NMI */
	jump .                           /* Event 2:  Error */
	jump .                           /* Event 3:  Reserved */
	jump .                           /* Event 4:  TLB miss X */
	jump .                           /* Event 5:  Reserved */
	jump .                           /* Event 6:  TLB miss RW */
	jump .                           /* Event 7:  Reserved */
	jump guest_exit_trap0_handler    /* Event 8:  Trap0 */
	jump .                           /* Event 9:  Trap1 */
	jump .                           /* Event 10: Reserved */
	jump .                           /* Event 11: Float */
	jump .                           /* Event 12: Debug */
	jump .                           /* Event 13: Reserved */
	jump .                           /* Event 14: Reserved */
	jump .                           /* Event 15: Reserved */
	/* Events 16-47: Interrupts 0-31 */
	.rept 32
	jump .
	.endr
	.size custom_monitor_vectors, . - custom_monitor_vectors

/*
 * Trap0 handler for exiting guest mode.
 *
 * Called when guest code or the DTG handler executes trap0(#0) with
 * CCR.GTE=0.  The trap goes to the monitor EVB instead of GEVB.
 *
 * On entry:
 *   SSR.EX = 1 (monitor exception mode)
 *   SSR.GM = 1 (still set from guest mode)
 *   ELR = PC after the trap0 instruction (not useful to us)
 *
 * We clear SSR.GM and SSR.UM, redirect ELR to the saved monitor
 * return address, and rte to resume in monitor mode.
 *
 * Clobbers r0 (caller-save, not needed by return path).
 */
	.p2align 4
	.type guest_exit_trap0_handler, @function
guest_exit_trap0_handler:
	r0 = ssr
	r0 = clrbit(r0, #19)   /* Clear GM -> exit guest mode */
	r0 = clrbit(r0, #16)   /* Clear UM -> supervisor mode */
	ssr = r0
	r0 = memw(##guest_exit_return_addr)
	elr = r0
	rte
	.size guest_exit_trap0_handler, . - guest_exit_trap0_handler

/*
 * Guest Event Vector Table
 *
 * GEVB bits 8:0 are hardwired to 0, requiring 512-byte alignment.
 * Event 16 at offset 0x40 is the direct-to-guest interrupt vector.
 */
	.p2align 9
	.global guest_event_vectors
	.type guest_event_vectors, @function
guest_event_vectors:
	jump .                         /* Event 0:  Reset */
	jump .                         /* Event 1:  NMI */
	jump .                         /* Event 2:  Error */
	jump .                         /* Event 3:  Reserved */
	jump .                         /* Event 4:  TLB miss X */
	jump .                         /* Event 5:  Reserved */
	jump .                         /* Event 6:  TLB miss RW */
	jump .                         /* Event 7:  Reserved */
	jump .                         /* Event 8:  Trap0 */
	jump .                         /* Event 9:  Trap1 */
	jump .                         /* Event 10: Reserved */
	jump .                         /* Event 11: Float */
	jump .                         /* Event 12: Debug */
	jump .                         /* Event 13: Reserved */
	jump .                         /* Event 14: Reserved */
	jump .                         /* Event 15: Reserved */
	jump guest_event_int_handler   /* Event 16: DTG interrupt */
	.size guest_event_vectors, . - guest_event_vectors

/*
 * Guest event handler for direct-to-guest interrupts (event 16).
 *
 * On entry, hardware has done:
 *   GSR.CAUSE = VIDx, GSR.UM = ~SSR.GM, GSR.GIE = CCR.GIE
 *   SSR.GM = 1, CCR.GIE = 0, GELR = interrupted guest PC
 *
 * We record that the handler ran by setting dtg_isr_entered = 1,
 * then exit guest mode via trap0(#0).  With CCR.GTE=0, the trap
 * goes to our custom monitor EVB trap0 handler which clears GM
 * and returns to the saved monitor return address.
 */
	.p2align 4
	.type guest_event_int_handler, @function
guest_event_int_handler:
	/* Save r0-r1 on the stack */
	sp = add(sp, #-8)
	memd(sp+#0) = r1:0

	/* Set dtg_isr_entered = 1 */
	r0 = #1
	r1 = ##dtg_isr_entered
	memw(r1) = r0

	/* Restore r0-r1 */
	r1:0 = memd(sp+#0)
	sp = add(sp, #8)

	/*
	 * Exit guest mode via trap0 instead of VMRTE.
	 * The custom trap0 handler redirects to guest_exit_return_addr.
	 */
	trap0(#0)
	.size guest_event_int_handler, . - guest_event_int_handler

/*
 * enter_guest_mode
 *
 * Enters Guest mode, optionally posts a software interrupt for DTG
 * testing, executes a few nops in guest mode, then exits back to
 * Monitor mode via trap0 through our custom EVB handler.
 *
 * Arguments:
 *   r0 = SWI mask to trigger (0 to skip)
 */
	.p2align 4
	.global enter_guest_mode
	.type enter_guest_mode, @function
enter_guest_mode:
	allocframe(#32)
	memd(sp+#0) = r17:16
	memd(sp+#8) = r19:18
	r16 = lr
	r17 = r0                  /* save SWI mask */

	/* Save old EVB and install our custom monitor vectors */
	r18 = evb
	r0 = ##custom_monitor_vectors
	evb = r0
	isync

	/*
	 * Store our monitor return address in a global so the
	 * trap0 handler can redirect execution there regardless
	 * of where trap0 fires (guest code or DTG handler).
	 */
	r0 = ##.Lmonitor_return
	memw(##guest_exit_return_addr) = r0

	/* Post SWI if requested (from Monitor mode, before rte) */
	{
		p0 = cmp.eq(r17, #0)
		if (p0.new) jump:t 1f
	}
	swi(r17)
	isync
1:

	/* Set up ELR to guest code entry point */
	r0 = ##.Lguest_code
	elr = r0

	/*
	 * Build SSR for guest mode entry:
	 *   EX = 1  (bit 17, required for rte)
	 *   UM = 1  (bit 16, guest user mode)
	 *   IE = 1  (bit 18, enable interrupts)
	 *   GM = 1  (bit 19, guest mode)
	 */
	r0 = ssr
	r0 = setbit(r0, #17)   /* EX = 1 */
	r0 = setbit(r0, #16)   /* UM = 1 */
	r0 = setbit(r0, #18)   /* IE = 1 */
	r0 = setbit(r0, #19)   /* GM = 1 */
	ssr = r0
	isync

	/* RTE: clears EX, jumps to ELR in Guest mode */
	rte

	/* --- Guest mode code --- */
.Lguest_code:
	nop
	nop
	nop
	nop

	/*
	 * Exit guest mode via trap0.
	 * With CCR.GTE=0, this goes to our custom EVB event 8 handler.
	 * The handler clears GM, sets ELR to .Lmonitor_return, and rte.
	 *
	 * If a DTG fired, the DTG handler already exited via trap0
	 * and we never reach this instruction.
	 */
	trap0(#0)

	/* --- Back in Monitor mode (GM=0, UM=0) --- */
.Lmonitor_return:
	/* Restore old EVB */
	evb = r18
	isync

	lr = r16
	r17:16 = memd(sp+#0)
	r19:18 = memd(sp+#8)
	deallocframe
	jumpr lr

	.size enter_guest_mode, . - enter_guest_mode

	.data
	.p2align 2
guest_exit_return_addr:
	.word 0
