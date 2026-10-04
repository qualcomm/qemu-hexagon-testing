# QEMU machine sa8775p-cdsp (SA8775P compute DSP, v73).
# Placement follows linux-firmware qcom/sa8775p/cdsp0.mbn:
# entry 0x9b800000, guest text at 0x9ba00000.
# H2K_GUEST_START is that address rounded up to 16MB: the booter maps guest
# memory with 16MB pages.
QEMU_MACHINE=sa8775p-cdsp
QEMU_BOOT=loader
ARCHV=73
H2K_LOAD_ADDR=0x9b800000
H2K_GUEST_START=0x9c000000
# loadlinux (the Linux kernel loader) is linked to run here.
LINUX_LINK_ADDR=0xa0000000
