#!/bin/sh
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

# Build the SDK qprintf example and reproduce test_sdk.py's archive layout.
set -eu

sdk=${1:-${HEXAGON_SDK_ROOT:-/opt/Hexagon_SDK/6.4.0.2}}
output=${2:-$PWD/qemu-qurt-tests-sdk-v81.tar.gz}
tools="$sdk/tools/HEXAGON_Tools/19.0.04"
example="$sdk/examples/qprintf_example"
package=qemu-qurt-tests-bc94e62a20370dfe405220898fc2a64127fd64a6
tmp=$(mktemp -d)
build="$tmp/build"
artifacts="$tmp/$package/sdk/V81QA_1"

trap 'rm -rf "$tmp"' EXIT

test -f "$example/CMakeLists.txt"
test -f "$sdk/rtos/qurt/computev81/sdksim_bin/runelf.pbn"
test -f "$sdk/libs/run_main_on_hexagon/ship/hexagon_toolv19_v81/run_main_on_hexagon_sim"

cmake -S "$example" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$sdk/build/cmake/hexagon_toolchain.cmake" \
    -DHEXAGON_SDK_ROOT="$sdk" \
    -DHEXAGON_TOOLS_ROOT="$tools" \
    -DDSP_VERSION=v81 \
    -DPREBUILT_LIB_DIR=hexagon_toolv19_v81
cmake --build "$build" --target qprintf_example_q

mkdir -p "$artifacts"
cp "$sdk/rtos/qurt/computev81/sdksim_bin/runelf.pbn" "$artifacts/"
cp "$sdk/libs/run_main_on_hexagon/ship/hexagon_toolv19_v81/run_main_on_hexagon_sim" "$artifacts/"
cp "$build/libqprintf_example_q.so" "$build/libqprintf.so" "$build/libworker_pool.so" "$artifacts/"
tar -C "$tmp" -czf "$output" "$package"

printf 'Created %s\n' "$output"
