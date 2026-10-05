#!/bin/bash
# check_layout.sh <machine>: confirm the built booter (and loadlinux, on the
# real machines) put the h2 kernel at H2K_LOAD_ADDR (physical) and end below
# H2K_GUEST_START.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
machine=${1:?usage: check_layout.sh <machine>}
. "$here/configs/$machine.mk"
SDK=${SDK:-/opt/Hexagon_SDK/6.4.0.2}
READELF=${READELF:-$SDK/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-llvm-readelf}

check_image() {
[ $((H2K_GUEST_START % 0x1000000)) -eq 0 ] || { echo "FAIL $machine: H2K_GUEST_START $H2K_GUEST_START is not 16MB aligned" >&2; exit 1; }

# The kernel image must start at H2K_LOAD_ADDR and end below the guest.
echo "$hdr" | python3 -c '
import re, sys
load, guest, m = int(sys.argv[1], 0), int(sys.argv[2], 0), sys.argv[3]
segs = [(int(f[3], 0), int(f[5], 0)) for f in
        (l.split() for l in sys.stdin) if f and f[0] == "LOAD"]
lo = min(pa for pa, _ in segs)
hi = max(pa + sz for pa, sz in segs)
if lo != load:
    sys.exit("FAIL %s: kernel starts at %#x, expected %#x" % (m, lo, load))
if hi > guest:
    sys.exit("FAIL %s: kernel ends at %#x, past the guest at %#x" % (m, hi, guest))
print("OK %s: kernel %#x-%#x, guest at %#x" % (m, lo, hi, guest))
' "$H2K_LOAD_ADDR" "$H2K_GUEST_START" "$machine/$1"
}

# Real machines (those with LINUX_LINK_ADDR) must also produce loadlinux.
images=(booter)
[ -z "${LINUX_LINK_ADDR:-}" ] || images+=(loadlinux)
for img in "${images[@]}"; do
    elf=$here/build/$machine/$img
    [ -f "$elf" ] || { echo "FAIL $machine: $img was not built" >&2; exit 1; }
    hdr=$("$READELF" -h -l "$elf")
    check_image "$img"
done

