#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# item1_summary.py <raw_csv_dir> <out_dir>
# Aggregates the raw per-run CSVs written by item1_bench.sh (and the re-measurement and A/B variants of it) into the
# summary CSVs kept under perf/item1: final_x64.csv, final_m4.csv, study.csv, shortcut.csv, shift_ab.csv. Aggregation is
# the one item1_tables.py uses for the tables of item1_frontend_results.md (median of runs; divrem up to 32 limbs: mean
# of seeds). The raw inputs are named <prefix>_<variant>_seed<S><rep>.csv (sweeps), <prefix>_<variant>_spot_seed<S>.csv,
# <prefix>_<variant>_big<S><rep>.csv (131072x13 shift spot), <prefix>_<variant>_shortcut_run<N>.csv,
# <prefix>_study<N>_run<R>.csv, <prefix>_study<N>_vecsort_run<R>.csv and <prefix>_vshift_<variant>_run<N>.csv.
# Run with python3 -I.
import glob
import os
import statistics
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import item1_tables as t  # noqa: E402

KEYS = ["sweep_inline_bits", "sweep_int_sizeof", "ARCH", "BMI2_ADX", "AVX512_IFMA", "SIMD_MUL", "limb_bits", "ndebug",
        "compiler", "build_type", "cxx_flags"]  # fmt: skip


def const_keys(path):
    """The provenance keys of the first #const line of a raw CSV, as one 'key=value ...' string."""
    with open(path) as f:
        for line in f:
            if line.startswith("#const"):
                kv = dict(tok.split("=", 1) for tok in line.split()[1:] if "=" in tok)
                return " ".join(f"{k}={kv[k]}" for k in KEYS if k in kv)
    return ""


def fmt(v):
    return "" if v is None else f"{v:.2f}"


def big_runs(d, prefix, variant):
    out = defaultdict(list)
    for p in sorted(glob.glob(os.path.join(d, f"{prefix}_{variant}_big*.csv"))):
        for k, v in t.load(p).items():
            out[k].append(v)
    return {k: statistics.median(v) for k, v in out.items()}


def final(d, out, machine, pfirst, pr3, pr4):
    """pfirst: 9145b4e measurement; pr3: d689f75 re-measurement; pr4: b823c5f shift re-measurement."""
    base3, new3 = t.sweep_runs(d, pr3, "base64"), t.sweep_runs(d, pr3, "new64")
    base4, new4 = t.sweep_runs(d, pr4, "base64"), t.sweep_runs(d, pr4, "new64")
    first = t.sweep_runs(d, pfirst, "new64")
    sbase3, snew3 = t.spot_runs(d, pr3, "base64"), t.spot_runs(d, pr3, "new64")
    sfirst = t.spot_runs(d, pfirst, "new64")
    bbase4, bnew4 = big_runs(d, pr4, "base64"), big_runs(d, pr4, "new64")
    rows = []
    keys = set(base3) | set(base4) | set(sbase3) | set(bbase4)
    for key in sorted(keys, key=lambda k: (k[0], k[1], k[2], k[3])):
        op, la, lb, row = key
        shift = op in ("shl", "shr")
        if key in bbase4:
            base, after, d689, commit = bbase4[key], bnew4.get(key), None, "b823c5f"
            nine = None
        elif shift and key in base4:
            base, after, d689, commit = t.agg(base4, key), t.agg(new4, key), t.agg(new3, key), "b823c5f"
            nine = t.agg(first, key)
        elif key in base3:
            base, after, d689, commit = t.agg(base3, key), t.agg(new3, key), t.agg(new3, key), "d689f75"
            nine = t.agg(first, key)
        else:
            base, after, d689, commit = t.agg(sbase3, key), t.agg(snew3, key), t.agg(snew3, key), "d689f75"
            nine = t.agg(sfirst, key)
        if base is None and after is None:
            continue
        rows.append((op, f"{la}x{lb}", row, fmt(base), fmt(nine), fmt(d689), fmt(after), commit))
    path = os.path.join(out, f"final_{machine}.csv")
    with open(path, "w") as f:
        f.write(f"# item 1 before/after medians, {machine}; ns per operation (vecsort and the study are in study.csv)\n")
        f.write("# base_ns: cf1cf54 harness (base64); first_9145b4e_ns: first measurement of opt_1; d689f75_ns: re-measurement;\n")
        f.write("# after_ns: final value (b823c5f for shl/shr, d689f75 for the other ops); empty cells were not measured\n")
        f.write("# aggregation: median of the runs, divrem up to 32 limbs the mean of seeds 1 and 2; shl/shr base_ns from the\n")
        f.write("# b823c5f runs; spots (large band, 3-round runs) are medians of two seeds\n")
        for lab, pre, var in (("base64", pr3, "base64"), ("after", pr3, "new64"), ("after_shifts", pr4, "new64")):
            probe = sorted(glob.glob(os.path.join(d, f"{pre}_{var}_seed*.csv")))
            if probe:
                f.write(f"# {lab}: {const_keys(probe[0])}\n")
        f.write("op,shape,row,base_ns,first_9145b4e_ns,d689f75_ns,after_ns,after_commit\n")
        for r in rows:
            f.write(",".join(r) + "\n")
    return path


def study(d, out):
    path = os.path.join(out, "study.csv")
    with open(path, "w") as f:
        f.write("# inline-capacity study (opt_1 at 9145b4e, before the add/sub/shift/single-limb follow-ups); ns per operation\n")
        f.write("# (vecsort: ns per element for 100000 one-limb values, rows auto/builtin/copy); median of two runs (seeds 1 and 2,\n")
        f.write("# divrem the mean of seeds); small band, add/sub/shl/shr/mul/sqr/divrem\n")
        for machine, pre in (("x64", "x64"), ("m4", "mac")):
            for n in (64, 128, 256, 512):
                probe = sorted(glob.glob(os.path.join(d, f"{pre}_study{n}_run1.csv")))
                if probe:
                    f.write(f"# {machine} N={n}: {const_keys(probe[0])}\n")
        f.write("machine,inline_bits,op,shape,row,ns\n")
        for machine, pre in (("x64", "x64"), ("m4", "mac")):
            for n in (64, 128, 256, 512):
                files = [(os.path.basename(p).split("_run")[1].split(".")[0], p)
                         for p in sorted(glob.glob(os.path.join(d, f"{pre}_study{n}_run*.csv"))) if "vecsort" not in p]
                runs = t.collect(files)
                for key in sorted(runs):
                    f.write(f"{machine},{n},{key[0]},{key[1]}x{key[2]},{key[3]},{fmt(t.agg(runs, key))}\n")
                vfiles = [(os.path.basename(p).split("_run")[1].split(".")[0], p)
                          for p in sorted(glob.glob(os.path.join(d, f"{pre}_study{n}_vecsort_run*.csv")))]
                vruns = t.collect(vfiles)
                for key in sorted(vruns):
                    f.write(f"{machine},{n},{key[0]},{key[1]}x{key[2]},{key[3]},{fmt(t.agg(vruns, key))}\n")
    return path


def shortcut(d, out):
    path = os.path.join(out, "shortcut.csv")
    with open(path, "w") as f:
        f.write("# mul_header_basecase_enabled A/B (opt_1 at 9145b4e; the 'off' build is a scratch copy with the constant\n")
        f.write("# set to false, not committed); ns per operation, median of three interleaved runs\n")
        f.write("machine,op,shape,row,on_ns,off_ns\n")
        for machine, pre in (("x64", "x64"), ("m4", "mac")):
            res = {}
            for v in ("new64", "nosc64"):
                files = [(os.path.basename(p), p) for p in sorted(glob.glob(os.path.join(d, f"{pre}_{v}_shortcut_run*.csv")))]
                res[v] = t.collect(files)
            for key in sorted(res["new64"]):
                on = [x for _, x in res["new64"][key]]
                off = [x for _, x in res["nosc64"].get(key, [])]
                if on and off:
                    f.write(f"{machine},{key[0]},{key[1]}x{key[2]},{key[3]},{statistics.median(on):.2f},{statistics.median(off):.2f}\n")
    return path


def shift_ab(d, out):
    path = os.path.join(out, "shift_ab.csv")
    with open(path, "w") as f:
        f.write("# in-place shift A/B (variants built from scratch copies of d689f75; new64 = d689f75, v1 = revert of 2f9150b,\n")
        f.write("# v2 = v1 + register-carried lshift_copy/rshift_copy loops, v3 = v2 with funnel_shl/funnel_shr); ns per operation,\n")
        f.write("# median of three interleaved runs; same compiler and flags as new64 (see final_*.csv)\n")
        f.write("machine,variant,op,shape,row,ns\n")
        for machine, pre in (("x64", "x64"), ("m4", "mac")):
            for v in ("base64", "new64", "v1", "v2", "v3"):
                data = defaultdict(list)
                for p in sorted(glob.glob(os.path.join(d, f"{pre}_vshift_{v}_run*.csv"))):
                    for k, val in t.load(p).items():
                        data[k].append(val)
                for key in sorted(data):
                    f.write(f"{machine},{v},{key[0]},{key[1]}x{key[2]},{key[3]},{statistics.median(data[key]):.2f}\n")
    return path


def main():
    d, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for p in (final(d, out, "x64", "x64", "x64r3", "x64r4"), final(d, out, "m4", "mac", "macr3", "macr4"),
              study(d, out), shortcut(d, out), shift_ab(d, out)):
        print(p, os.path.getsize(p))


if __name__ == "__main__":
    main()
