# QEMU machine V81QA_1 (v81).  The synthetic, hexagon-sim style machines share one
# memory map: RAM from address 0, so h2 keeps its default placement.
# (h2 maps v66 as ARCHV=65.)
QEMU_MACHINE=V81QA_1
QEMU_BOOT=kernel
ARCHV=81
H2K_LOAD_ADDR=0x0
H2K_GUEST_START=0x02000000
