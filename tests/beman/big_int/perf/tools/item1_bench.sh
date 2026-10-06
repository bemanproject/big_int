#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# bench_all.sh <bin_dir> <out_dir> <tools_dir> <prefix> [pin command]
# Runs the item 1 before/after sweeps, spot checks, shortcut A/B and inline-capacity study. Never run while building.
BIN=$1; OUT=$2; TOOLS=$3; P=$4; PIN=${5:-}
mkdir -p $OUT
GS=$TOOLS/gap_sweep.sh
OPS=add,sub,shl,shr,mul,sqr,divrem,fromchars
log() { echo "[bench] $(date +%H:%M:%S) $*" >&2; }

log selftest
for b in new64 new128 new256 new512 nosc64; do
  if [ -n "$PIN" ]; then :; fi
  $TOOLS/selftest.sh $BIN/shape_sweep.$b 2>&1 | grep -vc "^selftest: ok" | sed "s/^/selftest-failures $b: /"
done

# B. before/after, interleaved
for seed in 1 2; do for rep in a b; do
  for v in base64 new64; do
    log "sweep $v seed$seed$rep"
    $GS $BIN/shape_sweep.$v $OUT/${P}_${v}_seed${seed}${rep}.csv --seed $seed --ops $OPS --bands small,medium ${PIN:+--pin "$PIN"} 2>/dev/null
  done
done; done

# C. spot checks
spot() { # <binary> <csv> <seed> ; appends
  bin=$1; csv=$2; seed=$3
  rm -f $csv; first=1
  one() { op=$1; rows=$2; shape=$3
    $PIN $bin $op --rows $rows --rounds 3 --seed $seed $shape > $OUT/.spot.tmp || echo "spot failed $op $shape" >&2
    if [ $first = 1 ]; then grep -m1 '^#const' $OUT/.spot.tmp >> $csv; grep -m1 '^op,' $OUT/.spot.tmp >> $csv; first=0; fi
    grep -v '^#' $OUT/.spot.tmp | grep -v '^op,' >> $csv
  }
  one add auto,inplace,kernel,floor,gmp,gmpz 16384x16384
  one mul auto,kernel,floor,gmp,gmpz 2000x2000
  one mul auto,kernel,floor,gmp,gmpz 16384x16384
  one divrem auto,kernel,floor,gmp,gmpz 4096x2048
  one tochars auto,kernel,gmp,gmpz 10000x10
  one fromchars auto,kernel,gmp,gmpz 10000x10
}
for seed in 1 2; do
  for v in base64 new64; do log "spot $v seed$seed"; spot $BIN/shape_sweep.$v $OUT/${P}_${v}_spot_seed${seed}.csv $seed; done
done

# D. basecase shortcut A/B: new64 (shortcut on) vs nosc64 (off), interleaved
for rep in 1 2 3; do
  for v in new64 nosc64; do
    log "shortcut $v run$rep"
    f=$OUT/${P}_${v}_shortcut_run${rep}.csv; rm -f $f
    { $PIN $BIN/shape_sweep.$v mul --rows auto,kernel --rounds 9 --seed 1 2x2 3x3 4x4 6x6 8x8 12x12 16x16
      $PIN $BIN/shape_sweep.$v sqr --rows auto,kernel --rounds 9 --seed 1 4 16; } | grep -v '^# \|^op,' > $f.tmp
    { echo "# shortcut A/B $v run$rep"; echo "op,path,la,lb,reps,rounds,median_ns,min_ns,round_ms_actual"; grep -v '^#const' $f.tmp; } > $f; rm -f $f.tmp
  done
done

# E. inline-capacity study (small band on the new code) and vecsort, interleaved over N
for rep in 1 2; do
  for b in 64 128 256 512; do
    log "study N=$b run$rep"
    $GS $BIN/shape_sweep.new$b $OUT/${P}_study${b}_run${rep}.csv --seed $rep --ops add,sub,shl,shr,mul,sqr,divrem --bands small ${PIN:+--pin "$PIN"} 2>/dev/null
    $GS $BIN/shape_sweep.new$b $OUT/${P}_study${b}_vecsort_run${rep}.csv --seed $rep --ops vecsort --bands medium ${PIN:+--pin "$PIN"} 2>/dev/null
  done
done
rm -f $OUT/.spot.tmp
echo BENCH-ALLDONE >&2
