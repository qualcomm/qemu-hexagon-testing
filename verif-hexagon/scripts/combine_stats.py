#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from collections import Counter, OrderedDict
import sys
import json

if __name__ == '__main__':
    insn_counter = Counter()
    for fname in sys.argv[1:]:
        with open(fname, 'rt') as f:
            entry = json.load(f)
        print(fname)
        insn_counter.update(entry['inst_counts'])

    counts_by_tag = OrderedDict(sorted(insn_counter.items()))
    stats = {
        'inst_counts': counts_by_tag,
    }
    with open('combined_stats.json', 'wt') as f:
        json.dump(dict(stats), f, indent=4)
