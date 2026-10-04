#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# run_tests.sh <test-exe>...
# Run QuRT programs under the SDK's runelf loader.  A test passes if it prints
# a "PASS <name>" line, prints no "FAIL" line, and the simulator exits 0.
set -u
mode=sim
ARCH=${ARCH:-v73}
SDK=${SDK:-/opt/Hexagon_SDK/6.7.0.0}
TOOLS=${TOOLS:-$SDK/tools/HEXAGON_Tools/21.0.04/Tools}
QURT=$SDK/rtos/qurt/compute$ARCH
RUNELF=$QURT/sdksim_bin/runelf.pbn
TIMEOUT=${TIMEOUT:-300}
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/$ARCH/logs
mkdir -p "$out"

case $ARCH in
    v68) SIM_ARCH=v68n_1024 ;;
    v69) SIM_ARCH=v69na ;;
    v73) SIM_ARCH=v73na_1 ;;
    v75) SIM_ARCH=v75na_1 ;;
    *)   echo "unsupported ARCH $ARCH" >&2; exit 2 ;;
esac

ISS=$TOOLS/lib/iss
# Same platform as the SDK's runOnSimulator target (hexagon_toolchain.cmake)
printf '%s/qtimer.so --csr_base=0xFC900000 --irq_p=3 --freq=19200000 --cnttid=1\n%s/l2vic.so 32 0xFC910000\n' "$ISS" "$ISS" > "$out/q6ss.cfg"
echo "$QURT/debugger/lnx64/qurt_model.so" > "$out/osam.cfg"

pass=0; fail=0; skip=0
for exe in "$@"; do
    name=$(basename "$exe")
    log=$out/$name.$mode.log
    (cd "$out" && timeout "$TIMEOUT" "$TOOLS/bin/hexagon-sim" -m$SIM_ARCH \
        --simulated_returnval --usefs "$(dirname "$exe")" \
        --cosim_file "$out/q6ss.cfg" --l2tcm_base 0xd800 \
        --subsystem_base 0xFC90 --rtos "$out/osam.cfg" \
        "$RUNELF" -- "$exe" --) > "$log" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && grep -q "^SKIP $name\b" "$log" && ! grep -q "^FAIL" "$log"; then
        echo "SKIP $name ($mode): $(grep -m1 "^SKIP $name" "$log" | cut -d' ' -f3-)"; skip=$((skip+1))
    elif [ $rc -eq 0 ] && grep -q "^PASS $name\b" "$log" && ! grep -q "^FAIL" "$log"; then
        echo "PASS $name ($mode)"; pass=$((pass+1))
    else
        echo "FAIL $name ($mode) rc=$rc, see $log"; fail=$((fail+1))
    fi
done
echo "$mode: $pass passed, $fail failed, $skip skipped"
[ $fail -eq 0 ]
