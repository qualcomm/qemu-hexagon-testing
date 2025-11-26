#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import multiprocessing as mp
from collections import namedtuple, Counter, OrderedDict
import os.path
import argparse
import time
from string import Template
import shutil
import json
import sys

from src import log
from src.run_test import QEMU, TOOLCHAIN_PATH

BASEDIR = os.path.join(os.path.dirname(__file__), "../")

def run_verif(suite_cfg):
    from src.run_test import gen_test, print_info
    print_info(suite_cfg.test_cfg)
    with mp.Pool(processes=suite_cfg.proc_count) as p:
        results = []
        for i in range(suite_cfg.test_count):
            res = p.apply_async(gen_test, (suite_cfg.test_cfg,) )
            results.append(res)

        t0 = time.time()
        passes = 0
        total_packets = 0
        tag_count = Counter()
        bar = log.progress_bar('Running tests', len(results))
        for i, res in enumerate(results):
            try:
                fails, case = res.get(timeout=300.)
            except mp.TimeoutError:
                print('timed out waiting for a result')
                bar.update(i + 1)
                continue

            for packet in case.packets:
                tag_count.update(packet.tags)

            total_packets += len(case.packets) * case.cfg.test_iters

            if fails:
                os.makedirs(suite_cfg.test_cfg.output, exist_ok=True)
                shutil.move(case.dir, suite_cfg.test_cfg.output)

                new_case_dir = os.path.join(suite_cfg.test_cfg.output,
                    os.path.basename(case.dir))
                repro = os.path.join(new_case_dir, 'repro.sh')
                with open(repro, 'rt') as f:
                    subst_text = f.read()

                subst_text = subst_text.replace('__FILL_IN_DIR__',
                    new_case_dir)
                with open(repro, 'wt') as f:
                    f.write(subst_text)

                print('case:',case.dir, 'to', new_case_dir)
            else:
                passes += 1
            bar.update(i + 1)
        dur_sec = time.time() - t0
        print(f'{passes} passes out of {len(results)} runs')
        print(f'test rate: {total_packets / dur_sec:.2f} packets/sec')

        stamp = time.strftime('%Y%d%b_%H%M', time.gmtime())
        counts_by_tag = OrderedDict(sorted(tag_count.items()))
        stats = {
            'inst_counts': counts_by_tag,
        }
        with open(f'verif_stats_{stamp}.json', 'wt') as f:
            json.dump(stats, f, indent=4)
            f.write('\n')

        return passes == len(results)

SuiteCfg = namedtuple('SuiteCfg', 'test_cfg,test_count,proc_count')

def load_iset(args):
    import importlib
    if args.iset is not None:
        path = args.iset
    else:
        arch = args.hex_rev if args.hex_rev is not None else 73
        path = f"/prj/qct/coredev/hexagon/sitelinks/arch/pkg/arch/x86_64/v{arch}_stable/src/arch/iset.py"
    import importlib.util
    try:
        spec = importlib.util.spec_from_file_location("iset", path)
        iset = importlib.util.module_from_spec(spec)
        sys.modules["iset"] = iset
        spec.loader.exec_module(iset)
    except FileNotFoundError as e:
        log.critical((f"No iset.py file found at '{e.filename}'\n"
                       "Please use the --iset option to specify the path"
                       " of a custom iset file."))
    return iset

def parse_args():
    import platform
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-n', '--test-count', type=int,
        help='Number of tests to attempt',
#       default=1000*1000,
        default=500,
        required=False)
    parser.add_argument('-o', '--output-dir', type=str,
        help='Path to put logs/output',
        default=os.path.join(os.getcwd(), 'packet_test_' + platform.node() + '_' + time.strftime('%Y%b%d_%H%M%S')),
        required=False)
    parser.add_argument('-j', '--proc-count', type=int,
        help='Process count',
        default=max(1, int(mp.cpu_count() * .85)),
        required=False)
    parser.add_argument('-p', '--packets-per-case', type=int,
        help='When generating a case, this specifies how many '
         ' packets should be generated',
        default=5,
        required=False)
    parser.add_argument('-i', '--iters-per-case', type=int,
        help='When generating a case, this specifies how many times'
         ' it should iterate',
        default=40,
        required=False)
    parser.add_argument('-k', '--max-insts-per-packet', type=int,
        help='When generating a case, this specifies the max number of'
         ' instructions to go in each packet - note that loads/stores get'
         ' added beyond this number',
        default=6,
        required=False)
    parser.add_argument('-q', '--qemu-bin', type=str,
        help='location of the QEMU binary',
        default=QEMU,
        required=False)
    parser.add_argument('-b', '--base-qemu', type=str,
        help='location of another QEMU binary to be used as reference instead of the sim',
        default=None,
        required=False)
    parser.add_argument('-r', '--hex-rev', type=int,
        help='Hexagon revision to test',
        default=None,
        required=False)
    parser.add_argument('-t', '--iset', type=str,
        help='A custom path to an iset.py to be used',
        default=None,
        required=False)
    parser.add_argument('-l', '--logging', type=int,
        help='Set verbosity level: 0, 1 (default), 2, or 3 (highest verbosity)',
        default=1,
        required=False)
    parser.add_argument('-e', '--exit-code', action='store_true',
        help='Indicate whether all packets succeeded on exit code',
        default=False,
        required=False)
    parser.add_argument('--toolchain-path', type=str,
        help='The path for the hexagon toolchain',
        default=TOOLCHAIN_PATH,
        required=False)

    args = parser.parse_args()
    log.config(args.logging)

    if args.hex_rev is not None and args.iset is not None:
        sys.exit("--iset and --hex-rev are incompatible. Use just one of them.")

    for qemu in (args.qemu_bin, args.base_qemu):
        if qemu is None:
            continue
        if not os.path.isfile(qemu) or not os.access(qemu, os.X_OK):
            sys.exit(f"'{qemu}' is not a valid qemu executable")

    return args

if __name__ == '__main__':
    args = parse_args()
    from src.run_test import TestCfg, get_inst_tags, setup_toolchain
    setup_toolchain(args.toolchain_path)
    test_case_src_template = open(os.path.join(BASEDIR, 'etc/test_case.tmpl'), 'rt').read()
    test_case_src_template = Template(test_case_src_template)
    iset = load_iset(args)
    tags = list(get_inst_tags(iset.iset))
    cflags = f'-g -m{iset.q6version} -mhvx-ieee-fp -mhvx-qfloat -mhvx -mhmx'
    test_cfg = TestCfg(iset.iset, iset.q6version, tags, args.iters_per_case,
        args.packets_per_case, args.max_insts_per_packet, cflags,
        test_case_src_template, args.output_dir, args.qemu_bin, args.base_qemu)
    suite = SuiteCfg(test_cfg, args.test_count, args.proc_count)

    success = run_verif(suite)
    if args.exit_code:
        sys.exit(not success)
