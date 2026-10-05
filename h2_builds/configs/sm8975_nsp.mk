# QEMU machine sm8975_nsp (SM8975 neural signal processor; QEMU models a v81 core).
# Placement follows linux-firmware qcom/hawi/cdsp.mbn (v85, which h2 builds as
# ARCHV=81): entry 0x9b900000, guest text at 0x9bbc0000.
# H2K_GUEST_START is that address rounded up to 16MB: the booter maps guest
# memory with 16MB pages.
QEMU_MACHINE=sm8975_nsp
QEMU_BOOT=loader
ARCHV=81
H2K_LOAD_ADDR=0x9b900000
H2K_GUEST_START=0x9c000000
# loadlinux (the Linux kernel loader) is linked to run here.
LINUX_LINK_ADDR=0xa0000000
