#!/bin/bash
# run_tests.sh [machine...]
# Build h2_tests' guest programs for each machine's placement and run them on
# qemu-system-hexagon against the kernel built by `make` in this directory.
#
# Environment:
#   QEMU          qemu-system-hexagon to use
#   EXTRA_CFLAGS  passed to the h2_tests build (e.g. -DHAVE_HMX=0 ...)
#   TIMEOUT       seconds per test (default 120)
set -uo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
SDK=${SDK:-/opt/Hexagon_SDK/6.4.0.2}
QEMU=${QEMU:-$SDK/tools/Tools/QEMUHexagon/bin/qemu-system-hexagon}
TIMEOUT=${TIMEOUT:-120}
machines=("$@")
[ ${#machines[@]} -gt 0 ] || mapfile -t machines < <(cd "$here/configs" && ls *.mk | sed 's/\.mk$//')

pass=0 fail=0 xfail=0 xpass=0
for m in "${machines[@]}"; do
    . "$here/configs/$m.mk"
    work=$here/build/$m
    [ -x "$work/booter" ] || { echo "FAIL $m: not built (run make $m)"; fail=$((fail+1)); continue; }

    make -s -C "$here/../h2_tests" ARCHV="$ARCHV" GUEST_START="$H2K_GUEST_START" \
        H2_SRC="$work/h2-src" BUILD="$work/guest" EXTRA_CFLAGS="${EXTRA_CFLAGS:-}" \
        >"$work/guest-build.log" 2>&1 \
        || { tail "$work/guest-build.log"; echo "FAIL $m: guest build"; fail=$((fail+1)); continue; }

    guest=$work/guest/h2_functional
    case $QEMU_BOOT in
    bios-none) boot=(-bios none -kernel "$work/booter") ;;
    kernel)    boot=(-kernel "$work/booter") ;;
    # These machines do not load -kernel; -kernel only supplies the cmdline.
    loader)    boot=(-kernel "$work/booter" -device "loader,file=$work/booter,cpu-num=0") ;;
    esac
    timeout "$TIMEOUT" "$QEMU" -machine "$QEMU_MACHINE" -display none -serial stdio \
        -monitor none -semihosting "${boot[@]}" -append "$guest" >"$work/run.log" 2>&1
    rc=$?
    ok=0
    grep -q '^PASS h2_functional' "$work/run.log" && ! grep -q '^FAIL' "$work/run.log" && [ $rc -eq 0 ] && ok=1
    if grep -qx "$m" "$here/scripts/qemu_xfail.txt"; then
        if [ $ok -eq 1 ]; then echo "XPASS $m"; xpass=$((xpass+1)); else echo "XFAIL $m (rc=$rc)"; xfail=$((xfail+1)); fi
    elif [ $ok -eq 1 ]; then
        echo "PASS  $m"; pass=$((pass+1))
    else
        echo "FAIL  $m (rc=$rc, log: $work/run.log)"; fail=$((fail+1))
    fi
done
echo "qemu: $pass passed, $fail failed, $xfail expected failures, $xpass unexpected passes"
[ $fail -eq 0 ] && [ $xpass -eq 0 ]
