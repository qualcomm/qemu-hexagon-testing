# QEMU machine qcs6490-cdsp (Qualcomm QCS6490 compute DSP, v68).
# Placement follows the commercial cdsp.mbn (linux-firmware qcom/qcm6490):
# entry 0x88f00000 (the kernel image), guest text at 0x89100000.
# This is also the address toolchain_for_hexagon builds this machine's kernel at.
# H2K_GUEST_START is that address rounded up to 16MB: the booter maps guest
# memory with 16MB pages.
QEMU_MACHINE=qcs6490-cdsp
QEMU_BOOT=bios-none
ARCHV=68
H2K_LOAD_ADDR=0x88f00000
H2K_GUEST_START=0x8a000000
# loadlinux is linked to run here, where the machine's Linux-on-CDSP flow
# places its kernel (QCS6490_KERNEL_ADDR in QEMU).
LINUX_LINK_ADDR=0xa1000000
