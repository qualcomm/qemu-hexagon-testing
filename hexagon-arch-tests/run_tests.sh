#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Build and run all Hexagon Rust system architecture test cases.
#
# Usage:
#   ./run_tests.sh                  # Build and run on hexagon-sim
#   ./run_tests.sh --build-only     # Build only, don't run
#   ./run_tests.sh --qemu           # Run on QEMU instead of hexagon-sim
#   ./run_tests.sh test_sys_regs    # Run a single test
#
# hexagon-sim requires --timing --bypass_idle for L2VIC and QTimer cosim
# operation (see SDK cosim examples). The cosim config (cosim/q6ss.cfg)
# loads both qtimer.so and l2vic.so.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Configuration
TOOLCHAIN="+nightly"
TARGET="hexagon-unknown-none-elf"
SIM="${HEXAGON_SIM:-hexagon-sim}"
QEMU="${QEMU:-qemu-system-hexagon}"
QEMU_MACHINE="${QEMU_MACHINE:-V81QA_1}"
BUILD_DIR="target/${TARGET}/release"
BUILD_ONLY=0
USE_QEMU=0
SINGLE_TEST=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-only) BUILD_ONLY=1; shift ;;
        --qemu)       USE_QEMU=1; shift ;;
        *)            SINGLE_TEST="$1"; shift ;;
    esac
done

# Filter to single test if specified, otherwise discover from build dir
if [[ -n "$SINGLE_TEST" ]]; then
    TESTS=("$SINGLE_TEST")
fi

echo "=== Building Rust Hexagon Tests ==="
cargo $TOOLCHAIN build --release 2>&1
echo "Build complete."
echo ""

if [[ "$BUILD_ONLY" -eq 1 ]]; then
    echo "Build-only mode: skipping execution."
    exit 0
fi

# Discover test binaries from the build directory
if [[ -z "$SINGLE_TEST" ]]; then
    TESTS=()
    for f in "${BUILD_DIR}"/test_*; do
        [[ -f "$f" && -x "$f" && ! "$f" =~ \. ]] || continue
        TESTS+=("$(basename "$f")")
    done
    IFS=$'\n' TESTS=($(sort <<<"${TESTS[*]}")); unset IFS
fi

if [[ ${#TESTS[@]} -eq 0 ]]; then
    echo "ERROR: No test binaries found in ${BUILD_DIR}"
    exit 1
fi

# Run tests
PASS=0
FAIL=0
ERRORS=()

for test in "${TESTS[@]}"; do
    binary="${BUILD_DIR}/${test}"
    if [[ ! -f "$binary" ]]; then
        echo "SKIP: $test (binary not found)"
        continue
    fi

    echo "--- Running: $test ---"
    rc=0
    if [[ "$USE_QEMU" -eq 1 ]]; then
        output=$(timeout 30 $QEMU -M "$QEMU_MACHINE" -kernel "$binary" -nographic 2>&1) || rc=$?
    else
        output=$($SIM --mv81 --timing --bypass_idle --cosim_file "$SCRIPT_DIR/cosim/q6ss.cfg" -- "$binary" 2>&1) || rc=$?
    fi
    echo "$output"

    if [[ $rc -eq 0 ]]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        ERRORS+=("$test")
    fi
    echo ""
done

echo "=============================="
echo "Results: $PASS passed, $FAIL failed out of ${#TESTS[@]} tests"
if [[ ${#ERRORS[@]} -gt 0 ]]; then
    echo "Failed tests:"
    for e in "${ERRORS[@]}"; do
        echo "  - $e"
    done
    exit 1
fi
echo "All tests PASSED"
exit 0
