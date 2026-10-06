#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# prof.sh <shape_sweep binary> <out_dir> [filter-substring]
# Records LBR call-stack profiles (perf, Linux x86-64, kernel.perf_event_paranoid <= 1) of big_int (auto) and GMP
# (gmpz) for the shapes in LIST, folds them and renders flame graphs into <out_dir>/<op>_<shape>_<row>.{data,out,
# raw.folded,folded,svg}. Then bucket with: classify.py <out_dir> <gap.csv> > buckets.md
# Env: FLAMEGRAPH_DIR (default $HOME/FlameGraph, a clone of github.com/brendangregg/FlameGraph),
#      PIN (default "taskset -c 2"), MINW (flamegraph.pl --minwidth, default 0.5), LABEL (title suffix, default empty),
#      SEED (default 1).
set -u
BIN=$(cd "$(dirname "${1:?usage: prof.sh <shape_sweep> <out_dir> [filter]}")" && pwd)/$(basename "$1")
P=${2:?usage: prof.sh <shape_sweep> <out_dir> [filter]}
FILTER=${3:-}
FG=${FLAMEGRAPH_DIR:-$HOME/FlameGraph}
PIN=${PIN:-taskset -c 2}
mkdir -p "$P"

# Strip template arguments and parameter lists from folded frames.
strip_names() {
    python3 -I -c '
import re,sys
def short(f):
    for _ in range(10):
        g=re.sub(r"<[^<>]*>","",f)
        if g==f: break
        f=g
    f=re.sub(r"\([^()]*\)( const)?","",f)
    return f
for l in sys.stdin:
    l=l.rstrip("\n"); s,_,c=l.rpartition(" ")
    print(";".join(short(x) for x in s.split(";"))+" "+c)'
}

one() { # op shape row
    op=$1
    shape=$2
    row=$3
    name=${op}_${shape}_${row}
    # shellcheck disable=SC2086
    perf record -q -e cycles:u -F 2000 --call-graph lbr -o "$P/$name.data" -- $PIN "$BIN" "$op" --rows "$row" \
        --rounds 1 --round-ms 4000 --seed "${SEED:-1}" "$shape" >"$P/$name.out" 2>&1
    perf script --inline -i "$P/$name.data" 2>/dev/null | "$FG/stackcollapse-perf.pl" >"$P/$name.raw.folded"
    strip_names <"$P/$name.raw.folded" >"$P/$name.folded"
    who=big_int
    case $row in gmpz) who="GMP mpz" ;; inplace) who="big_int in-place" ;; esac
    "$FG/flamegraph.pl" --minwidth "${MINW:-0.5}" --width 1200 --title "$op $shape $who${LABEL:+ ($LABEL)}" \
        "$P/$name.folded" >"$P/$name.svg" 2>/dev/null
    awk -v n="$name" '{c=$NF; t+=c; if ($0 !~ /(^|;)main(;| )/) m+=c} END {printf "%s samples=%d no-main=%.1f%%\n", n, t, 100*m/t}' \
        "$P/$name.folded"
}

LIST="add:16x16:auto add:16x16:gmpz add:1000x1000:auto add:1000x1000:gmpz add:1000x1000:inplace
mul:64x64:auto mul:64x64:gmpz mul:1024x1024:auto mul:1024x1024:gmpz mul:16384x16384:auto mul:16384x16384:gmpz
mul:262144x262144:auto mul:262144x262144:gmpz divrem:256x128:auto divrem:256x128:gmpz divrem:4096x2048:auto
divrem:4096x2048:gmpz divrem:131072x65536:auto divrem:131072x65536:gmpz tochars:10000x10:auto tochars:10000x10:gmpz
fromchars:10000x10:auto fromchars:10000x10:gmpz gcd:1024x1024:auto gcd:1024x1024:gmpz"
for e in $LIST; do
    IFS=: read -r op shape row <<<"$e"
    case "$op:$shape:$row" in *"$FILTER"*) one "$op" "$shape" "$row" ;; esac
done
