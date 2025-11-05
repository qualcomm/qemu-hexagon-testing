#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import random
import sys
import re
from string import Template
from tempfile import NamedTemporaryFile
from src import mut
import shlex
import subprocess
import os

from src.regs import gprs, dbl_gprs, preds, vecs

# Defined in test_case.tmpl
MEM_SIZE = 16 * 2 * 4

access_size_syntax = {
    1: 'memb',
    2: 'memh',
    4: 'memw',
    8: 'memd',
    1024: 'vmem',
}
subword_sizes = (1,2)
value_regs = {
    1: gprs,
    2: gprs,
    4: gprs,
    8: dbl_gprs,
    1024: vecs,
}

def regs_in_double_reg(dreg):
    regs = []
    if ':' in dreg:
        for reg in map(lambda n: 'r' + n, dreg.lstrip('r').split(':')):
            regs.append(reg)
    else:
        regs.append(dreg)
    return regs

def get_written_regs(packet):
    regs = []
    # Some elements in the `packet` list may contain \n. Split them.
    lines = "\n".join(packet).split("\n")
    for inst in lines:
        if '=' not in inst:
            continue
        pre, post = inst.split('=', 1)
        pre = re.sub(r"^if \(.*\) +", "", pre.strip())
        for reg in regs_in_double_reg(pre):
            if reg in gprs:
                regs.append(reg)
        post_match = re.match(".*\((r[0-9]+)\+\+", post)
        if post_match is not None:
            reg = post_match.group(1)
            if reg in gprs:
                regs.append(reg)
    return regs

def get_written_preds(packet):
    preds = []
    for inst in packet:
        if '=' not in inst:
            continue
        pre, _ = inst.split('=', 1)
        pre = pre.strip()
        if pre in preds:
            preds.append(pre)
    return preds

def filter_regs(from_regs, exclude_regs):
    filtered = []
    exclude_regs = [reg for dreg in exclude_regs for reg in regs_in_double_reg(dreg)]
    for dreg in from_regs:
        if any([reg for reg in regs_in_double_reg(dreg) if reg in exclude_regs]):
            continue
        filtered.append(dreg)
    return filtered

class PacketGenError(Exception):
    pass

def get_mem_access(inits, packet):
    access_size = random.choice(list(access_size_syntax.keys()))
    size = access_size_syntax[access_size]
    mem_addr_regs = ('r1', 'r2', 'r3')
    init_written_regs = get_written_regs(inits)

    # Unsigned mem access:
    if access_size in subword_sizes and random.random() > 0.5:
        size.replace('mem', 'memu')

    addr_modes = ('reg_offset', 'reg_sum', 'reg_incr',)
    mode = random.choice(addr_modes)

    written_regs = get_written_regs(packet)
    if mode == 'reg_incr':
        # For 'reg_incr', the memory addressing register will also be
        # written to in the post-increment.
        new_mem_addr_regs = filter_regs(mem_addr_regs, written_regs)
        if len(new_mem_addr_regs) > 0:
            mem_addr_regs = new_mem_addr_regs
        else:
            addr_modes = [mode for mode in addr_modes if mode != 'reg_incr']
            mode = random.choice(addr_modes)

    # Not an 'elif' because the above 'if' might change the mode!
    if mode == 'reg_sum':
        offset_reg_choices = filter_regs(['r4', 'r5', 'r6'], init_written_regs)
        if len(offset_reg_choices) == 0:
            addr_modes = [mode for mode in addr_modes if mode != 'reg_sum']
            mode = random.choice(addr_modes)

    # We don't want to override the mem_addr_regs or offset_reg from
    # another memory accessing instruction in this packet.
    mem_addr_regs = filter_regs(mem_addr_regs, init_written_regs)
    if len(mem_addr_regs) == 0:
        raise PacketGenError("no more registers available for memory addressing")
    base_reg = random.choice(mem_addr_regs)
    reg_init = f'{base_reg} = #memory_access'

    if mode == 'reg_offset':
        offset_bytes = random.choice(range(4)) * access_size
        if offset_bytes > MEM_SIZE:
            offset_bytes = 0
        reg_loc = f'{base_reg}+#{offset_bytes}'
    elif mode == 'reg_incr':
        incr_bytes = random.choice(range(4))
        incr = random.choice((f'++#{incr_bytes}', '++m0', '++m1'))
        reg_loc = f'{base_reg}{incr}'
    elif mode == 'reg_sum':
        offset_reg = random.choice(offset_reg_choices)
        offset_bytes = random.choice(range(4)) * access_size
        shift_count = random.choice(range(2))
        if offset_bytes << shift_count > MEM_SIZE:
            offset_bytes = 0
        reg_init += f'\n{offset_reg} = #{offset_bytes}'
        reg_loc = f'{base_reg}+{offset_reg}<<#{shift_count}'
    else:
        raise Exception('invalid mode ' + mode)

    new_vals = [reg + '.new' for reg in written_regs]
    is_store = random.choice((True, False))

    if is_store and new_vals and access_size in (1,2,4):
        reg_value = random.choice(new_vals + gprs[:6])
    else:
        choices = filter_regs(value_regs[access_size], written_regs)
        if len(choices) == 0:
            raise PacketGenError("no more registers available for loading")
        reg_value = random.choice(choices)

    hint = random.choice((':nt', '')) if 'vmem' in size else ''
    use  = random.choice(('.tmp', '.cur', '')) if 'vmem' in size else ''
    align  = random.choice(('u', '')) if 'vmem' in size else ''

    load = f'{reg_value}{use} = {size}{align}({reg_loc}){hint}'
    store = f'{size}{align}({reg_loc}){hint} = {reg_value}'
    mem_access = store if is_store else load

    not_ = random.choice(('!', ''))
    written_preds = get_written_preds(packet)
    new_preds = [reg + '.new' for reg in written_preds]
    pred_reg = random.choice(new_preds if new_preds else preds)
    predicated = f'if ({not_}{pred_reg}) '
    pred_prefix = random.choice((predicated, ''))

    mem_access = pred_prefix + mem_access

    # FIXME: pred-dot-new, addr modes

    return (reg_init, mem_access)
