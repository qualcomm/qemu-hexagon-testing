#!/bin/bash
# run_tests.sh sim|qemu <test-exe>...
# Run h2 guest programs under the h2 booter.  A test passes if it prints a
# "PASS <name>" line, prints no "FAIL" line, and the simulator exit status is 0.
set -u
mode=$1; shift
ARCHV=${ARCHV:-73}
H2_INSTALL=${H2_INSTALL:?}
BOOTER=$H2_INSTALL/bin/booter
if [ "$mode" = sim ]; then TIMEOUT=${TIMEOUT:-600}; else TIMEOUT=${TIMEOUT:-90}; fi
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/v$ARCHV/logs
mkdir -p "$out"

case $ARCHV in
    68) SIM_ARCH=v68n_1024 ;;
    73) SIM_ARCH=v73na_1 ;;
    *)  SIM_ARCH=v$ARCHV ;;
esac

if [ "$mode" = sim ]; then
    TOOLS=${TOOLS:?}
    ISS=$TOOLS/lib/iss
    cfg=$out/q6ss.cfg
    # Matches the h2 default platform: scripts/timer_v*.cfg
    printf '%s/qtimer.so --csr_base=0xfe280000 --irq_p=2 --freq=19200000 --cnttid=0x01\n%s/l2vic.so 32 0xfe290000\n' "$ISS" "$ISS" > "$cfg"
fi

# Tests known to fail on qemu because of platform/model differences (not gating).
xfail_file=$here/scripts/qemu_xfail.txt
is_xfail() {
    [ "$mode" = qemu ] && [ -f "$xfail_file" ] && grep -q "^$1\b" "$xfail_file"
}

pass=0; fail=0; xfail=0
for exe in "$@"; do
    name=$(basename "$exe")
    log=$out/$name.$mode.log
    if [ "$mode" = sim ]; then
        timeout "$TIMEOUT" "$TOOLS/bin/hexagon-sim" -m$SIM_ARCH --nullptr 0 \
            --simulated_returnval --subsystem_base 0xfe28 --cosim_file "$cfg" \
            -- "$BOOTER" "$exe" > "$log" 2>&1
        rc=$?
    else
        timeout "$TIMEOUT" "${QEMU:-$SDK/tools/Tools/QEMUHexagon/bin/qemu-system-hexagon}" \
            -machine ${QEMU_MACHINE:-V68_H2} -display none -serial stdio -monitor none \
            -kernel "$BOOTER" -append "$exe" > "$log" 2>&1
        rc=$?
    fi
    if [ $rc -eq 0 ] && grep -q "^PASS $name\b" "$log" && ! grep -q "^FAIL" "$log"; then
        echo "PASS $name ($mode)"; pass=$((pass+1))
    elif is_xfail "$name"; then
        echo "XFAIL $name ($mode) rc=$rc, see $log"; xfail=$((xfail+1))
    else
        echo "FAIL $name ($mode) rc=$rc, see $log"; fail=$((fail+1))
    fi
done
echo "$mode: $pass passed, $fail failed, $xfail expected failures"
[ $fail -eq 0 ]
