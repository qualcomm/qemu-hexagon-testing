#!/bin/bash
# build_h2.sh <repo-url> <ref> <src-dir> <archv>
# Clone the h2 hypervisor (if <src-dir> is not already a checkout) and build
# the kernel, libh2 and booter for the given Hexagon arch version.
set -euo pipefail
repo=$1 ref=$2 src=$3 archv=$4
if [ ! -d "$src/.git" ] && [ ! -f "$src/makefile" ]; then
    git clone --depth 1 --branch "$ref" "$repo" "$src"
fi
# USE_PKW=0: the internal "pkw" tool selector is not available outside Qualcomm.
# NULL_ANGEL_TRAP=1: do not map the angel (semihosting) page; hexagon-sim hangs otherwise.
make -C "$src" USE_PKW=0 NULL_ANGEL_TRAP=1 ARCHV="$archv" build
