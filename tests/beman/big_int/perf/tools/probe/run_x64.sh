#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# Run the probe on x86-64 Linux, pinned to one core, with perf stat. Needs build_x64.sh first (about 4-6 minutes).
# Env: BIN_DIR (default ./probe-bin), OUT (default ./probe-results), CPU (default 2), LIMBS (default 100000000).
# Outputs in $OUT:
#   probe_x64.csv       compiler,opt,kernel,variant,n,cycles_per_limb,ipc
#   probe_x64_ns.csv    compiler,opt,kernel,variant,n,ns_per_limb  (internal steady_clock loop)
#   probe_asm/          objdump listings of the add_n loops
# c/l is a difference of two runs (2R and R reps) so process startup cancels:
#   c/l = (cycles(2R) - cycles(R)) / (R * n);  ipc likewise from instructions.
set -e
BIN_DIR=${BIN_DIR:-$PWD/probe-bin}
OUT=${OUT:-$PWD/probe-results}
CPU=${CPU:-2}
LIMBS=${LIMBS:-100000000}   # limbs processed in the R-run (~0.05-0.6 s per run)
mkdir -p "$OUT/probe_asm"
CSV="$OUT/probe_x64.csv"; NS="$OUT/probe_x64_ns.csv"
echo "compiler,opt,kernel,variant,n,cycles_per_limb,ipc" > "$CSV"
echo "compiler,opt,kernel,variant,n,ns_per_limb" > "$NS"

stat() { # stat BIN VARIANT N REPS -> "cycles instructions"
    taskset -c "$CPU" perf stat -x, -e cycles:u,instructions:u "$1" --variant "$2" --n "$3" --reps "$4" 2>&1 >/dev/null |
        awk -F, '/cycles/ {c=$1} /instructions/ {i=$1} END {print c, i}'
}

for bin in "$BIN_DIR"/probe.*; do
    base=$(basename "$bin"); tag=${base#probe.}; comp=${tag%%.*}; opt=${tag#*.}
    "$bin" --check || { echo "CHECK FAILED $base"; exit 1; }
    for v in $("$bin" --list); do
        for n in 8 64 512; do
            R=$((LIMBS / n))
            set -- $(stat "$bin" "$v" "$n" "$R");        c1=$1; i1=$2
            set -- $(stat "$bin" "$v" "$n" $((2 * R)));  c2=$1; i2=$2
            kern=${v%%:*}; var=${v#*:}
            awk -v c1="$c1" -v c2="$c2" -v i1="$i1" -v i2="$i2" -v R="$R" -v n="$n" -v comp="$comp" -v opt="$opt" \
                -v k="$kern" -v v="$var" 'BEGIN {
                    dc = c2 - c1; di = i2 - i1;
                    printf "%s,%s,%s,%s,%d,%.4f,%.3f\n", comp, opt, k, v, n, dc / (R * n), (dc > 0 ? di / dc : 0) }' >> "$CSV"
        done
    done
    taskset -c "$CPU" "$bin" --all | tail -n +2 | awk -F, -v comp="$comp" -v opt="$opt" \
        '{printf "%s,%s,%s,%s,%s,%s\n", comp, opt, $1, $2, $3, $4}' >> "$NS"
    # hot-loop listings for the add_n variants
    objdump -d -C --no-show-raw-insn "$bin" > "$OUT/probe_asm/.full.$base.txt"
    for f in add_lib add_loop add_addcll4 add_asm4; do
        awk -v f="$f" '/^[0-9a-f]+ <.*>:$/ {p = ($0 ~ "^[0-9a-f]+ <" f "\\(")} p {print}' \
            "$OUT/probe_asm/.full.$base.txt" > "$OUT/probe_asm/$base.$f.txt"
        [ -s "$OUT/probe_asm/$base.$f.txt" ] || echo "not built ($f absent from $base)" > "$OUT/probe_asm/$base.$f.txt"
    done
    rm -f "$OUT/probe_asm/.full.$base.txt"
    echo "done $base"
done
echo "wrote $CSV $NS $OUT/probe_asm/"
