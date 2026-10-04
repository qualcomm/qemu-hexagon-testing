#!/bin/bash
# quick.sh sim|qemu test...   (names relative to build/v$ARCHV)
cd "$(dirname "$0")/.."
ARCHV=${ARCHV:-73}
m=$1; shift
exes=(); for t in "$@"; do exes+=("$PWD/build/v$ARCHV/$t"); done
ARCHV=$ARCHV TOOLS=${TOOLS:-/opt/Hexagon_SDK/6.4.0.2/tools/HEXAGON_Tools/19.0.04/Tools} SDK=/opt/Hexagon_SDK/6.4.0.2 \
 H2_INSTALL=$PWD/build/h2-src/artifacts/v$ARCHV/opt/install scripts/run_tests.sh $m "${exes[@]}"
