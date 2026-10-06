#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# compare.py <x64 gcc14 csv,csv> <clang csv> <gcc13 csv> <noifma csv> <m4 csv,csv> -> markdown fragments for the report
import sys, collections, statistics, math

def load(files):
    d = collections.defaultdict(list); order = []
    for fn in files.split(","):
        for l in open(fn):
            if l[0] == "#" or l.startswith("op,") or not l.strip():
                continue
            p = l.strip().split(",")
            k = (p[0], int(p[2]), int(p[3]))
            if k not in order: order.append(k)
            d[k + (p[1],)].append(float(p[6]))
    return {k: statistics.mean(v) for k, v in d.items()}, order

X, order = load(sys.argv[1]); C, _ = load(sys.argv[2]); G13, _ = load(sys.argv[3]); NI, _ = load(sys.argv[4]); M, morder = load(sys.argv[5])

def r(D, op, la, lb, a, b):
    x, y = D.get((op, la, lb, a)), D.get((op, la, lb, b))
    return None if x is None or y is None else x / y
def f(x, d=2): return "-" if x is None else f"{x:.{d}f}"
def band(op, la):
    lim = (16, 4000) if op == "divrem" else (16, 2000)
    return "small" if la <= lim[0] else "medium" if la <= lim[1] else "large"

ops = []
for k in order:
    if k[0] not in ops: ops.append(k[0])
mode = sys.argv[6] if len(sys.argv) > 6 else "tables"
if mode == "heat":
    print("| op | band | x64 gcc14 auto/gmpz (geomean, worst) | M4 auto/gmpz (geomean, worst) |")
    print("|---|---|---|---|")
    for op in ops:
        for b in ("small", "medium", "large"):
            sh = [k for k in order if k[0] == op and band(op, k[1]) == b]
            if not sh: continue
            def agg(D):
                v = [r(D, *k, "auto", "gmpz") for k in sh]; v = [x for x in v if x]
                return "-" if not v else f"{math.exp(sum(map(math.log, v))/len(v)):.2f} ({max(v):.2f})"
            print(f"| {op} | {b} | {agg(X)} | {agg(M)} |")
    sys.exit()
for op in ops:
    print(f"\n#### {op}\n")
    print("| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for k in order:
        if k[0] != op: continue
        a, kn = X.get(k + ("auto",)), X.get(k + ("kernel",))
        d = None if a is None or kn is None else a - kn
        ma, mk = M.get(k + ("auto",)), M.get(k + ("kernel",))
        md = None if ma is None or mk is None else ma - mk
        print(f"| {k[1]}x{k[2]} | {f(r(X,*k,'auto','gmpz'))} | {f(r(X,*k,'kernel','gmp'))} | {f(r(X,*k,'inplace','gmpz'))} | {f(d,0)} | "
              f"{f(r(C,*k,'auto','gmpz'))} | {f(r(C,*k,'kernel','gmp'))} | {f(r(G13,*k,'kernel','gmp'))} | {f(r(NI,*k,'auto','gmpz'))} | "
              f"{f(r(M,*k,'auto','gmpz'))} | {f(r(M,*k,'kernel','gmp'))} | {f(r(M,*k,'inplace','gmpz'))} | {f(md,0)} |")
