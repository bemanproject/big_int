// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/config.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

#include <boost/multiprecision/cpp_int.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "benchmark_testing.hpp"
#include "boost_mp_testing.hpp"

namespace local {

struct conversion_input_buffers {
    using big_int_type = BEMAN_BIG_INT_NAMESPACE::big_int;
    using cpp_int_type =
        boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;

    std::vector<big_int_type> big_int_values;
    std::vector<cpp_int_type> cpp_int_values;
};

[[nodiscard]] conversion_input_buffers make_random_conversion_input_buffers(const std::size_t count,
                                                                            const std::size_t max_bits) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp) - deterministic seed for stable benchmark inputs.
    std::mt19937_64                            rng{20260427};
    std::uniform_int_distribution<std::size_t> bit_dist(1, max_bits);
    std::bernoulli_distribution                sign_dist(0.5);

    conversion_input_buffers out;
    out.big_int_values.reserve(count);
    out.cpp_int_values.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::string hex =
            BEMAN_BIG_INT_NAMESPACE::boost_mp_testing::random_big_int(bit_dist(rng), sign_dist(rng));
        out.big_int_values.push_back(BEMAN_BIG_INT_NAMESPACE::boost_mp_testing::detail::parse_big_int(hex));
        out.cpp_int_values.push_back(BEMAN_BIG_INT_NAMESPACE::boost_mp_testing::detail::parse_cpp_int(hex));
    }

    return out;
}

[[nodiscard]] bool run_conversion_benchmarks(const std::size_t max_bits) {
    using local_stopwatch_type = BEMAN_BIG_INT_NAMESPACE::benchmark_testing::stopwatch;

    constexpr std::uint64_t hash_offset_basis = 1469598103934665603;
    constexpr std::uint64_t hash_prime        = 1099511628211;

    constexpr std::size_t buffer_size = 1'000'000;
    constexpr std::size_t passes      = 5;

    const conversion_input_buffers inputs       = make_random_conversion_input_buffers(buffer_size, max_bits);
    std::uint64_t                  big_int_sink = hash_offset_basis;
    std::uint64_t                  cpp_int_sink = hash_offset_basis;

    const auto bench_big_int = [&] {
        for (std::size_t pass = 0; pass < passes; ++pass) {
            std::uint64_t big_int_pass_sink = 0;
            for (const auto& value : inputs.big_int_values) {
                big_int_pass_sink ^= std::bit_cast<std::uint64_t>(static_cast<double>(value));
            }
            big_int_sink ^= (big_int_pass_sink + static_cast<std::uint64_t>(pass));
            big_int_sink *= hash_prime;
        }
    };

    const auto bench_cpp_int = [&] {
        for (std::size_t pass = 0; pass < passes; ++pass) {
            std::uint64_t cpp_int_pass_sink = 0;
            for (const auto& value : inputs.cpp_int_values) {
                cpp_int_pass_sink ^= std::bit_cast<std::uint64_t>(static_cast<double>(value));
            }
            cpp_int_sink ^= (cpp_int_pass_sink + static_cast<std::uint64_t>(pass));
            cpp_int_sink *= hash_prime;
        }
    };

    std::cout << "stopwatch big_int->double: " << local_stopwatch_type::measure_time(bench_big_int) << std::endl;
    BEMAN_BIG_INT_ASSERT(big_int_sink != hash_offset_basis);

    std::cout << "stopwatch cpp_int->double: " << local_stopwatch_type::measure_time(bench_cpp_int) << std::endl;
    BEMAN_BIG_INT_ASSERT(cpp_int_sink != hash_offset_basis);

    // We expect bitwise identical results because both `big_int` and `cpp_int` use roundTiesToEven.
    return big_int_sink == cpp_int_sink;
}

} // namespace local

// The 64-bit benchmark is mostly just measuring whether there exists a fast path
// to delegate to the `uint64_t->double` conversions.
TEST(Conversion, ConversionToDoubleBench64) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(local::run_conversion_benchmarks(64));
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}

// The 128-bit benchmark is mostly just measuring whether there exists a fast path
// to delegate to the `uint128_t->double` conversions.
TEST(Conversion, ConversionToDoubleBench128) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(local::run_conversion_benchmarks(128));
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}

// At 256 bits, the "custom implementation" kicks in for both types.
TEST(Conversion, ConversionToDoubleBench256) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(local::run_conversion_benchmarks(256));
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}

TEST(Conversion, ConversionToDoubleBench512) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(local::run_conversion_benchmarks(512));
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}

TEST(Conversion, ConversionToDoubleBench1024) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(local::run_conversion_benchmarks(1024));
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}

TEST(Conversion, ConversionToDoubleBench2048) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(local::run_conversion_benchmarks(2048));
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}

BEMAN_BIG_INT_DIAGNOSTIC_POP()
