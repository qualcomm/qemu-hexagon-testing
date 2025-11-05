#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import sys
import os
from string import Template

BASEDIR = os.path.join(os.path.dirname(__file__), "../")
sys.path.append(BASEDIR)
from src.run_test import TEMPL_FIELDS

if __name__ == '__main__':
    tmpl_fname = os.path.join(BASEDIR, "etc", 'test_case.tmpl')
    case = Template(open(tmpl_fname, 'rt').read())

    sections = {}
    for field in TEMPL_FIELDS:
        fname = os.path.join(sys.argv[1], field)
        with open(fname, 'rt') as f:
            sections[field] = f.read()

    case_text = case.substitute(sections)
    with open('out_repro.S', 'wt') as f:
        f.write(case_text)
