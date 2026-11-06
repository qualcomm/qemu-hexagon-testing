/* SPDX-License-Identifier: BSD-3-Clause-Clear */
    .section .start, "ax", @progbits
    .global _start
_start:
    r0 = #0
    ssr = r0
    framekey = r0
    r29 = ##stack_top
    r0 = ##vectors
    evb = r0
    call main
    call finish
    stop(r0)

    .text
    .p2align 8
vectors:
    .rept 8
    jump exception_entry
    .endr
    jump semihost_return
    .rept 247
    jump exception_entry
    .endr

semihost_return:
    rte

exception_entry:
    allocframe(#128)
    memd(r29+#0) = r1:0
    memd(r29+#8) = r3:2
    memd(r29+#16) = r5:4
    memd(r29+#24) = r7:6
    memd(r29+#32) = r9:8
    memd(r29+#40) = r11:10
    memd(r29+#48) = r13:12
    memd(r29+#56) = r15:14
    memd(r29+#64) = r17:16
    memd(r29+#72) = r19:18
    memd(r29+#80) = r21:20
    memd(r29+#88) = r23:22
    memd(r29+#96) = r25:24
    memd(r29+#104) = r27:26
    memw(r29+#112) = r28
    r0 = p3:0
    memw(r29+#116) = r0
    r0 = usr
    memw(r29+#120) = r0
    call record_fault
    r0 = memw(r29+#120)
    usr = r0
    r0 = memw(r29+#116)
    p3:0 = r0
    r28 = memw(r29+#112)
    r27:26 = memd(r29+#104)
    r25:24 = memd(r29+#96)
    r23:22 = memd(r29+#88)
    r21:20 = memd(r29+#80)
    r19:18 = memd(r29+#72)
    r17:16 = memd(r29+#64)
    r15:14 = memd(r29+#56)
    r13:12 = memd(r29+#48)
    r11:10 = memd(r29+#40)
    r9:8 = memd(r29+#32)
    r7:6 = memd(r29+#24)
    r5:4 = memd(r29+#16)
    r3:2 = memd(r29+#8)
    r1:0 = memd(r29+#0)
    deallocframe
    rte
