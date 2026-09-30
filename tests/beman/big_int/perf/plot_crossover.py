# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
"""Plot the algorithm-tier timings and crossover points of multiplication,
squaring and division.

Reads the CSV produced by multiplication_stress_bench.test.cpp (columns:
algorithm,limbs,trials,ns_per_mul) and, optionally, the CSV produced by
division_stress_bench.test.cpp (columns:
algorithm,dividend_limbs,divisor_limbs,iters,ns_per_div), and generates:

  crossover_main.png        multiplication: log-log time vs operand size, one
                            line per tier (schoolbook through FFT), and below
                            it each tier's time relative to the fastest tier
                            measured at that size. Vertical lines mark the
                            sizes after which each tier stays ahead of the
                            one below it.

  crossover_square.png      the same for the squaring tiers.

  crossover_division.png    the same for the balanced 2n / n division tiers
                            (schoolbook, Burnikel-Ziegler, Barrett); only with
                            --division.

  crossover_speedup.png     each multiplication tier's speedup over the next
                            lower tier (>1 means the higher tier is faster).

  crossover_normalized.png  ns / limbs^omega per multiplication tier, where
                            omega is the theoretical exponent (n log n for the
                            FFT). Curves that flatten confirm the complexity.

A text summary (crossover_summary.txt) is also written with the fitted
log-log slope vs theoretical complexity and the detected crossover limb
counts (first and stable) between adjacent tiers.

The schoolbook tier is the runtime basecase kernel (multiply-long-runtime,
the one the tier ladder actually calls) when the CSV has it, else the portable
schoolbook row.

Usage:
    python3 plot_crossover.py [csv_path] [--division div_csv] [--out-dir DIR]

Dependencies: matplotlib only (no pandas/numpy).
"""

import argparse
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

try:
    import matplotlib

    matplotlib.use("Agg")  # headless: no display required
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
except ImportError:
    print("matplotlib is required. Install with: pip install matplotlib", file=sys.stderr)
    sys.exit(1)


# Tier order from slowest-asymptotic to fastest-asymptotic: (label, CSV
# algorithm names in order of preference).
MUL_TIERS = [
    ("schoolbook", ["multiply-long-runtime", "schoolbook"]),
    ("karatsuba", ["karatsuba"]),
    ("toom-cook-3", ["toom-cook-3"]),
    ("toom-cook-4", ["toom-cook-4"]),
    ("toom-cook-6.5", ["toom-cook-6.5"]),
    ("toom-cook-8.5", ["toom-cook-8.5"]),
    ("fft", ["fft"]),
]
SQR_TIERS = [
    ("schoolbook", ["square-long-runtime", "square-long"]),
    ("karatsuba", ["square-karatsuba"]),
    ("toom-cook-3", ["square-toom-cook-3"]),
    ("toom-cook-4", ["square-toom-cook-4"]),
    ("toom-cook-6.5", ["square-toom-cook-6.5"]),
    ("toom-cook-8.5", ["square-toom-cook-8.5"]),
    ("fft", ["square-fft"]),
]
DIV_TIERS = [
    ("schoolbook", ["schoolbook"]),
    ("burnikel-ziegler", ["burnikel-ziegler"]),
    ("barrett", ["barrett"]),
]

DISPLAY = {
    "schoolbook": "Schoolbook",
    "karatsuba": "Karatsuba",
    "toom-cook-3": "Toom-Cook 3",
    "toom-cook-4": "Toom-Cook 4",
    "toom-cook-6.5": "Toom-Cook 6.5",
    "toom-cook-8.5": "Toom-Cook 8.5",
    "fft": "FFT (NTT)",
    "burnikel-ziegler": "Burnikel-Ziegler",
    "barrett": "Barrett",
}

# Theoretical complexity exponent omega such that ns ~ limbs^omega. Toom-Cook
# k (or k.5) splits into pieces and needs (pieces_a + pieces_b - 1) products.
THEORETICAL_OMEGA = {
    "schoolbook": 2.0,
    "karatsuba": math.log2(3),  # ~1.585
    "toom-cook-3": math.log(5) / math.log(3),  # ~1.465
    "toom-cook-4": math.log(7) / math.log(4),  # ~1.404
    "toom-cook-6.5": math.log(12) / math.log(6.5),  # ~1.328
    "toom-cook-8.5": math.log(16) / math.log(8.5),  # ~1.294
    "fft": 1.0,  # n log n
}

# Categorical palette in fixed slot order (validated for adjacent-pair CVD
# separation); a tier keeps its color in every figure.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7"]
COLORS = {
    "schoolbook": SERIES[0],
    "karatsuba": SERIES[1],
    "toom-cook-3": SERIES[2],
    "toom-cook-4": SERIES[3],
    "toom-cook-6.5": SERIES[4],
    "toom-cook-8.5": SERIES[5],
    "fft": SERIES[6],
    "burnikel-ziegler": SERIES[1],
    "barrett": SERIES[2],
}
# Markers are a second identity channel next to color.
MARKERS = {
    "schoolbook": "o",
    "karatsuba": "s",
    "toom-cook-3": "^",
    "toom-cook-4": "D",
    "toom-cook-6.5": "v",
    "toom-cook-8.5": "P",
    "fft": "X",
    "burnikel-ziegler": "s",
    "barrett": "^",
}

SURFACE = "#ffffff"
INK = "#0b0b0b"
INK_2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"


def style_axes(ax):
    """Recessive hairline grid and axes; text in ink tokens."""
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(AXIS)
    ax.tick_params(colors=INK_2, which="both")
    ax.xaxis.label.set_color(INK_2)
    ax.yaxis.label.set_color(INK_2)
    ax.grid(True, which="major", color=GRID, linewidth=0.8, linestyle="-")
    ax.grid(True, which="minor", color=GRID, linewidth=0.4, linestyle="-", alpha=0.6)
    ax.set_axisbelow(True)


def format_count(x, _pos=None):
    """1,000-style tick labels for limb counts."""
    if x >= 1:
        return f"{int(round(x)):,}"
    return f"{x:g}"


def format_ns(x, _pos=None):
    """Tick labels for a nanosecond axis in the nearest time unit."""
    for scale, unit in ((1e9, "s"), (1e6, "ms"), (1e3, "us")):
        if x >= scale:
            return f"{x / scale:g} {unit}"
    return f"{x:g} ns"


def format_ratio(x, _pos=None):
    return f"{x:g}x"


def log_axes(ax, x_formatter=format_count, y_formatter=format_ns):
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(FuncFormatter(x_formatter))
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.yaxis.set_major_locator(LogLocator(base=10))
    ax.yaxis.set_major_formatter(FuncFormatter(y_formatter))
    ax.yaxis.set_minor_formatter(NullFormatter())


def plot_series(ax, xs, ys, tier, label=None):
    ax.plot(
        xs,
        ys,
        color=COLORS[tier],
        linewidth=1.6,
        solid_joinstyle="round",
        solid_capstyle="round",
        marker=MARKERS[tier],
        markersize=5,
        markeredgecolor=SURFACE,
        markeredgewidth=0.9,
        label=label,
    )


def load_multiplication_csv(path, tiers):
    """Return {tier: [(limbs, ns), ...]} sorted by limbs."""
    raw = defaultdict(dict)
    with open(path) as f:
        for row in csv.DictReader(f):
            ns = float(row["ns_per_mul"])
            if ns > 0:  # 0 marks a kernel the running CPU cannot execute
                raw[row["algorithm"]][int(row["limbs"])] = ns
    return pick_tiers(raw, tiers)


def load_division_csv(path, tiers):
    """Balanced 2n / n rows only: {tier: [(divisor_limbs, ns), ...]}."""
    raw = defaultdict(dict)
    with open(path) as f:
        for row in csv.DictReader(f):
            dividend = int(row["dividend_limbs"])
            divisor = int(row["divisor_limbs"])
            if dividend == 2 * divisor:
                raw[row["algorithm"]].setdefault(divisor, float(row["ns_per_div"]))
    return pick_tiers(raw, tiers)


def pick_tiers(raw, tiers):
    data = {}
    for tier, names in tiers:
        for name in names:
            if raw.get(name):
                data[tier] = sorted(raw[name].items())
                break
    return data


def fit_log_log_slope(points, min_limb=None):
    """Least-squares slope of log(ns) vs log(limbs).

    Pass min_limb to drop the constant-overhead head so the fit reflects
    asymptotic behaviour rather than fixed per-call setup time.
    """
    if min_limb is not None:
        points = [p for p in points if p[0] >= min_limb]
    if len(points) < 2:
        return None
    xs = [math.log(limbs) for (limbs, _) in points]
    ys = [math.log(ns) for (_, ns) in points]
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((xs[i] - mx) * (ys[i] - my) for i in range(n))
    den = sum((xs[i] - mx) ** 2 for i in range(n))
    return num / den if den else None


def find_crossover(data_a, data_b):
    """Estimate where tier B overtakes tier A: (first, stable).

    `first` is the first limb count at which B is faster, `stable` the one
    after which B stays faster over the rest of the overlapping sizes (ties
    near a fallback gate and the FFT's transform-length steps make the two
    differ). Both interpolate linearly in log-log space between the bracketing
    measurements, and are None when not observed.
    """
    a_map = dict(data_a)
    b_map = dict(data_b)
    common = sorted(set(a_map.keys()) & set(b_map.keys()))
    if len(common) < 2:
        return None, None
    diffs = [math.log(b_map[x]) - math.log(a_map[x]) for x in common]  # <0 where B is faster

    def interpolate(i):
        # Zero of the diff between common[i] (B slower) and common[i + 1] (B faster).
        t = diffs[i] / (diffs[i] - diffs[i + 1])
        return int(round(math.exp(math.log(common[i]) + t * (math.log(common[i + 1]) - math.log(common[i])))))

    first = None
    if diffs[0] <= 0:
        first = common[0]
    else:
        first = next((interpolate(i) for i in range(len(diffs) - 1) if diffs[i + 1] <= 0), None)
    slower = [i for i, d in enumerate(diffs) if d > 0]
    if not slower:
        stable = common[0]
    elif slower[-1] == len(diffs) - 1:
        stable = None
    else:
        stable = interpolate(slower[-1])
    return first, stable


def detect_crossovers(data, tiers):
    """Return list of (left, right, first, stable) for adjacent tiers."""
    order = [tier for tier, _ in tiers if tier in data]
    return [(left, right, *find_crossover(data[left], data[right])) for left, right in zip(order, order[1:])]


def plot_tiers(data, tiers, crossovers, output_path, title, x_label, y_label):
    """Absolute time (top) and time relative to the fastest tier (bottom)."""
    fig, (ax, ax_rel) = plt.subplots(
        2, 1, figsize=(11, 8.5), sharex=True, gridspec_kw={"height_ratios": [3, 2], "hspace": 0.08}
    )
    fig.patch.set_facecolor(SURFACE)
    order = [tier for tier, _ in tiers if tier in data]

    best = {}
    for tier in order:
        for limbs, ns in data[tier]:
            best[limbs] = min(best.get(limbs, math.inf), ns)

    max_ratio = 1.0
    for tier in order:
        xs = [limbs for (limbs, _) in data[tier]]
        ys = [ns for (_, ns) in data[tier]]
        plot_series(ax, xs, ys, tier, DISPLAY[tier])
        ratios = [ns / best[limbs] for (limbs, ns) in data[tier]]
        max_ratio = max(max_ratio, max(ratios))
        plot_series(ax_rel, xs, ratios, tier)

    # Crossover markers: thin lines in the incoming tier's color, labels in
    # muted ink, staggered so neighbours do not collide.
    annotation_ys = [0.97, 0.90, 0.83, 0.76]
    marked = sorted(((x, right) for (_, right, _first, x) in crossovers if x is not None))
    for idx, (x, right) in enumerate(marked):
        for a in (ax, ax_rel):
            a.axvline(x, color=COLORS[right], linewidth=1.0, alpha=0.55, zorder=0)
        ax.annotate(
            f"{DISPLAY[right]} @ {x:,}",
            xy=(x, annotation_ys[idx % len(annotation_ys)]),
            xycoords=("data", "axes fraction"),
            xytext=(4, 0),
            textcoords="offset points",
            ha="left",
            va="center",
            fontsize=8,
            color=INK_2,
            bbox=dict(boxstyle="round,pad=0.2", fc=SURFACE, ec="none", alpha=0.85),
        )

    for a in (ax, ax_rel):
        style_axes(a)
    log_axes(ax)
    ax.set_ylabel(y_label)
    ax.set_title(title, color=INK, loc="left", fontsize=12)
    legend = ax.legend(loc="lower right", frameon=False, labelcolor=INK_2, ncol=2 if len(order) > 4 else 1)
    legend.set_zorder(5)

    ax_rel.set_xscale("log")
    ax_rel.set_yscale("log")
    ax_rel.set_ylim(0.95, min(max_ratio * 1.1, 12.0))
    ax_rel.yaxis.set_major_locator(matplotlib.ticker.FixedLocator([1, 1.5, 2, 3, 5, 8]))
    ax_rel.yaxis.set_major_formatter(FuncFormatter(format_ratio))
    ax_rel.yaxis.set_minor_formatter(NullFormatter())
    ax_rel.xaxis.set_major_formatter(FuncFormatter(format_count))
    ax_rel.xaxis.set_minor_formatter(NullFormatter())
    ax_rel.set_ylabel("time / fastest tier")
    ax_rel.set_xlabel(x_label)

    fig.savefig(output_path, dpi=120, bbox_inches="tight", facecolor=SURFACE)
    plt.close(fig)


def plot_speedup(data, tiers, crossovers, output_path):
    """One subplot per adjacent-tier transition; each shows the speedup
    of the higher-order tier over the lower-order one across the pair's
    actual overlapping limb range.

    Speedup = left_ns / right_ns. A horizontal break-even line at y=1 makes
    the crossover trivially visible (the curve crosses y=1 there).
    """
    crossover_map = {(left, right): stable for (left, right, _first, stable) in crossovers}
    order = [tier for tier, _ in tiers if tier in data]

    pairs = []
    for left, right in zip(order, order[1:]):
        left_map = dict(data[left])
        right_map = dict(data[right])
        common = sorted(set(left_map.keys()) & set(right_map.keys()))
        if len(common) >= 2:
            pairs.append((left, right, common, left_map, right_map))

    if not pairs:
        return False

    n = len(pairs)
    cols = 2 if n > 1 else 1
    rows = (n + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(11, 3.6 * rows), squeeze=False)
    fig.patch.set_facecolor(SURFACE)

    for idx, (left, right, common, left_map, right_map) in enumerate(pairs):
        ax = axes[idx // cols][idx % cols]
        style_axes(ax)
        ys = [left_map[limbs] / right_map[limbs] for limbs in common]
        plot_series(ax, common, ys, right)
        ax.axhline(1.0, color=AXIS, linewidth=1.0)
        cx = crossover_map.get((left, right))
        if cx is not None:
            ax.axvline(cx, color=COLORS[right], linewidth=1.0, alpha=0.55, zorder=0)
            ax.annotate(
                f"crossover @ {cx:,}",
                xy=(cx, 1.0),
                xytext=(4, 4),
                textcoords="offset points",
                fontsize=8,
                color=INK_2,
                bbox=dict(boxstyle="round,pad=0.2", fc=SURFACE, ec="none", alpha=0.85),
            )
        log_axes(ax, y_formatter=format_ratio)
        ax.yaxis.set_major_locator(matplotlib.ticker.FixedLocator([0.2, 0.3, 0.5, 0.7, 1, 1.5, 2, 3, 5]))
        ax.set_xlabel("Operand size (64-bit limbs)")
        ax.set_ylabel(f"{DISPLAY[left]} / {DISPLAY[right]}")
        ax.set_title(f"{DISPLAY[left]} -> {DISPLAY[right]}", color=INK, fontsize=10)

    # Hide any unused subplots in the grid (e.g., if pairs is odd).
    for idx in range(n, rows * cols):
        axes[idx // cols][idx % cols].axis("off")

    fig.suptitle("Speedup per adjacent-tier transition (>1 means the higher tier is faster)", fontsize=12, color=INK)
    fig.tight_layout()
    fig.savefig(output_path, dpi=120, facecolor=SURFACE)
    plt.close(fig)
    return True


def normalizer(tier, limbs):
    if tier == "fft":
        return limbs * math.log2(limbs)
    return limbs ** THEORETICAL_OMEGA[tier]


def plot_normalized(data, tiers, output_path):
    """ns / limbs^omega for each tier. Asymptote = matches theory."""
    fig, ax = plt.subplots(figsize=(11, 6.5))
    fig.patch.set_facecolor(SURFACE)
    style_axes(ax)
    for tier, _ in tiers:
        if tier not in data:
            continue
        omega = "n log n" if tier == "fft" else f"omega={THEORETICAL_OMEGA[tier]:.3f}"
        xs = [limbs for (limbs, _) in data[tier]]
        ys = [ns / normalizer(tier, limbs) for (limbs, ns) in data[tier]]
        plot_series(ax, xs, ys, tier, f"{DISPLAY[tier]} ({omega})")
    log_axes(ax, y_formatter=lambda x, _pos=None: f"{x:g}")
    ax.set_xlabel("Operand size (64-bit limbs)")
    ax.set_ylabel("ns / limbs^omega")
    ax.set_title("Time normalized by theoretical complexity (flat tail = matches theory)", color=INK, loc="left")
    ax.legend(loc="best", frameon=False, labelcolor=INK_2)
    fig.tight_layout()
    fig.savefig(output_path, dpi=120, facecolor=SURFACE)
    plt.close(fig)


def build_summary(name, data, tiers, crossovers):
    """Build a multi-line text summary of fits and crossover points."""
    lines = []
    lines.append(f"{name} tier crossover summary")
    lines.append("=" * 50)
    lines.append("")
    lines.append("Empirical complexity (log-log slope of upper-half data)")
    lines.append("-" * 50)
    lines.append(f"{'tier':<17} {'empirical':>10} {'theoretical':>13}")
    for tier, _ in tiers:
        if tier not in data:
            continue
        pts = data[tier]
        # Drop the lower half so constant overhead doesn't bias the slope.
        tail_cutoff = pts[len(pts) // 2][0] if len(pts) >= 4 else None
        slope = fit_log_log_slope(pts, min_limb=tail_cutoff)
        slope_str = f"{slope:.3f}" if slope is not None else "n/a"
        if tier == "fft":
            theory_str = "n log n"
        elif tier in THEORETICAL_OMEGA:
            theory_str = f"{THEORETICAL_OMEGA[tier]:.3f}"
        else:
            theory_str = "n/a"
        lines.append(f"{tier:<17} {slope_str:>10} {theory_str:>13}")
    lines.append("")
    lines.append("Crossover points in limbs (first: right is first faster; stable: right stays faster)")
    lines.append("-" * 66)
    lines.append(f"{'left':<17} -> {'right':<17} {'first':>12} {'stable':>12}")
    for left, right, first, stable in crossovers:
        first_str = str(first) if first is not None else "not observed"
        stable_str = str(stable) if stable is not None else "not observed"
        lines.append(f"{left:<17} -> {right:<17} {first_str:>12} {stable_str:>12}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "csv",
        nargs="?",
        default=str(Path(__file__).parent / "crossover_tuning_data.csv"),
        help="path to the multiplication_stress_bench CSV",
    )
    parser.add_argument("--division", default=None, help="path to the division_stress_bench CSV")
    parser.add_argument(
        "--out-dir",
        default=None,
        help="directory to write PNGs and summary into (default: alongside CSV)",
    )
    args = parser.parse_args()

    csv_path = Path(args.csv).resolve()
    if not csv_path.exists():
        print(f"error: CSV not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out_dir).resolve() if args.out_dir else csv_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    mul = load_multiplication_csv(csv_path, MUL_TIERS)
    if not mul:
        print(f"error: no multiplication rows in {csv_path}", file=sys.stderr)
        sys.exit(1)
    sqr = load_multiplication_csv(csv_path, SQR_TIERS)

    summaries = []
    written = []

    mul_x = detect_crossovers(mul, MUL_TIERS)
    plot_tiers(
        mul,
        MUL_TIERS,
        mul_x,
        out_dir / "crossover_main.png",
        "Multiplication tiers, each forced at the top level",
        "Operand size (64-bit limbs, both operands)",
        "time per multiplication",
    )
    written.append("crossover_main.png")
    if plot_speedup(mul, MUL_TIERS, mul_x, out_dir / "crossover_speedup.png"):
        written.append("crossover_speedup.png")
    plot_normalized(mul, MUL_TIERS, out_dir / "crossover_normalized.png")
    written.append("crossover_normalized.png")
    summaries.append(build_summary("Multiplication", mul, MUL_TIERS, mul_x))

    if sqr:
        sqr_x = detect_crossovers(sqr, SQR_TIERS)
        plot_tiers(
            sqr,
            SQR_TIERS,
            sqr_x,
            out_dir / "crossover_square.png",
            "Squaring tiers, each forced at the top level",
            "Operand size (64-bit limbs)",
            "time per square",
        )
        written.append("crossover_square.png")
        summaries.append(build_summary("Squaring", sqr, SQR_TIERS, sqr_x))

    if args.division:
        div = load_division_csv(Path(args.division).resolve(), DIV_TIERS)
        if div:
            div_x = detect_crossovers(div, DIV_TIERS)
            plot_tiers(
                div,
                DIV_TIERS,
                div_x,
                out_dir / "crossover_division.png",
                "Division tiers (2n / n limbs), each called directly",
                "Divisor size n (64-bit limbs; dividend 2n)",
                "time per division",
            )
            written.append("crossover_division.png")
            summaries.append(build_summary("Division", div, DIV_TIERS, div_x))

    for name in written:
        print(f"wrote {out_dir / name}")

    summary = "\n".join(summaries)
    summary_path = out_dir / "crossover_summary.txt"
    summary_path.write_text(summary)
    print(f"wrote {summary_path}")
    print()
    print(summary, end="")


if __name__ == "__main__":
    main()
