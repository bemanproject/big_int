#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# item1_tables.py <csv_dir> <prefix> [section ...] > tables.md
# Tables for item1_frontend_results.md from the CSVs written by the item 1 benchmark driver
# (<prefix>_<variant>_seed<S><rep>.csv, <prefix>_<variant>_spot_seed<S>.csv, <prefix>_<variant>_shortcut_run<N>.csv,
# <prefix>_study<N>_run<R>.csv, <prefix>_study<N>_vecsort_run<R>.csv). Run with python3 -I.
# Aggregation: median across runs; divrem shapes of up to 32 limbs use the mean over seeds (the operands change the
# division path), each seed first reduced to the median of its repeats.
import csv
import glob
import os
import statistics
import sys
from collections import defaultdict


def load(path):
    d = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("op,") or not line.strip():
                continue
            p = line.strip().split(",")
            d[(p[0], int(p[2]), int(p[3]), p[1])] = float(p[6])
    return d


def collect(files):
    """files: list of (seed, path). Returns {key: [(seed, value)]}."""
    out = defaultdict(list)
    for seed, path in files:
        for k, v in load(path).items():
            out[k].append((seed, v))
    return out


def agg(runs, key):
    vals = runs.get(key)
    if not vals:
        return None
    op, la = key[0], key[1]
    if op == "divrem" and la <= 32:
        per_seed = defaultdict(list)
        for s, v in vals:
            per_seed[s].append(v)
        return statistics.mean(statistics.median(v) for v in per_seed.values())
    return statistics.median(v for _, v in vals)


def sweep_runs(d, prefix, variant):
    files = []
    for p in sorted(glob.glob(os.path.join(d, f"{prefix}_{variant}_seed*.csv"))):
        tag = os.path.basename(p).split("_seed")[1].split(".")[0]
        files.append((tag[0], p))
    return collect(files)


def spot_runs(d, prefix, variant):
    files = []
    for p in sorted(glob.glob(os.path.join(d, f"{prefix}_{variant}_spot_seed*.csv"))):
        files.append((os.path.basename(p).split("_seed")[1].split(".")[0], p))
    return collect(files)


def f1(x, digits=1):
    return "-" if x is None else f"{x:.{digits}f}"


def arrow(a, b, digits=1):
    if a is None and b is None:
        return "-"
    return f"{f1(a, digits)} -> {f1(b, digits)}"


SMALL = {
    "add": [(1, 1), (2, 2), (4, 4), (8, 8), (16, 16)],
    "sub": [(1, 1), (2, 2), (4, 4), (8, 8), (16, 16)],
    "shl": [(1, 13), (4, 13), (16, 13)],
    "shr": [(1, 13), (4, 13), (16, 13)],
    "mul": [(2, 2), (3, 3), (4, 4), (6, 6), (8, 8), (12, 12), (16, 16)],
    "sqr": [(4, 4), (16, 16)],
    "divrem": [(2, 1), (8, 4), (32, 16)],
    "fromchars": [(4, 10), (16, 10)],
    "tochars": [(4, 10), (16, 10)],
}
MEDIUM = {
    "add": [(64, 64), (256, 256), (1024, 1024), (2000, 2000)],
    "sub": [(64, 64), (1024, 1024)],
    "shl": [(256, 13), (2000, 13), (2000, 77)],
    "shr": [(256, 13), (2000, 13), (2000, 77)],
    "mul": [(32, 32), (64, 64), (256, 256), (1000, 1000), (2000, 2000), (2000, 100)],
    "sqr": [(64, 64), (512, 512), (2000, 2000)],
    "divrem": [(128, 64), (512, 256), (2000, 1000), (4000, 2000), (2000, 100), (65536, 1)],
    "fromchars": [(64, 10), (256, 10), (2000, 10)],
    "tochars": [(64, 10), (256, 10), (2000, 10)],
}


def before_after(d, prefix, table):
    base, new = sweep_runs(d, prefix, "base64"), sweep_runs(d, prefix, "new64")
    print("| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for op, shapes in table.items():
        for la, lb in shapes:
            def g(runs, row):
                return agg(runs, (op, la, lb, row))

            ab, an = g(base, "auto"), g(new, "auto")
            ib, i_n = g(base, "inplace"), g(new, "inplace")
            fb, fn = g(base, "floor"), g(new, "floor")
            zb, zn = g(base, "gmpz"), g(new, "gmpz")
            kb, kn = g(base, "kernel"), g(new, "kernel")

            def sub(a, b):
                return None if a is None or b is None else a - b

            def rat(a, b):
                return None if a is None or b is None or b == 0 else a / b

            print(
                f"| {op} | {la}x{lb} | {arrow(ab, an)} | {arrow(ib, i_n)} | {arrow(sub(ab, fb), sub(an, fn))} | "
                f"{arrow(rat(ab, zb), rat(an, zn), 2)} | {arrow(sub(ab, kb), sub(an, kn))} |"
            )


def spots(d, prefix):
    base, new = spot_runs(d, prefix, "base64"), spot_runs(d, prefix, "new64")
    keys = sorted({k for k in list(base) + list(new)}, key=lambda k: (k[0], k[1], k[3]))
    shapes = []
    for k in keys:
        if (k[0], k[1], k[2]) not in shapes:
            shapes.append((k[0], k[1], k[2]))
    print("| op | shape | auto ns | kernel ns | gmpz ns | auto/gmpz | change in auto |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for op, la, lb in shapes:
        def g(runs, row):
            return agg(runs, (op, la, lb, row))

        ab, an = g(base, "auto"), g(new, "auto")
        z = g(new, "gmpz")
        ch = None if not ab or not an else (an - ab) / ab * 100
        print(
            f"| {op} | {la}x{lb} | {arrow(ab, an, 0)} | {arrow(g(base, 'kernel'), g(new, 'kernel'), 0)} | "
            f"{f1(z, 0)} | {arrow(None if not z else ab / z, None if not z else an / z, 2)} | "
            f"{'-' if ch is None else f'{ch:+.1f}%'} |"
        )


def shortcut(d, prefix):
    rows = {}
    for v in ("new64", "nosc64"):
        files = [(os.path.basename(p), p) for p in sorted(glob.glob(os.path.join(d, f"{prefix}_{v}_shortcut_run*.csv")))]
        rows[v] = collect(files)
    keys = sorted({k for k in rows["new64"] if k[3] == "auto"}, key=lambda k: (k[0], k[1]))
    print("| op | shape | shortcut on (new64) ns | off ns | on - off ns | runs on / off |")
    print("|---|---|---:|---:|---:|---|")
    for k in keys:
        on = [v for _, v in rows["new64"][k]]
        off = [v for _, v in rows["nosc64"].get(k, [])]
        if not on or not off:
            continue
        mo, mf = statistics.median(on), statistics.median(off)
        print(
            f"| {k[0]} | {k[1]}x{k[2]} | {mo:.1f} | {mf:.1f} | {mo - mf:+.1f} | "
            f"{'/'.join(f'{x:.1f}' for x in on)} vs {'/'.join(f'{x:.1f}' for x in off)} |"
        )


STUDY_ROWS = [
    ("add", (1, 1)), ("add", (4, 4)), ("add", (16, 16)),
    ("sub", (1, 1)), ("sub", (4, 4)), ("sub", (16, 16)),
    ("shl", (1, 13)), ("shl", (4, 13)), ("shl", (16, 13)),
    ("shr", (1, 13)), ("shr", (4, 13)), ("shr", (16, 13)),
    ("mul", (1, 1)), ("mul", (2, 2)), ("mul", (4, 4)), ("mul", (8, 8)), ("mul", (16, 16)),
    ("sqr", (4, 4)), ("sqr", (16, 16)),
    ("divrem", (2, 1)), ("divrem", (8, 4)), ("divrem", (32, 16)),
]  # fmt: skip


def study(d, prefix):
    ns = (64, 128, 256, 512)
    runs = {}
    for n in ns:
        files = []
        for p in sorted(glob.glob(os.path.join(d, f"{prefix}_study{n}_run*.csv"))):
            if "vecsort" in p:
                continue
            files.append((os.path.basename(p).split("_run")[1].split(".")[0], p))
        runs[n] = collect(files)
    for row in ("auto", "inplace"):
        print(f"\n**{row} ns**\n")
        print("| op | shape | " + " | ".join(f"N={n}" for n in ns) + " |")
        print("|---|---|" + "---:|" * len(ns))
        for op, (la, lb) in STUDY_ROWS:
            vals = [agg(runs[n], (op, la, lb, row)) for n in ns]
            if all(v is None for v in vals):
                continue
            print(f"| {op} | {la}x{lb} | " + " | ".join(f1(v) for v in vals) + " |")
    print("\n**vecsort, ns per element (100000 one-limb values: copy + sort + sum)**\n")
    print("| row | " + " | ".join(f"N={n}" for n in ns) + " |")
    print("|---|" + "---:|" * len(ns))
    vs = {}
    for n in ns:
        files = [(os.path.basename(p).split("_run")[1].split(".")[0], p)
                 for p in sorted(glob.glob(os.path.join(d, f"{prefix}_study{n}_vecsort_run*.csv")))]
        vs[n] = collect(files)
    for row in ("auto", "builtin", "copy"):
        vals = []
        for n in ns:
            v = [x for _, x in vs[n].get(("vecsort", 100000, 1, row), [])]
            vals.append(None if not v else statistics.median(v))
        print(f"| {row} | " + " | ".join(f1(v, 2) for v in vals) + " |")


if __name__ == "__main__":
    d, prefix = sys.argv[1], sys.argv[2]
    sections = sys.argv[3:] or ["small", "medium", "spots", "shortcut", "study"]
    for s in sections:
        print(f"\n### {s}\n")
        if s == "small":
            before_after(d, prefix, SMALL)
        elif s == "medium":
            before_after(d, prefix, MEDIUM)
        elif s == "spots":
            spots(d, prefix)
        elif s == "shortcut":
            shortcut(d, prefix)
        elif s == "study":
            study(d, prefix)
