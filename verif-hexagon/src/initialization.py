#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import random
from src.regs import gprs, vecs, vec_preds4, preds

# interesting values from AFL:
interesting_vals = [
  0xffffff80,
  0xffffffff,
  0x00000000,
  0x00000001,
  0x00000010,
  0x00000020,
  0x00000040,
  0x0000007f,
  0xffff8000,
  0xffffff7f,
  0x00000080,
  0x000000ff,
  0x00000100,
  0x00000200,
  0x00000400,
  0x00001000,
  0x00007fff,
  0x80000000,
  0xfa0000fa,
  0xffff7fff,
  0x00008000,
  0x0000ffff,
  0x00010000,
  0x05ffff05,
  0x7fffffff,
  ]

def rand_nearzero(): return random.randrange(0, 256)
def randval(): return random.randrange(2**32-1)
def interesting(): return random.choice(interesting_vals)
inits = [rand_nearzero, randval, interesting]

def init_state_val():
    method = random.randrange(0,len(inits))
    return inits[method]()

def get_gpr_insts(): return [f'{r} = #0x{init_state_val():08x}' for r in gprs]

HVX_MEM_ADDR_REGS = ('r1', 'r2', 'r3', )
def hvx_init(reg):
    s4 = 1 << 3
    imm = random.randrange(-s4, s4 - 1)
    hvx_mem_addr_reg = random.choice(HVX_MEM_ADDR_REGS)
    greg = int(reg) % 29
    options = (
        f'v{reg} = vsplat(r{greg})',
        f'v{reg} = vmemu({hvx_mem_addr_reg} + #{imm})',
        )
    return random.choice(options)
def get_hvx_insts(): return [hvx_init(reg[1:]) for reg in vecs]

def randvbool(): return random.choice(['vcmp.eq(v0.b,v0.b)','vcmp.eq(v0.b,v1.b)'])
def get_hvx_pred_insts(): return [f'{reg} = {randvbool()}' for reg in vec_preds4]

def randbool(): return random.choice(['cmp.eq(r0,r0)','cmp.eq(r0,r1)'])
def get_pred_insts(): return [f'{reg} = {randbool()}' for reg in preds]

test_gpr_insts = get_gpr_insts()
test_gpr_init  = '\n    '.join(test_gpr_insts)

_MEM_WORDS = 16
_MEM_REPEAT = 128

_MEM_BYTES = _MEM_WORDS * _MEM_REPEAT * 4
_HVX_ELEM_SIZE_BYTES = 128
_MEM_PADDING_REPEAT = (_HVX_ELEM_SIZE_BYTES * (1 << 3)) // (_MEM_WORDS * 4)
def hvx_rand_offset():
    return random.randrange(0, _MEM_BYTES - _HVX_ELEM_SIZE_BYTES)

def get_hvx_init():
    hvx_setup = '\n    '.join(f"{mem_reg} = #memory_access\n" +
        f"    {mem_reg} = add({mem_reg}, #{hvx_rand_offset()})"
        for mem_reg in HVX_MEM_ADDR_REGS) + "\n    "
    hvx_regs = '\n    '.join(get_hvx_insts() + get_hvx_pred_insts())
    test_hvx_init  = hvx_setup + hvx_regs
    return test_hvx_init

test_pred_init = '\n    '.join(get_pred_insts())

mem_rand_words = '\n    '.join('.word 0x{:08x}'.format(randval()) for i in range(_MEM_WORDS))

JUMP_TARGET_CNT = 6
