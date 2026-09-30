# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
"""Plot big_int against Boost.Multiprecision cpp_int and GMP (gmp_int).

Reads a CSV with columns op,limbs,trials,big_int_us,cpp_int_us,gmp_int_us,
one row per run of mul_big_int_vs_gmp_cpp.perf.cpp (op "mul") or
div_big_int_vs_gmp_cpp.perf.cpp (op "div") at the given operand width, and
writes, per operation present:

  vs_libraries_mul.png / vs_libraries_div.png
      log-log time per operation vs operand width for the three libraries
      (top) and each library's time relative to GMP (bottom).

Usage:
    python3 plot_vs_libraries.py [csv_path] [--out-dir DIR] [--note TEXT]

Dependencies: matplotlib only (no pandas/numpy).
"""

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

from plot_crossover import (
    INK,
    INK_2,
    SERIES,
    SURFACE,
    FuncFormatter,
    NullFormatter,
    format_count,
    format_ratio,
    log_axes,
    matplotlib,
    plt,
    style_axes,
)

LIBRARIES = [
    ("big_int_us", "beman::big_int", SERIES[0], "o"),
    ("cpp_int_us", "Boost cpp_int", SERIES[1], "s"),
    ("gmp_int_us", "GMP (gmp_int)", SERIES[2], "^"),
]

OPERATIONS = {
    "mul": ("Multiplication", "Operand size (64-bit limbs, both operands)", "time per multiplication"),
    "div": ("Division", "Dividend size (64-bit limbs; divisor is half as wide)", "time per division"),
}


def load(path):
    """Return {op: [(limbs, {column: us}), ...]} sorted by limbs."""
    rows = defaultdict(list)
    with open(path) as f:
        for row in csv.DictReader(f):
            rows[row["op"]].append((int(row["limbs"]), {col: float(row[col]) for col, *_ in LIBRARIES}))
    for op in rows:
        rows[op].sort(key=lambda r: r[0])
    return rows


def format_us(x, _pos=None):
    for scale, unit in ((1e6, "s"), (1e3, "ms")):
        if x >= scale:
            return f"{x / scale:g} {unit}"
    return f"{x:g} us"


def plot_op(op, rows, output_path, note):
    title, x_label, y_label = OPERATIONS[op]
    fig, (ax, ax_rel) = plt.subplots(
        2, 1, figsize=(11, 8), sharex=True, gridspec_kw={"height_ratios": [3, 2], "hspace": 0.08}
    )
    fig.patch.set_facecolor(SURFACE)
    xs = [limbs for limbs, _ in rows]
    max_ratio = 1.0
    for col, label, color, marker in LIBRARIES:
        ys = [values[col] for _, values in rows]
        ratios = [values[col] / values["gmp_int_us"] for _, values in rows]
        max_ratio = max(max_ratio, max(ratios))
        for a, y in ((ax, ys), (ax_rel, ratios)):
            a.plot(
                xs,
                y,
                color=color,
                linewidth=1.6,
                solid_joinstyle="round",
                marker=marker,
                markersize=5,
                markeredgecolor=SURFACE,
                markeredgewidth=0.9,
                label=label if a is ax else None,
            )
        if col != "gmp_int_us":
            # Selective direct label: the ratio at the widest operand only.
            ax_rel.annotate(
                f"{ratios[-1]:.1f}x" if ratios[-1] < 10 else f"{ratios[-1]:.0f}x",
                xy=(xs[-1], ratios[-1]),
                xytext=(6, 0),
                textcoords="offset points",
                va="center",
                fontsize=9,
                color=INK_2,
            )

    for a in (ax, ax_rel):
        style_axes(a)
    log_axes(ax, y_formatter=format_us)
    ax.set_ylabel(y_label)
    ax.set_title(f"{title}: big_int vs cpp_int vs gmp_int" + (f" ({note})" if note else ""), color=INK, loc="left")
    ax.legend(loc="upper left", frameon=False, labelcolor=INK_2)

    ax_rel.set_xscale("log")
    ax_rel.set_yscale("log")
    ax_rel.set_ylim(0.4, max_ratio * 1.25)
    ax_rel.yaxis.set_major_locator(matplotlib.ticker.FixedLocator([0.5, 1, 2, 5, 10, 20, 50, 100]))
    ax_rel.yaxis.set_major_formatter(FuncFormatter(format_ratio))
    ax_rel.yaxis.set_minor_formatter(NullFormatter())
    ax_rel.xaxis.set_major_formatter(FuncFormatter(format_count))
    ax_rel.xaxis.set_minor_formatter(NullFormatter())
    ax_rel.set_ylabel("time / GMP time")
    ax_rel.set_xlabel(x_label)

    fig.savefig(output_path, dpi=120, bbox_inches="tight", facecolor=SURFACE)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "csv",
        nargs="?",
        default=str(Path(__file__).parent / "vs_libraries_data.csv"),
        help="path to the comparison CSV",
    )
    parser.add_argument("--out-dir", default=None, help="directory to write PNGs into (default: alongside CSV)")
    parser.add_argument("--note", default="", help="machine/build note appended to the titles")
    args = parser.parse_args()

    csv_path = Path(args.csv).resolve()
    if not csv_path.exists():
        print(f"error: CSV not found: {csv_path}", file=sys.stderr)
        sys.exit(1)
    out_dir = Path(args.out_dir).resolve() if args.out_dir else csv_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    data = load(csv_path)
    for op, rows in data.items():
        if op not in OPERATIONS or not rows:
            continue
        path = out_dir / f"vs_libraries_{op}.png"
        plot_op(op, rows, path, args.note)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
