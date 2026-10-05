#!/bin/bash
# build_h2.sh <machine>
# Build the h2 kernel, libh2 and booter for one QEMU machine, using the
# placement in configs/<machine>.mk.  Output goes to build/<machine>/install
# (the same layout as h2's artifacts/v<ARCHV>/opt/install).
#
# Environment:
#   H2_SRC   h2 checkout to build from (default: a clone in build/h2-src)
#   H2_REPO, H2_REF   where to clone from when H2_SRC is not set
#   SDK      Hexagon SDK root
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
machine=${1:?usage: build_h2.sh <machine>}
cfg=$here/configs/$machine.mk
[ -f "$cfg" ] || { echo "no such config: $cfg" >&2; exit 1; }
# shellcheck disable=SC1090
. "$cfg"

SDK=${SDK:-/opt/Hexagon_SDK/6.4.0.2}
TOOLS=${TOOLS:-$SDK/tools/HEXAGON_Tools/19.0.04/Tools}
H2_REPO=${H2_REPO:-https://github.com/androm3da/hexagon-hypervisor}
H2_REF=${H2_REF:-bcain/qemu_boot}
H2_SRC=${H2_SRC:-$here/build/h2-src}
export PATH=$TOOLS/bin:$PATH

if [ ! -f "$H2_SRC/makefile" ]; then
    git clone --depth 1 --branch "$H2_REF" "$H2_REPO" "$H2_SRC"
fi

# h2 keeps per-ARCHV artifacts inside its tree and generates architecture- and
# placement-specific files there, so build every machine in a private copy.
work=$here/build/$machine
rm -rf "$work"
mkdir -p "$work"
rsync -a --exclude .git --exclude artifacts "$H2_SRC"/ "$work/h2-src"/
src=$work/h2-src
install=$src/artifacts/v$ARCHV/opt/install

# USE_PKW=0: the internal "pkw" tool selector is not available outside Qualcomm.
# NULL_ANGEL_TRAP=1, SHUTDOWN_AFTER_GUEST_EXIT=1: the same as toolchain_for_hexagon.
common=(USE_PKW=0 ARCHV="$ARCHV" NULL_ANGEL_TRAP=1 SHUTDOWN_AFTER_GUEST_EXIT=1
        H2K_LOAD_ADDR="$H2K_LOAD_ADDR" H2K_GUEST_START="$H2K_GUEST_START")
make -C "$src" -j"$(nproc)" TARGET=opt "${common[@]}" > "$work/build.log" 2>&1 \
    || { tail -30 "$work/build.log" >&2; echo "FAIL $machine: h2 build" >&2; exit 1; }

if [ -n "${LINUX_LINK_ADDR:-}" ]; then
    make -C "$src/linux" -j"$(nproc)" "${common[@]}" NO_LOAD=1 \
        LINUX_LINK_ADDR="$LINUX_LINK_ADDR" INSTALLPATH="$install" \
        KERNELPATH="$src/artifacts/v$ARCHV/opt/build/kernel" loadlinux \
        >> "$work/build.log" 2>&1 \
        || { tail -30 "$work/build.log" >&2; echo "FAIL $machine: loadlinux" >&2; exit 1; }
    install -D -m 0755 "$src/linux/loadlinux" "$work/loadlinux"
fi

rm -rf "$work/install"
cp -a "$install" "$work/install"
install -D -m 0755 "$install/bin/booter" "$work/booter"
printf 'machine=%s\narchv=%s\nload=%s\nguest=%s\n' \
    "$machine" "$ARCHV" "$H2K_LOAD_ADDR" "$H2K_GUEST_START" > "$work/placement.txt"
"$here/scripts/check_layout.sh" "$machine"
