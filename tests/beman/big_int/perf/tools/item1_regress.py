#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# item1_regress.py <csv_dir> <prefix> [threshold_percent] [rows]
# Lists every (op, shape, row) where the new64 binary is more than the threshold (default 3%) slower than base64,
# using the same aggregation as item1_tables.py (median of runs; divrem up to 32 limbs: mean of seeds). Rows default to
# auto,inplace. Run with python3 -I.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import item1_tables as t  # noqa: E402


def main():
    d, prefix = sys.argv[1], sys.argv[2]
    thr = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
    rows = sys.argv[4].split(",") if len(sys.argv) > 4 else ["auto", "inplace"]
    base = t.sweep_runs(d, prefix, "base64")
    new = t.sweep_runs(d, prefix, "new64")
    worse, total = [], 0
    for key in sorted(base):
        if key[3] not in rows:
            continue
        b, n = t.agg(base, key), t.agg(new, key)
        if b is None or n is None:
            continue
        total += 1
        pct = (n - b) / b * 100
        if pct > thr:
            worse.append((pct, key, b, n))
    print(f"{len(worse)} of {total} rows ({','.join(rows)}) are more than {thr}% slower than base64")
    for pct, key, b, n in sorted(worse, reverse=True):
        print(f"  {key[0]} {key[1]}x{key[2]} {key[3]}: {b:.1f} -> {n:.1f} ns ({pct:+.1f}%, {n - b:+.1f} ns)")


if __name__ == "__main__":
    main()
