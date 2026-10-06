#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# Time the probe on a quiet Mac (about 1 minute). Builds it first if needed.
# Env: BIN_DIR (default ./probe-bin), OUT (default ./probe_m4.csv), GHZ_NOTE (default 4.47, only written to the header).
# Output: CSV opt,kernel,variant,n,ns_per_limb; c/l ~= ns_per_limb * GHz (frequency is not pinned).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BIN_DIR=${BIN_DIR:-$PWD/probe-bin}
OUT=${OUT:-$PWD/probe_m4.csv}
export BIN_DIR
[ -x "$BIN_DIR/probe.mac.O2" ] || "$HERE/build_mac.sh"
{
    echo "# c/l ~= ns_per_limb * ${GHZ_NOTE:-4.47} (approximate)"
    echo "opt,kernel,variant,n,ns_per_limb"
    for O in O2 O3; do
        "$BIN_DIR/probe.mac.$O" --all | tail -n +2 | awk -F, -v o="$O" '{printf "%s,%s,%s,%s,%s\n", o, $1, $2, $3, $4}'
    done
} >"$OUT"
echo "wrote $OUT"
