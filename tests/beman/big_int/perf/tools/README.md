<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

# GMP gap analysis tools

Scripts that produced the data in [`../gmp_gap_analysis.md`](../gmp_gap_analysis.md). The harness itself is
`../shape_sweep.cpp` (CMake target `beman.big_int.benchmarks.shape_sweep`, built with
`-DBEMAN_BIG_INT_BUILD_BENCHMARKS=ON -DBEMAN_BIG_INT_SHAPE_SWEEP_ONLY=ON -DBEMAN_BIG_INT_SWEEP_GMP=ON`; `-DBEMAN_BIG_INT_SWEEP_INLINE_BITS=64|128|256|512` sets the inline capacity of the integer type behind every timed row, default 64 = `big_int`). Nothing here is part
of the CMake build or the test suite.

| Tool | What it does and how it is invoked |
|---|---|
| `gap_sweep.sh` | `gap_sweep.sh <shape_sweep> <out.csv> [--seed S] [--ops a,b] [--bands small,medium,large] [--pin "taskset -c 2"]`: runs the report's size grid with every applicable row (add, sub, shl, shr, mul, sqr and divrem also run the `floor` row: the kernel plus one allocate/deallocate pair of the result size, the API floor of `c = a op b`; `--ops vecsort` adds the inline-capacity container row, default off); writes one CSV (`#const` line once, then `op,path,la,lb,reps,rounds,median_ns,min_ns,round_ms_actual`). About 5 minutes per full run. |
| `selftest.sh` | `selftest.sh <shape_sweep>`: every op and row at small shapes; exit status 0 means all correctness checks passed (timings meaningless). |
| `summarize.py` | `summarize.py a.csv [b.csv] [probe.csv] [--ghz G] > summary.md`: per op and band ratio tables (auto/gmpz, kernel/gmp, inplace/gmpz, auto-kernel ns), top gaps, probe table; with two sweep CSVs also the seed spread. Tables carry the floor, `auto - floor` and `inplace - kernel` ns columns when those rows exist. Run with `python3 -I`. |
| `compare.py` | `compare.py <x64 csvs> <clang csv> <gcc13 csv> <noifma csv> <m4 csvs> [heat\|tables]`: the report's cross-configuration tables (comma-separated CSV lists average seeds). |
| `probe_tables.py` | `probe_tables.py <probe_x64.csv> <probe_m4.csv> [n] [opt]`: the report's probe table (cycles/limb per compiler, M4 ns/limb x 4.47). |
| `item1_bench.sh` / `item1_tables.py` | The item 1 before/after driver (`item1_bench.sh <bin_dir> <out_dir> <tools_dir> <prefix> [pin]`, needs binaries `shape_sweep.{base64,new64,new128,new256,new512,nosc64}`) and the tables for [`../item1_frontend_results.md`](../item1_frontend_results.md) (`python3 -I item1_tables.py <csv_dir> <prefix> [small medium spots shortcut study]`). |
| `bucket_tables.py` | `bucket_tables.py <buckets.md>`: compact profile table plus the "time saved if linear helpers ran at GMP speed" estimate. |
| `prof.sh` | `prof.sh <shape_sweep> <out_dir> [filter]` (Linux x86-64, `perf`, `kernel.perf_event_paranoid<=1`): LBR profiles and flame graphs for the report's 12 shapes. Env: `FLAMEGRAPH_DIR` (default `$HOME/FlameGraph`), `PIN`, `MINW`, `LABEL`, `SEED`. |
| `classify.py` | `classify.py <profile_dir> <gap.csv> > buckets.md`: buckets the folded stacks from `prof.sh` by the frame nearest the leaf and scales by the median. |
| `probe/` | Standalone linear-kernel probe (`probe.cpp`: library kernels vs loop variants vs GMP mpn). `build_mac.sh` / `run_mac.sh` (env `GMP`, `BIN_DIR`, `OUT`) write an `opt,kernel,variant,n,ns_per_limb` CSV; `build_x64.sh` / `run_x64.sh` (env `GMP`, `BIN_DIR`, `COMPILERS`, `OUT`, `CPU`, `LIMBS`) write cycles/limb CSVs and `add_n` loop listings. `probe --check` verifies every kernel against GMP. |

The run order used for the report: build the harness per configuration, `gap_sweep.sh` per binary, `summarize.py` and
`compare.py` for the tables, `prof.sh` then `classify.py` then `bucket_tables.py` for the profiles, the probe scripts for
the probe tables.
