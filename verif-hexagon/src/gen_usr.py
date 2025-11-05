#!/usr/bin/env python
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import random
import re


from src.regs import m, ctrls, dbl_ctrls, preds, gprs, gpr8, dpl_gprs
from src.regs import dbl_gprs, vec_preds4, vec_preds, vec_dbl_preds, vecs
from src.regs import quad_vecs, dbl_vecs, dpl_dbl_gprs, dbl_guest, guest
from src.initialization import JUMP_TARGET_CNT

def choose(vals):
    return lambda x: random.choice(vals)

immed_pat_text = r'#(?P<signed>[UuSsr])(?P<bits>\d+)(?P<shift>:\d+)?'
immed_pat = re.compile(immed_pat_text)
def immed(op):
    fields = immed_pat.match(op).groupdict()
    is_signed = fields['signed'] in ('S', 's')
    min_val = -1 if is_signed else 0
    bits = int(fields['bits'])
    has_shift = fields['shift'] != None
    shift = int(fields['shift'][1:]) if has_shift else 0
    max_bit = (bits+shift) - (1 if is_signed else 0)
    max_val = (1 << max_bit) - 1

    value = random.randint(min_val, max_val)
    value = value & (2**32-1)
    return '#0x{:02x}'.format(value)

def choose_immed():
    return lambda op: immed(op)

def choose_lh(vals):
    return lambda x: random.choice(vals) + random.choice(['.l', '.h'])

def fill_jump(orig):
    return orig.split(' ')[0] + ' .Ljump_target' + str(random.choice(range(JUMP_TARGET_CNT)))

idents = r'[edstuvwxy]'
idents_p = idents + r'{2}'
idents_q = idents + r'{4}'
patterns_ = {
    r'jump(:t|:nt)? #r\d+(:\d+)?': fill_jump,
    r'P' + idents + '4': choose(preds),
    r'R' + idents_p + '8': choose(dpl_dbl_gprs),
    r'R' + idents + '32': choose(gprs),
    r'R' + idents + '\.[LH]32': choose_lh(gprs),
    r'R' + idents_p + '32': choose(dbl_gprs),
    r'R' + idents + '16': choose(dpl_gprs),
    r'V' + idents + '32': choose(vecs),
    r'V' + idents_p + '32': choose(dbl_vecs),
    r'V' + idents_q + '32': choose(quad_vecs),
    r'Q' + idents + '4': choose(vec_preds4),
    r'Q' + idents + '8': choose(vec_preds),
    r'[NR]' + idents + '8': choose(gpr8),
    r'C' + idents + '32': choose(ctrls),
    r'C' + idents_p + '32': choose(dbl_ctrls),
    r'M' + idents + '2': choose(m),
    r'G' + idents + '32': choose(guest),
    r'G' + idents_p + '32': choose(dbl_guest),
    immed_pat_text: choose_immed(),
}
patterns = { re.compile(k): v for k,v in patterns_.items() }

def populate_inst(inst):
    syn = inst['syntax']
    pre = None
    for pattern, subst in patterns.items():
        while True:
            m = pattern.search(syn)
            if not m:
                break
            reg = subst(m.group(0))
            if 'A_ATOMIC' in inst['attrs'] and m.group(0).startswith('Rs'):
                pre = f'{reg} = #memory_access'
            syn = syn[:m.start()] + reg + syn[m.end():]
    return pre, syn


if __name__ == '__main__':
    pass
