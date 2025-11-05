#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import random
from string import Template
import os.path
from collections import namedtuple
import subprocess
import shlex
from tempfile import mkdtemp
import filecmp
import shutil
import time
import sys
import stat

from src import log
from src.gen_usr import populate_inst
from src.gen_all import get_mem_access, PacketGenError
from src.regs import m, ctrls, preds, vec_preds4, vecs, gprs
from src.initialization import test_gpr_init, get_hvx_init, test_pred_init, \
    mem_rand_words, _MEM_REPEAT, _MEM_PADDING_REPEAT, JUMP_TARGET_CNT
from src import mut
from src import log
from src.adjust_output import adjust_binary_output

QEMU = '/prj/qct/llvm/target/vp_qemu_llvm/qemu_builds/build-latest/Tools/QEMUHexagon/bin/qemu-system-hexagon'
TOOLCHAIN_PATH = '/prj/qct/llvm/release/internal/HEXAGON/branch-23.0/linux64/latest/Tools/bin'
LLDB, CC, SIM = '', '', ''

def setup_toolchain(path):
    global LLDB, CC, SIM
    LLDB = os.path.join(path, 'hexagon-lldb')
    CC   = os.path.join(path, 'hexagon-clang')
    SIM  = os.path.join(path, 'hexagon-sim')

#from memory_profiler import profile
#@profile
def _run(cmd, env=None, block=True):
    log.debug(f'Running "{cmd}"')
    try:
        debug_res = subprocess.run(shlex.split(cmd), timeout=225.,
             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env)
    except subprocess.TimeoutExpired as e:
        log.debug(f'timeout while executing "{e.cmd}"')
        raise
    return debug_res

DebugRun = namedtuple('DebugRun', 'cmd,env')

QEMU_MACHINE_NAME = {
    "v68": "V68N_1024",
    "v69": "V69NA_1024",
    "v73": "V73M",
    "v75": "V75NA_1024",
    "v79": "V79NA_1",
    "v81": "V81QA_1",
    "v83": "V83H_1",
    "v85": "V85QA_1",
}

def sim_machine_name(rev):
    if rev == "v69": return "v69na"
    if rev == "v75": return "v75na_1"
    return QEMU_MACHINE_NAME[rev].lower()

def _debug(extra_args, test_case, env=None):
    dump = gen_lldb_script(test_case)
    dump_fname = os.path.realpath(
        os.path.join(test_case.dir, 'test_case_script.lldb'))
    with open(dump_fname, 'wt') as f:
        f.write(dump)

    debug_cmd = f'{LLDB} --batch --source {dump_fname} {test_case.exe} -- {extra_args}'
    run_info = DebugRun(debug_cmd, env)
    return run_info, _run(debug_cmd, env=env)

def _debug_qemu(test_case, qemu=None):
    if qemu is None:
        qemu = test_case.cfg.qemu_bin
    extra_args = f'-M {QEMU_MACHINE_NAME[test_case.cfg.arch]}'
    return _debug(extra_args, test_case,
        env={'LLDB_HEXAGON_USE_QEMU': '1',
             'LLDB_HEXAGON_QEMU_PATH': qemu,
            })

def _debug_hexagon_sim(test_case):
    mach_arg = f'--m{sim_machine_name(test_case.cfg.arch)}'
    extra_args = f'{mach_arg} --pmu_statsfile /dev/null'
    return _debug(extra_args, test_case)

def compile_cmd(tc_bin, cflags, test_input):
    cmd = f'{CC} -g -o {tc_bin} {cflags} {test_input}'
    return cmd

def compile(tc_bin, cflags, test_input):
    cmd = compile_cmd(tc_bin, cflags, test_input)
    return _run(cmd)

def get_inst_tags(iset):
    log.workaround("QTOOL-77570, QTOOL-78875")
    log.workaround("QTOOL-88095")
    log.workaround("QTOOL-95874")
    skip_terms = ('mem', 'swi', 'trap', 'scatter', 'gather', ':raw',
                    'call', 'callr', 'jumpr', 'nmi', 'stop', 'rte', 'wait',
                    'icinv', 'tlbw', 'tlbp', 'tlbinvasid', 'k0lock', 'k0unlock',
                    'tlblock', 'tlbunlock', 'vhist', 'vwhist', 'dealloc',
                    'loop',
                    # questionable?
                    'dcclean', 'dcinv', 'l2lock', 'l2unlock','l2fetch',
                    # should be changed to refer to a valid memory addr:
                    'release', 'dmlink',
                    # known failures:
                    'start', 'setprio', 'diag', 'dczero','resume',
                    # QTOOL-77570, QTOOL-78875
                    'sfmax', 'sfmin', 'dfmin', 'dfmax',
                    # QTOOL-88095
                    'vshuff', 'vdeal',
                    # QTOOL-95874:
                    'vcombine',
                    # misaligned stores:
                    'allocframe',
                    # odd debug behavior:
                    'pause',
                    # insn that read/write these values could cause the program
                    # to run amok and/or not terminate as expected
                    'r29', 'r30', 'r31',
                    )
    log.workaround("QTOOL-88573")
    omit_attrs = ('A_FAKEINSN', 'A_MAPPING', 'A_VECX', 'A_EXPERIMENTAL',
                # QTOOL-88573
                'A_CVI_VS_3SRC',
                # For now, until we can create sane tests for these:
                'A_HMX', 'A_AUDIO', 'A_PRIV',
                'A_IMPLICIT_READS_Z', 'A_IMPLICIT_WRITES_Z',
                # New cores don't always have cabac support:
                'A_CABAC',
                # v79 cores implemented at hexagon-sim don't support this
                'A_HVX_IEEE_FP',
                # Toolchain fails to compile those at the moment
                'A_EXTENSION_AUDIO',
                )

    def skip_inst(inst):
        attrs = inst['attrs'].split(',')
        tag = inst['tag']

        return tag.startswith('dep_') or 'alloc' in tag or \
                any(attr in attrs for attr in omit_attrs) or \
                any(skip in inst['syntax'] for skip in skip_terms)

    for tag in sorted(iset.keys()):
        inst = iset[tag]
        if not skip_inst(inst):
            yield tag

def gen_lldb_script(test_case):
    # FIXME: these regs:
        #register read badva0 badva1 ssr ccr
        # memory read `*(uint32_t *)memory_access` => this results in QEMU error:
            # ERROR:../build/target/hexagon/gdbstub.c:115:hexagon_gdb_write_register: code should not be reached\nerror: error: supposed to interpret, but failed: Interpreter couldn't read from memory

    # FIXME: HVX Z reg?
    # ctrl_regs = ' '.join(ctrls)
    # m_regs = ' '.join(m)
    vec_regs = ' '.join(vecs + vec_preds4) # FIXME why not all 8?
    gpr_regs = ' '.join(gprs)
    return f'''breakpoint set --name test_case
breakpoint set --name test_end
breakpoint command add 1 2
register read badva0 ccr
register read {gpr_regs}
register read sa0 lc0 sa1 lc1 p3_0 m0 m1 usr pc ugp gp cs0 cs1 framelimit framekey
register read {vec_regs}
register read ipendad vid vid1 bestwait schedcfg evb modectl syscfg
continue
DONE
run
'''

TestPacket = namedtuple('TestPacket', 'pre_insts,insts,tags,valid_attrs')
def gen_packet(cfg):
    iset = cfg.iset
    tags = cfg.tags
    packet_inst_count = random.randint(1, cfg.inst_per_packet)
    non_solo_tags = [t for t in tags if 'A_RESTRICT_SOLO' not in iset[t]['attrs'].split(',')]
    packet = random.choices(
        non_solo_tags if packet_inst_count > 0 else tags,
        k=packet_inst_count)

    init = []
    case = {}
    has_extender = False
    for tag in packet:
        inst = iset[tag]
        pre, inst_syntax = populate_inst(inst)
        case[tag] = inst_syntax
        if pre is not None:
            init.append(pre)
        has_extender = 'immext' in inst_syntax

    added_mem_inst = 0

    packet_attrs = set()
    if random.random() < 0.5 and len(case) < 4 and not has_extender:
        pre, access = get_mem_access(init, case.values())
        init.append(pre)
        case['synth_mem'] = access
        added_mem_inst += 1

        if len(case) < 4:
            pre, access = get_mem_access(init, case.values())
            init.append(pre)
            case['synth_mem2'] = access
            added_mem_inst += 1

        if added_mem_inst == 2 and random.choice((True, False)):
            packet_attrs.add('mem_noshuf')

    return TestPacket(init, case, packet, packet_attrs)

def gen_case_src_sections(case):
    def get_test_packets():
        for packet_index, (init_insts, packet, tags, possible_attrs) in enumerate(case.packets):
            pre_packet   = '\n    '.join(init_insts)
            actual_attrs = { attr for attr in possible_attrs if random.random() < 0.5 }
            packet_attrs = ':' + ','.join(actual_attrs) if actual_attrs else ''
            test_packet  = '\n    '.join(packet.values())
            packet_debug = ', '.join(tags)
            test_packet = f'''
        {pre_packet}
        {packet_index}:
        // tags: {packet_debug}
        {{
          {test_packet}
        }}{packet_attrs}
    '''
            yield test_packet

    test_packets = '\n'.join(get_test_packets())
    gpr_init = test_gpr_init
    hvx_init = get_hvx_init()
    pred_init = test_pred_init
    mem_init = mem_rand_words
    mem_repeat = str(_MEM_REPEAT)
    mem_padding_repeat = str(_MEM_PADDING_REPEAT)

#   hvx_mutate = mut.vec_rot
    hvx_mutate = get_hvx_init()
    gpr_xor = mut.gpr_xor
    gpr_flip = mut.gpr_flip
    gpr_brev = mut.gpr_brev
    gpr_rot = mut.gpr_rot
    invalid_packet = '.word 0x6fffdffc'

    _jump_targets = [f'''
.align 0x04
.Ljump_target_{i}:
    r{i} = brev(r{i})
    jumpr r31
{invalid_packet}
.skip 0x{skip:x}
    ''' for i, skip in enumerate(random.randint(1, 4192) for _ in range(JUMP_TARGET_CNT))]

    jump_targets = '\n'.join(_jump_targets)

    muts = (
        'xor',
        'rot',
        'flip',
        'brev',
    )
    def pick_mut(i): return muts[i%len(muts)]

    test_cases = [f'''    call test_case
    call mutate_{pick_mut(i)}''' for i in range(case.cfg.test_iters)]
    test_cases = '\n'.join(test_cases)
    return locals()

TestCfg = namedtuple('TestCfg', 'iset,arch,tags,test_iters,test_packets,inst_per_packet,cflags,tmpl,output,qemu_bin,base_qemu')
TestCase = namedtuple('TestCase', 'cfg,packets,dir,exe')

class CompError(Exception):
    pass

def gen_case_prog(test_case):
    filename = os.path.join(test_case.dir, 'out.S')
    case = gen_case_src_sections(test_case)
    with open(filename, 'wt') as f:
        case_text = test_case.cfg.tmpl.substitute(case)
        f.write(case_text)
    p = compile(test_case.exe, test_case.cfg.cflags, filename)
    if p.returncode != 0:
        log.debug(("Compilation failed.\n"
                      "=============== OUTPUT\n"
                      f"{p.stdout.decode('utf-8')}\n"
                      f"{p.stderr.decode('utf-8')}"
                      "================"))
        raise CompError('comp err')
    return case

def run_once(test_case, runfn, version):
    timed_out = False
    run = None
    err = False
    inf = None
    try:
        inf, run = runfn(test_case)
    except subprocess.TimeoutExpired as e:
        timed_out = True
        errf = os.path.join(test_case.dir, f'timeout_{version}.txt')
        with open(errf, 'wb') as f:
            f.write(str(e).encode('utf-8'))
            f.write(b'\n\n\n')
            if e.stdout:
                f.write(e.stdout)
            f.write(b'\n\n\n')
            if e.stderr:
                f.write(e.stderr)
    if run:
        file = os.path.join(test_case.dir, f'output_{version}.txt')
        with open(file, 'wb') as f:
            stdout = adjust_binary_output(run.stdout)
            f.write(stdout)
        file = os.path.join(test_case.dir, f'output_{version}_err.txt')
        with open(file, 'wb') as f:
            f.write(run.stderr)
        err = run.returncode != 0
        del run

    return timed_out, err, inf

def print_info(test_cfg):
    base = SIM if test_cfg.base_qemu is None else test_cfg.base_qemu
    print("Comparing:")
    print(f"  BASE: {base}")
    print(f"  NEW:  {test_cfg.qemu_bin}")

def run_case(test_case):
    if test_case.cfg.base_qemu is None:
        base_fn = _debug_hexagon_sim
    else:
        base_fn = lambda case: _debug_qemu(case, qemu=test_case.cfg.base_qemu)

    base_timed_out, base_err, base_inf = run_once(test_case, base_fn, "base")
    new_timed_out, new_err, new_inf = run_once(test_case, _debug_qemu, "new")

    repro = os.path.join(test_case.dir, 'repro.sh')
    base_env = '\n'.join([f'{key}={val} \\' for key, val in base_inf.env.items()]) if base_inf and base_inf.env else ''
    base_cmd = base_inf.cmd.replace(test_case.dir, '.') if base_inf else '/bin/false'
    new_env = '\n'.join([f'{key}={val} \\' for key, val in new_inf.env.items()]) if new_inf and new_inf.env else ''
    new_cmd = new_inf.cmd.replace(test_case.dir, '.') if new_inf else '/bin/false'
    comp_cmd = compile_cmd('test_out', test_case.cfg.cflags, 'out_repro.S')
    subst_py = os.path.join(os.path.dirname(__file__), '..', 'scripts', 'subst.py')
    adjust_py = os.path.join(os.path.dirname(__file__), 'adjust_output.py')

    with open(repro, 'wt') as f:
        s = os.fstat(f.fileno())
        os.fchmod(f.fileno(), s.st_mode | stat.S_IEXEC)
        f.write(f'''#!/bin/bash
{subst_py} __FILL_IN_DIR__ || exit 1
{comp_cmd} || exit 1

{base_env}
{base_cmd} > base_output.txt

{new_env}
{new_cmd} > new_output.txt

{adjust_py} base_output.txt
{adjust_py} new_output.txt

exec diff base_output.txt new_output.txt
''')
    return (new_timed_out or base_timed_out), (new_err or base_err)

def gen_packet_set(test_cfg):
    packets = []
    for i in range(test_cfg.test_packets):
        packet = None
        while packet is None:
            try:
                packet = gen_packet(test_cfg)
            except PacketGenError:
                pass
        packets.append(packet)
    return packets


TEMPL_FIELDS = '''gpr_brev
        gpr_flip
        gpr_init
        gpr_rot
        gpr_xor
        hvx_init
        hvx_mutate
        invalid_packet
        jump_targets
        pred_init
        mem_padding_repeat
        mem_init
        mem_repeat
        test_cases
        test_packets'''.split()


def create_test(test_cfg):
    cant_compile = True
    tmpdir = mkdtemp(prefix='pkt_')
    iters = 0
    while cant_compile:
        iters += 1
        try:
            packets = gen_packet_set(test_cfg)
            exe = os.path.join(tmpdir,'test_out')
            test_case = TestCase(test_cfg, packets, tmpdir, exe)
            sections = gen_case_prog(test_case)

            for field in TEMPL_FIELDS:
                fname = os.path.join(tmpdir, field)
                with open(fname, 'wt') as f:
                    f.write(sections[field])

        except CompError:
            continue
        else:
            cant_compile = False

        log.info(f'create_test took {iters} tries')
        return test_case
    return None

def gen_test(test_cfg):
    t0 = time.time()
    case = create_test(test_cfg)
    t_sec = time.time() - t0
    log.info(f'test creation took {t_sec:.2f} seconds')

    t0 = time.time()
    timed_out, err = run_case(case)
    t_sec = time.time() - t0
    log.info(f'test run took {t_sec:.2f} seconds')
    if timed_out or err:
        matches = False
    else:
        matches = filecmp.cmp(
                os.path.join(case.dir, 'output_base.txt'),
                os.path.join(case.dir, 'output_new.txt'))

    out_new_fname = os.path.join(case.dir, 'output_new.txt')
    last_line = open(out_new_fname, 'rt').readlines()[-1] if os.path.exists(out_new_fname) else ''

    if matches:
        shutil.rmtree(case.dir)
    elif 'run' in last_line:
        # We sometimes see hard to explain failures where
        # the debugger output looks as if there are breakpoints
        # that weren't caught.  Assume this is not a defect for now
        # and just repeat the test.
        log.info(f'mismatch, re-trying')
        t0 = time.time()
        timed_out, err = run_case(case)
        t_sec = time.time() - t0
        log.info(f'test repeat run took {t_sec:.2f} seconds')
        if err:
            matches = False
        else:
            matches = filecmp.cmp(
                    os.path.join(case.dir, 'output_base.txt'),
                    os.path.join(case.dir, 'output_new.txt'))

        if matches:
            shutil.rmtree(case.dir)

    filecmp.clear_cache()

    return not matches, case
