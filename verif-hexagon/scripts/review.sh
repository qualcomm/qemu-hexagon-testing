#!/bin/bash
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

# Use this script to review test results: identify
# failure trends, screen out "non-interesting" failure
# modes.

tmp_f=$(mktemp)
for case in $(find packet_test_* -type d -name 'pkt_*')
do
    sdiff ${case}/output_{base,new}.txt |egrep '\|' > ${tmp_f} || continue
    echo ${case}
    head -n 3 ${tmp_f}
    reg=$(head -n 1 ${tmp_f} | egrep -o '.*\|' | egrep -m 1 -o '((r|v)[[:digit:]]+|ssr)')
    if [[ "${reg}" != "" ]]; then
        grep -h -C5 ${reg} ${case}/test_packets
    fi
    echo -e '\n\n\n'
done
rm ${tmp_f}
