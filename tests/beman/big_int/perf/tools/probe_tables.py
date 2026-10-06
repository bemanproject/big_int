#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# probe_tables.py <probe_x64.csv> <probe_m4.csv> [n] [opt]: x64 cycles/limb per compiler + M4 ns/limb*4.47, markdown
import csv, sys, collections
x = [r for r in csv.DictReader(l for l in open(sys.argv[1]) if not l.startswith("#"))]
m = [r for r in csv.DictReader(l for l in open(sys.argv[2]) if not l.startswith("#"))]
n = sys.argv[3] if len(sys.argv) > 3 else "64"
opt = sys.argv[4] if len(sys.argv) > 4 else "O2"
X = {(r["compiler"], r["kernel"], r["variant"]): float(r["cycles_per_limb"]) for r in x if r["n"] == n and r["opt"] == opt}
M = {(r["kernel"], r["variant"]): float(r["ns_per_limb"]) * 4.47 for r in m if r["n"] == n and r["opt"] == opt}
keys = []
for r in x:
    k = (r["kernel"], r["variant"])
    if k not in keys: keys.append(k)
print(f"| kernel | variant | gcc13 c/l | gcc14 c/l | clang23 c/l | M4 c/l | gcc14 / GMP | M4 / GMP |")
print("|---|---|---:|---:|---:|---:|---:|---:|")
for k, v in keys:
    g = X.get(("gcc14", k, "gmp"))
    mg = M.get((k, "gmp"))
    def c(comp):
        z = X.get((comp, k, v)); return "-" if z is None else f"{z:.2f}"
    mv = M.get((k, v))
    r1 = X.get(("gcc14", k, v));
    print(f"| {k} | {v} | {c('gcc13')} | {c('gcc14')} | {c('clang23')} | {'-' if mv is None else f'{mv:.2f}'} | "
          f"{'-' if not (g and r1) else f'{r1/g:.2f}'} | {'-' if not (mg and mv) else f'{mv/mg:.2f}'} |")
