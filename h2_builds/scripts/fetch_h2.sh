#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# fetch_h2.sh: clone the h2 hypervisor at H2_REF into H2_SRC unless it is
# already there from that same ref.  H2_REF may be a branch, tag or commit.
# The ref is recorded in $H2_SRC/.h2_ref, so changing H2_REF re-fetches.
set -euo pipefail
H2_REPO=${H2_REPO:?}
H2_REF=${H2_REF:?}
H2_SRC=${H2_SRC:?}
if [ -f "$H2_SRC/makefile" ] && [ "$(cat "$H2_SRC/.h2_ref" 2>/dev/null)" = "$H2_REF" ]; then
    exit 0
fi
rm -rf "$H2_SRC"
git init -q "$H2_SRC"
git -C "$H2_SRC" fetch -q --depth 1 "$H2_REPO" "$H2_REF"
git -C "$H2_SRC" checkout -q FETCH_HEAD
echo "$H2_REF" > "$H2_SRC/.h2_ref"
