// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Benchmarks for gcd: the Lehmer reduction against boost::multiprecision's
// (which uses the same double-digit algorithm), plus the allocation-free mixed
// and single-limb paths. These are the numbers quoted in doc numeric.adoc.
//
// Disabled by default; define BEMAN_BIG_INT_RUN_BENCHMARKS to run.
// Output is CSV on stdout: case,bits,big_int_us,cpp_int_us,ratio.

#include <boost/multiprecision/cpp_int.hpp>

#include <beman/big_int/big_int.hpp>

#include <gtest/gtest.h>

#include "benchmark_testing.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace local {

using big_int_type = ::BEMAN_BIG_INT_NAMESPACE::big_int;
using cpp_int_type =
    ::boost::multiprecision::number<::boost::multiprecision::cpp_int_backend<>, ::boost::multiprecision::et_off>;
using stopwatch = ::BEMAN_BIG_INT_NAMESPACE::benchmark_testing::stopwatch;

// One operand pair in both libraries, built from the same limbs so the two
// timings measure the same reduction.
struct operand_pair {
    big_int_type big_lhs, big_rhs;
    cpp_int_type cpp_lhs, cpp_rhs;
};

[[nodiscard]] std::vector<operand_pair> make_pairs(std::mt19937_64& rng, const std::size_t bits, const int count) {
    std::vector<operand_pair> pairs;
    pairs.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        operand_pair pair{};
        for (std::size_t produced = 0; produced < bits; produced += 64) {
            const std::uint64_t lhs_limb = rng();
            const std::uint64_t rhs_limb = rng();
            pair.big_lhs                 = (pair.big_lhs << 64) | big_int_type{lhs_limb};
            pair.big_rhs                 = (pair.big_rhs << 64) | big_int_type{rhs_limb};
            pair.cpp_lhs                 = (pair.cpp_lhs << 64) | cpp_int_type{lhs_limb};
            pair.cpp_rhs                 = (pair.cpp_rhs << 64) | cpp_int_type{rhs_limb};
        }
        pairs.push_back(std::move(pair));
    }
    return pairs;
}

void emit(const char* const name, const std::size_t bits, const double ours, const double theirs) {
    // A clock too coarse to time the reference leaves the ratio column at zero
    // rather than dividing by it.
    const double ratio = theirs > 0.0 ? ours / theirs : 0.0;
    std::cout << name << ',' << bits << ',' << std::fixed << std::setprecision(4) << ours << ',' << theirs << ','
              << ratio << '\n';
}

void run_all() {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp) - deterministic seed is intentional.
    std::mt19937_64 rng{20260817};
    std::cout << "case,bits,big_int_us,cpp_int_us,ratio\n";

    for (const std::size_t bits : {std::size_t{64},
                                   std::size_t{128},
                                   std::size_t{256},
                                   std::size_t{512},
                                   std::size_t{1024},
                                   std::size_t{2048},
                                   std::size_t{4096}}) {
        const int  reps  = bits <= 512 ? 2000 : (bits <= 2048 ? 200 : 50);
        const auto pairs = make_pairs(rng, bits, 32);

        // Summing the results' limb counts keeps them from being optimized away
        // without a cast: the limb type is `unsigned long long` on some targets and
        // `std::uint64_t` on others, so a cast to a fixed-width type is a useless
        // cast on one of them, which the test warning set rejects.
        std::size_t  alive  = 0;
        const double ours   = stopwatch::measure_time([&] {
            for (int i = 0; i < reps; ++i) {
                const operand_pair& pair = pairs[static_cast<std::size_t>(i) % pairs.size()];
                alive += gcd(pair.big_lhs, pair.big_rhs).representation().size();
            }
        });
        const double theirs = stopwatch::measure_time([&] {
            for (int i = 0; i < reps; ++i) {
                const operand_pair& pair = pairs[static_cast<std::size_t>(i) % pairs.size()];
                alive += gcd(pair.cpp_lhs, pair.cpp_rhs).backend().size();
            }
        });
        emit("balanced", bits, ours * 1e6 / reps, theirs * 1e6 / reps);
        std::cout.flush();
        EXPECT_NE(alive, 0U);
    }

    // The paths that never allocate: a single-limb operand against a wide one,
    // and a built-in integer operand.
    const auto   wide      = make_pairs(rng, 4096, 1).front();
    std::size_t  alive     = 0;
    const int    reps      = 2000;
    const double mixed_big = stopwatch::measure_time([&] {
        for (int i = 0; i < reps; ++i) {
            alive += gcd(wide.big_lhs, big_int_type{0x9e3779b97f4a7c15ULL}).representation().size();
        }
    });
    const double mixed_cpp = stopwatch::measure_time([&] {
        for (int i = 0; i < reps; ++i) {
            alive += gcd(wide.cpp_lhs, cpp_int_type{0x9e3779b97f4a7c15ULL}).backend().size();
        }
    });
    emit("wide_vs_single_limb", 4096, mixed_big * 1e6 / reps, mixed_cpp * 1e6 / reps);

    const double builtin_big = stopwatch::measure_time([&] {
        for (int i = 0; i < reps; ++i) {
            alive += gcd(wide.big_lhs, 0x9e3779b97f4a7c15ULL).representation().size();
        }
    });
    const double builtin_cpp = stopwatch::measure_time([&] {
        for (int i = 0; i < reps; ++i) {
            alive += gcd(wide.cpp_lhs, 0x9e3779b97f4a7c15ULL).backend().size();
        }
    });
    emit("wide_vs_builtin", 4096, builtin_big * 1e6 / reps, builtin_cpp * 1e6 / reps);

    const auto   small     = make_pairs(rng, 64, 32);
    const int    small_rep = 20000;
    const double small_big = stopwatch::measure_time([&] {
        for (int i = 0; i < small_rep; ++i) {
            const operand_pair& pair = small[static_cast<std::size_t>(i) % small.size()];
            alive += gcd(pair.big_lhs, pair.big_rhs).representation().size();
        }
    });
    const double small_cpp = stopwatch::measure_time([&] {
        for (int i = 0; i < small_rep; ++i) {
            const operand_pair& pair = small[static_cast<std::size_t>(i) % small.size()];
            alive += gcd(pair.cpp_lhs, pair.cpp_rhs).backend().size();
        }
    });
    emit("single_limb", 64, small_big * 1e6 / small_rep, small_cpp * 1e6 / small_rep);
    std::cout.flush();
    EXPECT_NE(alive, 0U);
}

} // namespace local

TEST(Gcd, Bench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_all();
    SUCCEED();
#else
    GTEST_SKIP() << "Benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
