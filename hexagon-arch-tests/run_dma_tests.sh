#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Build the Rust DMA test for each DMA descriptor generation and run it on the
# corresponding hexagon-sim core.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="/opt/Hexagon_SDK/6.4.0.2/tools/HEXAGON_Tools/19.0.04/Tools/bin"
CLANG="${HEXAGON_CLANG:-${TOOL_DIR}/hexagon-clang}"
SIM="${HEXAGON_SIM:-/prj/qct/llvm/release/internal/HEXAGON/branch-23.0/linux64/latest/Tools/bin/hexagon-sim}"

run_one() {
    local arch="$1"
    local sim_core="$2"
    local target_dir="target-dma-v${arch}"
    local flags

    flags="-Clink-arg=-mv${arch}"$'\x1f'"-Clink-arg=-G0"$'\x1f'
    flags+="-Clink-arg=-nostdlib"$'\x1f'"-Ctarget-cpu=hexagonv${arch}"

    echo "=== DMA test on v${arch} ==="
    PATH="${TOOL_DIR}:${PATH}" HEXAGON_CLANG="$CLANG" HEXAGON_ARCH="$arch" \
        CARGO_TARGET_DIR="$target_dir" CARGO_ENCODED_RUSTFLAGS="$flags" \
        cargo +nightly build --release --bin test_dma
    "$SIM" "$sim_core" -- \
        "${target_dir}/hexagon-unknown-none-elf/release/test_dma"
}

# These cores exercise DMA generations 1, 2, 4, 6, and 8, respectively.
run_one 68 --mv68
run_one 73 --mv73
run_one 75 --mv75
run_one 79 --mv79
run_one 81 --mv81nc_1
