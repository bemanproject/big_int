#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# gap_sweep.sh <shape_sweep binary> <out.csv> [--seed S] [--ops op1,op2,...] [--bands small,medium,large]
#              [--pin "taskset -c 2"]
#
# Phase 2 size grid of the GMP gap analysis. Per op and shape the rows auto/inplace/kernel/gmp/gmpz (whichever apply)
# run back to back in one process. Small and medium shapes use the harness defaults (9 rounds of 20 ms), the large band
# uses --rounds 3, gcd 65536 uses --rounds 1. Output: one CSV (op,path,la,lb,reps,rounds,median_ns,min_ns,
# round_ms_actual) with the '#const' line kept once at the top, after '# gap_sweep' comment lines.
# Works on macOS bash 3.2 and Ubuntu bash. --bands is for partial runs (default all three).
# Ops: add sub shl shr cmp mul sqr divrem tochars fromchars gcd (default all).
set -eu

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <shape_sweep binary> <out.csv> [--seed S] [--ops a,b] [--bands small,medium,large] [--pin \"cmd\"]" >&2
    exit 2
fi
BIN=$1
OUT=$2
shift 2
SEED=1
OPS=add,sub,shl,shr,cmp,mul,sqr,divrem,tochars,fromchars,gcd
BANDS=small,medium,large
PIN=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --seed) SEED=$2; shift 2 ;;
        --ops) OPS=$2; shift 2 ;;
        --bands) BANDS=$2; shift 2 ;;
        --pin) PIN=$2; shift 2 ;;
        *) echo "gap_sweep.sh: unknown option $1" >&2; exit 2 ;;
    esac
done
[ -x "$BIN" ] || { echo "gap_sweep.sh: not executable: $BIN" >&2; exit 2; }

want() { case ",$OPS," in *",$1,"*) return 0 ;; esac; return 1; }
band() { case ",$BANDS," in *",$1,"*) return 0 ;; esac; return 1; }

TMP=$(mktemp "${TMPDIR:-/tmp}/gap_sweep.XXXXXX")
trap 'rm -f "$TMP"' EXIT

if [ ! -s "$OUT" ]; then
    {
        echo "# gap_sweep date $(date -u +%Y-%m-%dT%H:%M:%SZ) host $(hostname) $(uname -sm)"
        echo "# gap_sweep bin $BIN seed=$SEED ops=$OPS bands=$BANDS pin=$PIN"
    } >"$OUT"
    first=1
else
    first=0
fi

# run <op> <rows> <rounds> <shape>...
run() {
    op=$1
    rows=$2
    rounds=$3
    shift 3
    echo "[gap_sweep] $(date +%H:%M:%S) $op rows=$rows rounds=$rounds: $*" >&2
    # shellcheck disable=SC2086
    $PIN "$BIN" "$op" --rows "$rows" --rounds "$rounds" --seed "$SEED" "$@" >"$TMP" || {
        echo "gap_sweep.sh: $op failed (shapes: $*)" >&2
        exit 1
    }
    if [ "$first" = 1 ]; then
        grep -m1 '^#const' "$TMP" >>"$OUT"
        grep -m1 '^op,' "$TMP" >>"$OUT"
        first=0
    fi
    grep -v '^#' "$TMP" | grep -v '^op,' >>"$OUT" || true
}

# sweep <op> <rows> <small shapes> <medium shapes> <large shapes>   (a band with no shapes is "-")
sweep() {
    op=$1
    rows=$2
    if band small && [ "$3" != "-" ]; then run "$op" "$rows" 9 $3; fi
    if band medium && [ "$4" != "-" ]; then run "$op" "$rows" 9 $4; fi
    if band large && [ "$5" != "-" ]; then run "$op" "$rows" 3 $5; fi
}

if want add; then
    sweep add auto,inplace,kernel,gmp,gmpz "1x1 2x2 4x4 8x8 16x16" "64x64 256x256 1024x1024 2000x2000 1024x1" \
        "16384x16384 131072x131072 1048576x1048576"
fi
if want sub; then
    sweep sub auto,inplace,kernel,gmp,gmpz "1x1 2x2 4x4 8x8 16x16" "64x64 256x256 1024x1024 2000x2000 1024x1" \
        "16384x16384 131072x131072 1048576x1048576"
fi
if want shl; then
    sweep shl auto,inplace,kernel,kernelip,gmp,gmpz "1x13 4x13 16x13" "256x13 2000x13 2000x77" "131072x13 1048576x13"
fi
if want shr; then
    sweep shr auto,inplace,kernel,gmp,gmpz "1x13 4x13 16x13" "256x13 2000x13 2000x77" "131072x13 1048576x13"
fi
if want cmp; then
    sweep cmp auto,kernel,gmp,gmpz "1 4 16" "256 2000" "131072"
fi
if want mul; then
    sweep mul auto,kernel,gmp,gmpz "1x1 2x2 3x3 4x4 6x6 8x8 12x12 16x16" \
        "32x32 64x64 128x128 256x256 512x512 1000x1000 2000x2000 2000x100" \
        "4096x4096 16384x16384 65536x65536 262144x262144 1048576x1048576 262144x8192"
fi
if want sqr; then
    sweep sqr auto,kernel,gmp,gmpz "4 16" "64 512 2000" "16384 262144 1048576"
fi
if want divrem; then
    sweep divrem auto,kernel,gmp,gmpz "2x1 8x4 32x16" "128x64 512x256 2000x1000 4000x2000 2000x100 65536x1" \
        "16384x8192 131072x65536 1048576x524288"
fi
if want tochars; then
    sweep tochars auto,kernel,gmp,gmpz "1x10 4x10 16x10" "64x10 256x10 2000x10" "16384x10 131072x10 262144x10"
    # Base 16 is a power of two: no span-level kernel row exists.
    sweep tochars auto,gmp,gmpz "16x16" "2000x16" "131072x16"
fi
if want fromchars; then
    sweep fromchars auto,kernel,gmp,gmpz "4x10 16x10" "64x10 256x10 2000x10" "16384x10 131072x10"
fi
if want gcd; then
    sweep gcd auto,kernel,gmp,gmpz "2x2 4x4 16x16" "64x64 256x256 1024x1024 2000x2000" "16384x16384"
    if band large; then run gcd auto,kernel,gmp,gmpz 1 65536x65536; fi
fi
echo "[gap_sweep] $(date +%H:%M:%S) done: $OUT" >&2
