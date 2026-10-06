#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# Build the probe on x86-64 Linux: each compiler in COMPILERS at -O2 and -O3, -march=native (about 1 minute).
# Env: GMP (default /usr/lib/x86_64-linux-gnu/libgmp.a; point it at a custom build's libgmp.a if you have one),
#      BIN_DIR (default ./probe-bin), COMPILERS (default "gcc13:g++-13 gcc14:g++-14 clang23:clang++-23"; a missing
#      compiler is skipped). Output: $BIN_DIR/probe.<tag>.<opt>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../../.." && pwd)
GMP=${GMP:-/usr/lib/x86_64-linux-gnu/libgmp.a}
BIN_DIR=${BIN_DIR:-$PWD/probe-bin}
COMPILERS=${COMPILERS:-"gcc13:g++-13 gcc14:g++-14 clang23:clang++-23"}
mkdir -p "$BIN_DIR"
[ -f "$GMP" ] || { echo "missing $GMP"; exit 1; }
DEFS=""
for pair in by3c:divexact_by3c bdiv_q_1:bdiv_q_1 divexact_1:divexact_1; do
    d=${pair%%:*}; s=${pair##*:}
    if nm "$GMP" 2>/dev/null | grep -Eq " T _{2,3}gmpn_$s\$"; then
        DEFS="$DEFS -DPROBE_HAVE_$(echo $d | tr a-z A-Z)"
    fi
done
echo "gmp defs:$DEFS"
for pair in $COMPILERS; do
    tag=${pair%%:*}; cxx=${pair##*:}
    if ! command -v "$cxx" >/dev/null 2>&1; then echo "SKIP $tag ($cxx not found)"; continue; fi
    for O in O2 O3; do
        $cxx -std=c++23 -$O -march=native -DNDEBUG $DEFS -no-pie -I "$ROOT/include" "$HERE/probe.cpp" "$GMP" \
            -o "$BIN_DIR/probe.$tag.$O"
        echo "built probe.$tag.$O ($($cxx --version | head -1))"
    done
done
