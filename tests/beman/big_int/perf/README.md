# Performance Measurements

The benchmark results, the methodology behind them, and an index of the source
programs in this directory all live in the project documentation, on the
**Benchmarks** page:
[`doc/modules/ROOT/pages/benchmarks.adoc`](../../../../doc/modules/ROOT/pages/benchmarks.adoc).

It consolidates the small-value optimization micro-benchmarks (`big_int` vs the
builtin integer types), the large-integer comparisons against `boost.cpp_int` and
GMP (`boost.gmp_int`) for multiplication and division, the ECDSA gauge, and the
algorithm-tier analysis with its complexity fits and crossover plots
(`plot_crossover.py`, `plot_vs_libraries.py`, and the CSV data behind them).

The Google Benchmark suite is part of the CMake build (enable it with
`-DBEMAN_BIG_INT_BUILD_BENCHMARKS=ON`, or use a `*-release-benchmarks` preset);
the `*.perf.cpp` and `*.limbs.cpp` programs are standalone and depend on
Boost.Multiprecision and GMP, so they are compiled by hand.
