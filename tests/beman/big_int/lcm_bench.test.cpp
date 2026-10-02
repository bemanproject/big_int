// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Benchmarks for lcm against boost::multiprecision, which composes it the same
// way (one gcd, one exact division, one multiplication). These are the numbers
// quoted in doc numeric.adoc.
//
// Disabled by default; define BEMAN_BIG_INT_RUN_BENCHMARKS to run.
// Output is CSV on stdout: case,bits,big_int_us,cpp_int_us,ratio.

#include <boost/multiprecision/cpp_int.hpp>

#include <beman/big_int.hpp>

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
// timings measure the same computation.
struct operand_pair {
    big_int_type big_lhs, big_rhs;
    cpp_int_type cpp_lhs, cpp_rhs;
};

[[nodiscard]] std::vector<operand_pair>
make_pairs(std::mt19937_64& rng, const std::size_t lhs_bits, const std::size_t rhs_bits, const int count) {
    const auto fill = [&rng](big_int_type& big, cpp_int_type& cpp, const std::size_t bits) {
        for (std::size_t produced = 0; produced < bits; produced += 64) {
            const std::uint64_t limb = rng();
            big                      = (big << 64) | big_int_type{limb};
            cpp                      = (cpp << 64) | cpp_int_type{limb};
        }
    };

    std::vector<operand_pair> pairs;
    pairs.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        operand_pair pair{};
        fill(pair.big_lhs, pair.cpp_lhs, lhs_bits);
        fill(pair.big_rhs, pair.cpp_rhs, rhs_bits);
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

// Times `lcm` over `pairs` in both libraries and reports the pair of timings.
void run_case(const char* const name, const std::vector<operand_pair>& pairs, const std::size_t bits, const int reps) {
    // Summing the results' limb counts keeps them from being optimized away
    // without a cast: the limb type is `unsigned long long` on some targets and
    // `std::uint64_t` on others, so a cast to a fixed-width type is a useless
    // cast on one of them, which the test warning set rejects.
    std::size_t  alive  = 0;
    const double ours   = stopwatch::measure_time([&] {
        for (int i = 0; i < reps; ++i) {
            const operand_pair& pair = pairs[static_cast<std::size_t>(i) % pairs.size()];
            alive += lcm(pair.big_lhs, pair.big_rhs).representation().size();
        }
    });
    const double theirs = stopwatch::measure_time([&] {
        for (int i = 0; i < reps; ++i) {
            const operand_pair& pair = pairs[static_cast<std::size_t>(i) % pairs.size()];
            alive += lcm(pair.cpp_lhs, pair.cpp_rhs).backend().size();
        }
    });
    emit(name, bits, ours * 1e6 / reps, theirs * 1e6 / reps);
    std::cout.flush();
    EXPECT_NE(alive, 0U);
}

void run_all() {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp) - deterministic seed is intentional.
    std::mt19937_64 rng{20260818};
    std::cout << "case,bits,big_int_us,cpp_int_us,ratio\n";

    // Operands of equal size: random pairs are almost always coprime, so this is
    // the gcd plus a full-width multiplication.
    for (const std::size_t bits : {std::size_t{64},
                                   std::size_t{128},
                                   std::size_t{256},
                                   std::size_t{512},
                                   std::size_t{1024},
                                   std::size_t{2048},
                                   std::size_t{4096}}) {
        const int reps = bits <= 512 ? 2000 : (bits <= 2048 ? 200 : 50);
        run_case("balanced", make_pairs(rng, bits, bits, 32), bits, reps);
    }

    // A wide operand against a single-limb one: the narrow operand is the one
    // divided, leaving a single-limb multiplication over the wide one.
    run_case("wide_vs_single_limb", make_pairs(rng, 4096, 64, 32), 4096, 2000);

    // A planted common factor, so the division is not by one.
    auto common = make_pairs(rng, 1024, 1024, 32);
    for (operand_pair& pair : common) {
        pair.big_lhs *= 6;
        pair.cpp_lhs *= 6;
        pair.big_rhs *= 6;
        pair.cpp_rhs *= 6;
    }
    run_case("common_factor", common, 1024, 200);
}

} // namespace local

TEST(Lcm, Bench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_all();
    SUCCEED();
#else
    GTEST_SKIP() << "Benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
