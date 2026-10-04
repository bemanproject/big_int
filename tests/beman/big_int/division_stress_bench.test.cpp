// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Stress benchmark for tuning burnikel_ziegler_cutoff and
// burnikel_ziegler_offset. Sweeps the schoolbook kernel (divide_unsigned)
// against the divide-and-conquer driver (divide_burnikel_ziegler, called
// directly so the dispatch gates do not mask the crossover) over balanced,
// quotient-length, and unbalanced shapes. Each timing includes the kernel's
// production scratch setup. Crossovers are sawtoothed (recursion-depth
// boundaries), so points report the MIN over several repetitions.
//
// Disabled by default. To run:
//   cmake --preset appleclang-release (build dir carries
//     CMAKE_CXX_FLAGS=-DBEMAN_BIG_INT_RUN_BENCHMARKS=1)
//   cmake --build build/appleclang-release
//   ./build/appleclang-release/tests/beman/big_int/beman.big_int.tests.division_stress_bench
//
// Output is CSV on stdout: algorithm,dividend_limbs,divisor_limbs,iters,ns_per_div.

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>

#include <gtest/gtest.h>

#include "benchmark_testing.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace local {

using uint_t           = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using stopwatch        = ::BEMAN_BIG_INT_NAMESPACE::benchmark_testing::stopwatch;
using std_allocator    = std::allocator<uint_t>;
using scratch_for_test = ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<std_allocator>;

inline constexpr unsigned reps_per_point = 5;

// Distinct random operand pairs per data point. Quotient-estimate quality in
// the schoolbook kernel varies strongly with the divisor's top limbs, so a
// single pair can misplace a crossover by more than the timing noise.
inline constexpr unsigned samples_per_point = 3;

// NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
static std::mt19937_64 rng{0xD17ULL};

inline void fill_random(const std::span<uint_t> dest) {
    std::uniform_int_distribution<uint_t> dist;
    for (auto& x : dest) {
        x = dist(rng);
    }
    if (!dest.empty() && dest.back() == 0) {
        dest.back() = 1;
    }
}

// Scale the inner iteration count so one repetition lands in the low
// milliseconds regardless of operand size.
[[nodiscard]] inline unsigned iters_for(const std::size_t m, const std::size_t s) {
    const std::size_t cost = m * s;
    return static_cast<unsigned>(std::clamp<std::size_t>(20'000'000 / std::max<std::size_t>(cost, 1), 1, 2000));
}

// Per operand pair: minimum over reps_per_point repetitions of `iters` calls
// (strips scheduler/timing noise). Across pairs: the MEDIAN (the workload
// variation is real signal, and a per-algorithm min would report each
// algorithm's luckiest divisor).
template <class Algo>
double measure_division(const std::size_t m, const std::size_t s, Algo algo) {
    const unsigned iters = iters_for(m, s);

    std::array<double, samples_per_point> sample_ns{};
    for (unsigned sample = 0; sample < samples_per_point; ++sample) {
        std::vector<uint_t> dividend(m);
        std::vector<uint_t> divisor(s);
        fill_random(dividend);
        fill_random(divisor);

        std::vector<uint_t> quotient(m - s + 1, 0);
        std::vector<uint_t> remainder(m + 1, 0);

        const auto a_view = std::span<const uint_t>{dividend};
        const auto b_view = std::span<const uint_t>{divisor};
        const auto q_view = std::span<uint_t>{quotient};
        const auto r_view = std::span<uint_t>{remainder};

        algo(q_view, r_view, a_view, b_view); // warmup

        double best = 1.0e300;
        for (unsigned rep = 0; rep < reps_per_point; ++rep) {
            const stopwatch sw{};
            for (unsigned i = 0; i < iters; ++i) {
                algo(q_view, r_view, a_view, b_view);
            }
            best = std::min(best, stopwatch::elapsed_time<double>(sw) / iters);
        }
        sample_ns[sample] = best;
    }

    std::ranges::sort(sample_ns);
    return sample_ns[samples_per_point / 2] * 1.0e9;
}

double run_schoolbook_at(const std::size_t m, const std::size_t s) {
    return measure_division(m, s, [](const auto q, const auto r, const auto a, const auto b) {
        std_allocator    alloc;
        scratch_for_test scratch(::BEMAN_BIG_INT_NAMESPACE::detail::divide_unsigned_storage_size(a.size(), b.size()),
                                 alloc);
        ::BEMAN_BIG_INT_NAMESPACE::detail::divide_unsigned(q, r, a, b, scratch);
    });
}

double run_burnikel_ziegler_at(const std::size_t m, const std::size_t s) {
    return measure_division(m, s, [](const auto q, const auto r, const auto a, const auto b) {
        std_allocator alloc;
        ::BEMAN_BIG_INT_NAMESPACE::detail::divide_burnikel_ziegler(q, r, a, b, alloc);
    });
}

double run_barrett_at(const std::size_t m, const std::size_t s) {
    return measure_division(m, s, [](const auto q, const auto r, const auto a, const auto b) {
        std_allocator alloc;
        ::BEMAN_BIG_INT_NAMESPACE::detail::divide_barrett(q, r, a, b, alloc);
    });
}

void emit(const char* algorithm, const std::size_t m, const std::size_t s, const double ns) {
    std::cout << algorithm << ',' << m << ',' << s << ',' << local::iters_for(m, s) << ',' << std::fixed
              << std::setprecision(1) << ns << '\n';
}

void run_sweep() {
    std::cout << "algorithm,dividend_limbs,divisor_limbs,iters,ns_per_div\n";

    // Balanced 2n / n: the canonical shape for the cutoffs. The top sizes
    // bracket the Barrett balanced gate.
    constexpr std::size_t balanced[] = {16,   24,   32,   40,    48,    64,    80,    96,     128,
                                        192,  256,  384,  512,   768,   1024,  1536,  2048,   3072,
                                        4096, 6144, 8192, 12288, 16384, 32768, 65536, 131072, 262144};
    for (const std::size_t n : balanced) {
        if (n <= 4096) {
            emit("schoolbook", 2 * n, n, run_schoolbook_at(2 * n, n));
        }
        emit("burnikel-ziegler", 2 * n, n, run_burnikel_ziegler_at(2 * n, n));
        if (n >= 256) {
            emit("barrett", 2 * n, n, run_barrett_at(2 * n, n));
        }
        std::cout.flush();
    }

    // Quotient-length sweep at a fixed 100-limb divisor: tunes the offset gate.
    constexpr std::size_t extras[] = {4, 8, 12, 16, 20, 24, 32, 48, 64, 96, 128};
    for (const std::size_t extra : extras) {
        emit("schoolbook", 100 + extra, 100, run_schoolbook_at(100 + extra, 100));
        emit("burnikel-ziegler", 100 + extra, 100, run_burnikel_ziegler_at(100 + extra, 100));
        std::cout.flush();
    }

    // Unbalanced: long block marches at a fixed dividend.
    constexpr std::size_t divisors[] = {64, 128, 256, 512, 1024, 2048};
    for (const std::size_t s : divisors) {
        emit("schoolbook", 4096, s, run_schoolbook_at(4096, s));
        emit("burnikel-ziegler", 4096, s, run_burnikel_ziegler_at(4096, s));
        if (s >= 256) {
            emit("barrett", 4096, s, run_barrett_at(4096, s));
        }
        std::cout.flush();
    }

    // Huge unbalanced marches where the reciprocal amortizes over many blocks.
    constexpr std::size_t huge_divisors[] = {512, 1024, 2048, 4096};
    for (const std::size_t s : huge_divisors) {
        emit("burnikel-ziegler", 16384, s, run_burnikel_ziegler_at(16384, s));
        emit("barrett", 16384, s, run_barrett_at(16384, s));
        std::cout.flush();
    }
}

} // namespace local

TEST(Division, DivisionStressBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_sweep();
    SUCCEED();
#else
    GTEST_SKIP() << "Stress benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
