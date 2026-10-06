#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# bucket_tables.py <buckets.md>: compact profile table + "ns saved if linear helpers ran at GMP c/l" estimate
import re, sys
secs = re.split(r"\n## ", open(sys.argv[1]).read())[1:]
rows = []
for s in secs:
    h = s.split("\n")[0]
    name = h.split("  (")[0]
    m = re.search(r"auto median ([\d.]+) ns.*?gmpz median ([\d.]+) ns", h)
    inc = re.findall(r"ours ([\d.]+)%", s)
    ours, gmp = [], []
    for l in s.split("\n"):
        c = [x.strip() for x in l.strip("|").split("|")]
        if len(c) >= 6 and re.match(r"^[\d.]+$", c[1]) and c[0] != "bucket":
            ours.append((c[0], float(c[1])))
            if re.match(r"^[\d.]+$", c[4]): gmp.append((c[3], float(c[4])))
    if not m or "inplace" in name: continue
    rows.append((name, float(m.group(1)), float(m.group(2)), ours, gmp, float(inc[-1]) if inc else 0))
def fmtb(b): return ", ".join(f"{n} {p:.0f}%" for n, p in b[:4] if p >= 1)
def tm(ns): return f"{ns/1e6:.2f} ms" if ns >= 1e6 else f"{ns/1e3:.1f} us" if ns >= 1e3 else f"{ns:.0f} ns"
print("| profile (x64 gcc14) | auto | gmpz | ours top buckets | GMP top buckets | span_ops incl. |")
print("|---|---:|---:|---|---|---:|")
for n, a, g, o, gm, inc in rows:
    print(f"| {n} | {tm(a)} | {tm(g)} | {fmtb(o)} | {fmtb(gm)} | {inc:.0f}% |")
print()
print("| profile | span_ops incl. ns | saved at r=1.5 | saved at r=2.0 | auto -> projected | gmpz | projected ratio |")
print("|---|---:|---:|---:|---:|---:|---:|")
for n, a, g, o, gm, inc in rows:
    if n.startswith("add 16") or n.startswith("mul 64x64") or n.startswith("add 1000"): continue
    if n.startswith("gcd"):
        inc = sum(p for b, p in o if b in ("span_ops", "wide_ops"))
    ns = a * inc / 100
    s1, s2 = ns * (1 - 1 / 1.5), ns * (1 - 1 / 2.0)
    print(f"| {n} | {tm(ns)} | {tm(s1)} | {tm(s2)} | {tm(a-s1)} to {tm(a-s2)} | {tm(g)} | {(a-s1)/g:.2f} to {(a-s2)/g:.2f} (now {a/g:.2f}) |")
