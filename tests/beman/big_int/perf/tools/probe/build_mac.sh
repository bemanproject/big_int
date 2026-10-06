#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# Build the probe with the system clang++ at -O2 and -O3 against a static GMP.
# Env: GMP (default $(brew --prefix gmp)/lib/libgmp.a, else /opt/homebrew/lib/libgmp.a), BIN_DIR (default ./probe-bin).
# Output: $BIN_DIR/probe.mac.O2 and probe.mac.O3.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../../.." && pwd)
BIN_DIR=${BIN_DIR:-$PWD/probe-bin}
if [ -z "${GMP:-}" ]; then
    GMP=$(brew --prefix gmp 2>/dev/null || echo /opt/homebrew)/lib/libgmp.a
fi
[ -f "$GMP" ] || { echo "missing $GMP (set GMP=/path/to/libgmp.a)"; exit 1; }
mkdir -p "$BIN_DIR"
DEFS=""
for pair in by3c:divexact_by3c bdiv_q_1:bdiv_q_1 divexact_1:divexact_1; do
    d=${pair%%:*}
    s=${pair##*:}
    if nm "$GMP" 2>/dev/null | grep -Eq " T _{2,3}gmpn_$s\$"; then
        DEFS="$DEFS -DPROBE_HAVE_$(echo "$d" | tr a-z A-Z)"
    fi
done
for O in O2 O3; do
    # shellcheck disable=SC2086
    clang++ -std=c++23 -$O $DEFS -I "$ROOT/include" "$HERE/probe.cpp" "$GMP" -o "$BIN_DIR/probe.mac.$O"
done
echo "built $BIN_DIR/probe.mac.O2 probe.mac.O3 (defs:$DEFS)"
