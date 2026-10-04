#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# fetch_h2.sh: clone the h2 hypervisor at H2_REF into H2_SRC unless it is
# already there.  H2_REF may be a branch, tag or commit.
set -euo pipefail
H2_REPO=${H2_REPO:?}
H2_REF=${H2_REF:?}
H2_SRC=${H2_SRC:?}
[ ! -f "$H2_SRC/makefile" ] || exit 0
rm -rf "$H2_SRC"
git init -q "$H2_SRC"
git -C "$H2_SRC" fetch -q --depth 1 "$H2_REPO" "$H2_REF"
git -C "$H2_SRC" checkout -q FETCH_HEAD
