#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import random

gpr_brev_insts = [f'r{reg} = brev(r{reg})' for reg in range(28)]
gpr_brev = '\n    '.join(gpr_brev_insts)

gpr_flip_insts = [f'r{reg} = togglebit(r{reg}, #{29-reg})' for reg in range(28)]
gpr_flip = '\n    '.join(gpr_flip_insts)

_dest_regs = list(range(29))
random.shuffle(_dest_regs)
gpr_xor_insts = [f'r{dst} = xor(r{src}, r{28-src})' for dst, src in zip(_dest_regs, range(29))]
gpr_xor = '\n    '.join(gpr_xor_insts)

gpr_rot_insts = [f'r{reg} = rol(r{reg}, #1)' for reg in range(28)]
gpr_rot = '\n    '.join(gpr_rot_insts)
# we will rotate by whatever bits happen to be in r0:
vec_rot_insts = [f'v{reg} = vrol(v{reg}, r0)' for reg in range(32)]
vec_rot = '\n    '.join(vec_rot_insts)
