#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# item1_vshift.py <csv_dir> <prefix> > table.md
# Median over runs of the in-place shift A/B (<prefix>_vshift_<variant>_run<N>.csv), one row per op/shape/row with the
# percentage against base64. Run with python3 -I.
import glob
import os
import statistics
import sys
from collections import defaultdict

VARIANTS = ["base64", "new64", "v1", "v2", "v3"]


def main():
    d, prefix = sys.argv[1], sys.argv[2]
    data = defaultdict(lambda: defaultdict(list))
    for v in VARIANTS:
        for p in sorted(glob.glob(os.path.join(d, f"{prefix}_vshift_{v}_run*.csv"))):
            for line in open(p):
                if line.startswith("op,") or not line.strip():
                    continue
                f = line.strip().split(",")
                data[(f[0], int(f[2]), int(f[3]), f[1])][v].append(float(f[6]))
    print("| op | shape | row | " + " | ".join(VARIANTS) + " |")
    print("|---|---|---|" + "---:|" * len(VARIANTS))
    worst = {v: [] for v in VARIANTS[1:]}
    for key in sorted(data, key=lambda k: (k[0], k[1], k[3] != "auto", k[3] != "inplace", k[3])):
        cells = []
        base = statistics.median(data[key]["base64"]) if data[key].get("base64") else None
        for v in VARIANTS:
            vals = data[key].get(v)
            if not vals:
                cells.append("-")
                continue
            m = statistics.median(vals)
            if v == "base64" or base is None:
                cells.append(f"{m:.1f}")
            else:
                pct = (m - base) / base * 100
                cells.append(f"{m:.1f} ({pct:+.0f}%)")
                if key[3] in ("auto", "inplace"):
                    worst[v].append((pct, key))
        print(f"| {key[0]} | {key[1]}x{key[2]} | {key[3]} | " + " | ".join(cells) + " |")
    print()
    for v in VARIANTS[1:]:
        over = [(p, k) for p, k in worst[v] if p > 3.0]
        print(f"- {v}: {len(over)} of {len(worst[v])} auto/inplace rows more than 3% slower than base64"
              + (": " + ", ".join(f"{k[0]} {k[1]}x{k[2]} {k[3]} {p:+.0f}%" for p, k in sorted(over, reverse=True)) if over else ""))


if __name__ == "__main__":
    main()
