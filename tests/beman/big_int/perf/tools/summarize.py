#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# summarize.py <gap.csv> [probe.csv] [--ghz 4.47] > summary.md
# Ratio tables per op/band from a gap_sweep CSV (m4 or x64), plus the probe table.
import csv, sys, collections, statistics

args = [a for a in sys.argv[1:] if not a.startswith("--")]
ghz = 4.47
if "--ghz" in sys.argv:
    ghz = float(sys.argv[sys.argv.index("--ghz") + 1])
    args = [a for a in args if a != str(ghz) and a != sys.argv[sys.argv.index("--ghz") + 1]]
gaps_csv = [a for a in args if not a.rsplit("/", 1)[-1].startswith("probe")]
probe = next((a for a in args if a.rsplit("/", 1)[-1].startswith("probe")), None)

data = collections.defaultdict(list)  # (op, la, lb, path) -> [median_ns]
order = []
meta = []
for gap in gaps_csv:
    with open(gap) as f:
        for line in f:
            if line.startswith("#"):
                if len(gaps_csv) == 1 or line.startswith("# gap_sweep bin"):
                    meta.append(line.rstrip())
                continue
            if line.startswith("op,") or not line.strip():
                continue
            p = line.strip().split(",")
            k = (p[0], int(p[2]), int(p[3]))
            if k not in order:
                order.append(k)
            data[k + (p[1],)].append(float(p[6]))


def spread(key):
    v = data.get(key)
    return (max(v) - min(v)) / min(v) if v and len(v) > 1 else None


def shape_spread(op, la, lb):
    s = [(spread((op, la, lb, p)), p) for p in ("auto", "inplace", "kernel", "gmp", "gmpz")]
    s = [x for x in s if x[0] is not None]
    return max(s) if s else (None, "")


def med(op, la, lb, path):
    v = data.get((op, la, lb, path))
    return statistics.mean(v) if v else None


def ratio(a, b):
    return None if a is None or b is None or b == 0 else a / b


def fmt(x, d=2):
    return "-" if x is None else f"{x:.{d}f}"


def band(op, la):
    lim = (16, 4000) if op == "divrem" else (16, 2000)
    return "small" if la <= lim[0] else "medium" if la <= lim[1] else "large"


def spr_txt(op, la, lb):
    v, p = shape_spread(op, la, lb)
    return "-" if v is None else f"{v*100:.1f} ({p})" + (" **FLAG**" if v > 0.05 else "")


out = []
out.append("# GMP gap summary\n")
for m in meta:
    if m.startswith("# gap_sweep") or m.startswith("#const"):
        out.append("    " + m[:200])
out.append("")
out.append("Ratios: >1 means big_int is slower. inplace is per-op (already halved). Gap ns = auto - kernel (front end).\n")
gaps = []
ops = []
allspr = [(spread(k), k) for k in data if spread(k) is not None]
if allspr:
    flagged = [x for x in allspr if x[0] > 0.05]
    out.append(f"Seeds: {len(gaps_csv)} CSVs. Spread = (max-min)/min of medians across seeds. "
               f"{len(flagged)} of {len(allspr)} rows exceed 5%.\n")
    out.append("Worst 5 rows: " + "; ".join(
        f"{k[0]} {k[3]} {k[1]}x{k[2]} {v*100:.0f}%" for v, k in sorted(allspr, reverse=True)[:5]) + "\n")
for k in order:
    if k[0] not in ops:
        ops.append(k[0])
for op in ops:
    out.append(f"## {op}\n")
    for b in ("small", "medium", "large"):
        shapes = [k for k in order if k[0] == op and band(op, k[1]) == b]
        if not shapes:
            continue
        out.append(f"**{b}**\n")
        out.append("| shape | auto ns | gmpz ns | auto/gmpz | kernel/gmp | inplace/gmpz | auto-kernel ns | spread % (row) |")
        out.append("|---|---:|---:|---:|---:|---:|---:|---:|")
        for _, la, lb in shapes:
            a, kn, g, z, ip = (med(op, la, lb, x) for x in ("auto", "kernel", "gmp", "gmpz", "inplace"))
            r1, r2, r3 = ratio(a, z), ratio(kn, g), ratio(ip, z)
            d = None if a is None or kn is None else a - kn
            out.append(f"| {la}x{lb} | {fmt(a,1)} | {fmt(z,1)} | {fmt(r1)} | {fmt(r2)} | {fmt(r3)} | {fmt(d,1)} | {spr_txt(op, la, lb)} |")
            if r1:
                gaps.append((r1, "auto/gmpz", op, la, lb))
            if r2:
                gaps.append((r2, "kernel/gmp", op, la, lb))
        out.append("")
for metric in ("auto/gmpz", "kernel/gmp"):
    out.append(f"## Top 5 gaps by {metric} (best shape per op, band >= small)\n")
    out.append("| ratio | op | shape |")
    out.append("|---:|---|---|")
    best = {}
    for r, m, op, la, lb in gaps:
        if m == metric and r > best.get(op, (0,))[0]:
            best[op] = (r, la, lb)
    for op, (r, la, lb) in sorted(best.items(), key=lambda kv: -kv[1][0])[:5]:
        out.append(f"| {r:.2f} | {op} | {la}x{lb} |")
    out.append("")
if probe:
    rows = []
    with open(probe) as f:
        for r in csv.reader(f):
            if not r or r[0].startswith("#") or r[0] == "opt":
                continue
            rows.append(r)
    variants = []
    for r in rows:
        if r[2] not in variants:
            variants.append(r[2])
    cells = collections.OrderedDict()
    for o, kern, var, n, ns in rows:
        cells.setdefault((o, kern, int(n)), {})[var] = float(ns)
    out.append(f"## Probe (ns/limb, ~c/l at {ghz} GHz in parentheses)\n")
    out.append("| opt | kernel | n | " + " | ".join(variants) + " |")
    out.append("|---|---|---:|" + "---:|" * len(variants))
    for (o, kern, n), d in cells.items():
        out.append(f"| {o} | {kern} | {n} | " + " | ".join(
            f"{d[v]:.3f} ({d[v]*ghz:.2f})" if v in d else "-" for v in variants) + " |")
print("\n".join(out))
