#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# run_tests.sh <test-exe>...
# Run h2 guest programs under the h2 booter.  A test passes if it prints a
# "PASS <name>" line, prints no "FAIL" line, and the simulator exit status is 0.
set -u
mode=sim
ARCHV=${ARCHV:-73}
H2_INSTALL=${H2_INSTALL:?}
BOOTER=$H2_INSTALL/bin/booter
TIMEOUT=${TIMEOUT:-600}
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/v$ARCHV/logs
mkdir -p "$out"

case $ARCHV in
    68) SIM_ARCH=v68n_1024 ;;
    73) SIM_ARCH=v73na_1 ;;
    *)  SIM_ARCH=v$ARCHV ;;
esac

TOOLS=${TOOLS:?}
ISS=$TOOLS/lib/iss
cfg=$out/q6ss.cfg
# Matches the h2 default platform: scripts/timer_v*.cfg
printf '%s/qtimer.so --csr_base=0xfe280000 --irq_p=2 --freq=19200000 --cnttid=0x01\n%s/l2vic.so 32 0xfe290000\n' "$ISS" "$ISS" > "$cfg"

pass=0; fail=0
for exe in "$@"; do
    name=$(basename "$exe")
    log=$out/$name.$mode.log
    timeout "$TIMEOUT" "$TOOLS/bin/hexagon-sim" -m$SIM_ARCH --nullptr 0 \
        --simulated_returnval --subsystem_base 0xfe28 --cosim_file "$cfg" \
        -- "$BOOTER" "$exe" > "$log" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && grep -q "^PASS $name\b" "$log" && ! grep -q "^FAIL" "$log"; then
        echo "PASS $name ($mode)"; pass=$((pass+1))
    else
        echo "FAIL $name ($mode) rc=$rc, see $log"; fail=$((fail+1))
    fi
done
echo "$mode: $pass passed, $fail failed"
[ $fail -eq 0 ]
