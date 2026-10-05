# QEMU machine sc8480xp-nsp0 (SC8480XP neural signal processor, v81).
# Placement follows linux-firmware qcom/glymur/cdsp.mbn (v81):
# entry 0x8d900000, guest text at 0x8e100000.
# H2K_GUEST_START is that address rounded up to 16MB: the booter maps guest
# memory with 16MB pages.
QEMU_MACHINE=sc8480xp-nsp0
QEMU_BOOT=loader
ARCHV=81
H2K_LOAD_ADDR=0x8d900000
H2K_GUEST_START=0x8f000000
# loadlinux (the Linux kernel loader) is linked to run here.
LINUX_LINK_ADDR=0xa0000000
