// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Probes the scratch_allocator high-water mark for each allocating
// multiplication algorithm (Karatsuba, Toom-Cook 3, Toom-Cook 4, Toom-Cook
// 6.5) across a range of operand sizes. Drives tuning of the *_storage_size
// heuristics in mul_impl.hpp: pick the smallest multiplier that comfortably
// covers the observed peak/s ratio across the algorithm's full active range.
//
// Disabled by default. To run:
//   cmake --preset appleclang-release -DBEMAN_BIG_INT_RUN_BENCHMARKS=ON
//   cmake --build build/appleclang-release
//   ./build/appleclang-release/tests/beman/big_int/beman.big_int.tests.scratch_peak_bench
//
// Output is CSV on stdout (algorithm,limbs,peak_limbs,peak_per_s) followed by
// a per-algorithm worst-case line. Pipe to a file to analyze. Each row uses
// an oversized scratch (10*s) so no algorithm can run out of room; the value
// reported is what the algorithm would minimally need.

#define BEMAN_BIG_INT_INSTRUMENT

#include <beman/big_int.hpp>
#include <beman/big_int/detail/mul_impl.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace local {

using uint_t           = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using std_allocator    = std::allocator<uint_t>;
using scratch_for_test = ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<std_allocator>;

// Over-provision factor for the probe buffer. Comfortably above any current
// or plausible future _storage_size multiplier.
inline constexpr std::size_t over_provision_multiplier = 10;

// Number of operand pairs sampled per (algorithm, limbs) data point. Carry
// propagation in the temporary sums can shift the recursive sub-product
// sizes by a limb or two, which can in turn nudge the high-water mark; this
// catches the worst of that variation.
inline constexpr unsigned samples_per_point = 3;

// Constant seed is intentional: it keeps the bench reproducible run-to-run.
// NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
static std::mt19937_64                       rng{0xC0FFEEULL};
static std::uniform_int_distribution<uint_t> dist;

inline void fill_random(const std::span<uint_t> dest) {
    for (auto& x : dest) {
        x = dist(rng);
    }
    if (!dest.empty() && dest.back() == 0) {
        dest.back() = 1;
    }
}

// Run `algo(result, a, b, scratch)` once with a fresh scratch sized to
// `over_provision_multiplier * limbs` and return the high-water mark recorded
// during the call (in limbs).
template <class Algo>
std::size_t measure_peak_once(const std::size_t limbs, Algo algo) {
    std::vector<uint_t> a(limbs);
    std::vector<uint_t> b(limbs);
    fill_random(a);
    fill_random(b);

    std::vector<uint_t> result(2 * limbs, uint_t{0});

    std_allocator    alloc;
    scratch_for_test scratch(over_provision_multiplier * limbs, alloc);

    algo(std::span<uint_t>{result}, std::span<const uint_t>{a}, std::span<const uint_t>{b}, scratch);

    return scratch.peak();
}

template <class Algo>
std::size_t measure_peak(const std::size_t limbs, Algo algo) {
    std::size_t worst = 0;
    for (unsigned i = 0; i < samples_per_point; ++i) {
        worst = std::max(worst, measure_peak_once(limbs, algo));
    }
    return worst;
}

// Per-algorithm runners. Each calls the algorithm directly so the recursion
// drops down through the same fallback chain as production code (Toom-4
// -> Toom-3 -> Karatsuba -> schoolbook), but the top-level call always
// dispatches to the named algorithm. `cutoff_override=1` on Toom-4 forces it
// to engage at any input size that clears its 3*k correctness gate, matching
// the trick used in multiplication_stress_bench.test.cpp.

std::size_t peak_karatsuba_at(const std::size_t limbs) {
    return measure_peak(limbs,
                        [](const std::span<uint_t>       r,
                           const std::span<const uint_t> a,
                           const std::span<const uint_t> b,
                           scratch_for_test&             s) {
                            ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_karatsuba(
                                r.first(a.size() + b.size()), a, b, s);
                        });
}

std::size_t peak_toom_cook_3_at(const std::size_t limbs) {
    return measure_peak(limbs,
                        [](const std::span<uint_t>       r,
                           const std::span<const uint_t> a,
                           const std::span<const uint_t> b,
                           scratch_for_test&             s) {
                            ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_3(
                                r.first(a.size() + b.size()), a, b, s);
                        });
}

std::size_t peak_toom_cook_4_at(const std::size_t limbs) {
    return measure_peak(limbs,
                        [](const std::span<uint_t>       r,
                           const std::span<const uint_t> a,
                           const std::span<const uint_t> b,
                           scratch_for_test&             s) {
                            ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_4(
                                r.first(a.size() + b.size()), a, b, s, std::size_t{1});
                        });
}

std::size_t peak_toom_cook_6_5_at(const std::size_t limbs) {
    return measure_peak(limbs,
                        [](const std::span<uint_t>       r,
                           const std::span<const uint_t> a,
                           const std::span<const uint_t> b,
                           scratch_for_test&             s) {
                            ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_6_5(
                                r.first(a.size() + b.size()), a, b, s, std::size_t{1});
                        });
}

std::size_t peak_toom_cook_8_5_at(const std::size_t limbs) {
    return measure_peak(limbs,
                        [](const std::span<uint_t>       r,
                           const std::span<const uint_t> a,
                           const std::span<const uint_t> b,
                           scratch_for_test&             s) {
                            ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_8_5(
                                r.first(a.size() + b.size()), a, b, s, std::size_t{1});
                        });
}

struct algorithm_runner {
    std::string_view name;
    std::size_t      min_limbs;
    std::size_t      max_limbs;
    std::size_t (*run)(std::size_t limbs);
};

// Each algorithm is probed over a range that starts at its own cutoff (below
// that the dispatcher would fall through to a smaller algorithm anyway) and
// stops at a size large enough to expose asymptotic recursive behavior.
constexpr algorithm_runner algorithms[] = {
    {"karatsuba", 40, 4000, peak_karatsuba_at},
    {"toom-cook-3", 550, 80000, peak_toom_cook_3_at},
    {"toom-cook-4", 1400, 80000, peak_toom_cook_4_at},
    {"toom-cook-6.5", 3000, 80000, peak_toom_cook_6_5_at},
    {"toom-cook-8.5", 8000, 80000, peak_toom_cook_8_5_at},
};

constexpr std::size_t sweep_limbs[] = {
    40,   48,   56,   64,   72,   80,    96,    112,   128,   144,   160,   192,   224,   256,   300,   400,
    500,  550,  600,  700,  800,  900,   1000,  1100,  1200,  1300,  1400,  1600,  2000,  2500,  3000,  3500,
    4000, 5000, 6000, 7000, 8000, 10000, 12000, 15000, 18000, 20000, 25000, 30000, 40000, 50000, 60000, 80000,
};

void run_sweep() {
    std::cout << "algorithm,limbs,peak_limbs,peak_per_s\n";
    for (const auto& algo : algorithms) {
        double      worst_ratio = 0.0;
        std::size_t worst_limbs = 0;
        std::size_t worst_peak  = 0;
        for (const std::size_t limbs : sweep_limbs) {
            if (limbs < algo.min_limbs || limbs > algo.max_limbs) {
                continue;
            }
            const std::size_t peak  = algo.run(limbs);
            const double      ratio = static_cast<double>(peak) / static_cast<double>(limbs);
            if (ratio > worst_ratio) {
                worst_ratio = ratio;
                worst_limbs = limbs;
                worst_peak  = peak;
            }
            std::cout << algo.name << ',' << limbs << ',' << peak << ',' << std::fixed << std::setprecision(4) << ratio
                      << '\n';
        }
        std::cout << "# " << algo.name << " worst: peak=" << worst_peak << " at s=" << worst_limbs
                  << " (ratio=" << std::fixed << std::setprecision(4) << worst_ratio << ")\n";
        std::cout.flush();
    }
}

} // namespace local

TEST(Multiplication, ScratchPeakBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_sweep();
    SUCCEED();
#else
    GTEST_SKIP() << "Stress benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
