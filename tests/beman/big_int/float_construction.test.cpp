// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <climits>
#include <bit>
#include <cmath>
#include <cstdint>
#include <random>
#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

namespace {

using namespace BEMAN_BIG_INT_NAMESPACE::big_int_literals;

// ----- compile-time sanity -----

consteval bool ce_decompose_float_zero() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(0.0f);
    return !sign && mantissa == 0;
}
static_assert(ce_decompose_float_zero());

consteval bool ce_decompose_double_zero() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(0.0);
    return !sign && mantissa == 0;
}
static_assert(ce_decompose_double_zero());

consteval bool ce_decompose_long_double_zero() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(0.0l);
    return !sign && mantissa == 0;
}
static_assert(ce_decompose_long_double_zero());

consteval bool ce_decompose_float_minus_zero() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(-0.0f);
    return sign && mantissa == 0;
}
static_assert(ce_decompose_float_minus_zero());

consteval bool ce_decompose_double_minus_zero() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(-0.0);
    return sign && mantissa == 0;
}
static_assert(ce_decompose_double_minus_zero());

consteval bool ce_decompose_long_double_minus_zero() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(-0.0l);
    return sign && mantissa == 0;
}
static_assert(ce_decompose_long_double_minus_zero());

consteval bool ce_decompose_float_one() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(1.0f);
    return !sign && (mantissa >> -exponent) == 1;
}
static_assert(ce_decompose_float_one());

consteval bool ce_decompose_double_one() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(1.0);
    return !sign && (mantissa >> -exponent) == 1;
}
static_assert(ce_decompose_double_one());

consteval bool ce_decompose_long_double_one() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(1.0l);
    return !sign && (mantissa >> -exponent) == 1;
}
static_assert(ce_decompose_long_double_one());

consteval bool ce_decompose_float_minus_one() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(-1.0f);
    return sign && (mantissa >> -exponent) == 1;
}
static_assert(ce_decompose_float_minus_one());

consteval bool ce_decompose_double_minus_one() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(-1.0);
    return sign && (mantissa >> -exponent) == 1;
}
static_assert(ce_decompose_double_minus_one());

consteval bool ce_decompose_long_double_minus_one() {
    const auto [sign, exponent, mantissa] = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(-1.0l);
    return sign && (mantissa >> -exponent) == 1;
}
static_assert(ce_decompose_long_double_minus_one());

// ----- runtime tests -----

TEST(DecomposeFloat, RoundTrip) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
    std::mt19937 rng{42};

    for (std::size_t i = 0; i < 20000; ++i) {
        const auto bits = static_cast<std::uint32_t>(rng());
        const auto x    = std::bit_cast<float>(bits);
        if (!std::isfinite(x)) {
            continue;
        }

        const auto rep = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(x);
        const auto y   = BEMAN_BIG_INT_NAMESPACE::detail::compose_float(rep);

        ASSERT_EQ(std::bit_cast<std::uint32_t>(y), bits);
    }
}

TEST(DecomposeFloat, RoundTripDouble) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
    std::mt19937_64 rng{42};

    for (std::size_t i = 0; i < 20'000; ++i) {
        const auto bits = rng();
        const auto x    = std::bit_cast<double>(bits);
        if (!std::isfinite(x)) {
            continue;
        }

        const auto rep = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(x);
        const auto y   = BEMAN_BIG_INT_NAMESPACE::detail::compose_float(rep);

        ASSERT_EQ(std::bit_cast<std::uint64_t>(y), bits);
    }
}

TEST(DecomposeFloat, RoundTripLongDouble) {
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
    [[maybe_unused]] std::mt19937_64 rng{42};

    // IILE is necessary to get a dependent type and prevent errors despite if constexpr.
    [&]<class LongDouble = long double>() {
        if constexpr (sizeof(LongDouble) == 2 * sizeof(std::uint64_t)) {
            for (std::size_t i = 0; i < 20'000; ++i) {
                const wide<std::uint64_t> bits{.low_bits = rng(), .high_bits = rng()};
                const auto                x = std::bit_cast<LongDouble>(bits);
                if (!std::isfinite(x)) {
                    continue;
                }

                const auto rep = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(x);
                const auto y   = BEMAN_BIG_INT_NAMESPACE::detail::compose_float(rep);

                // We cannot do a bitwise comparison because some of the bits are padding,
                // but we can do an equality comparison with the generated long double.
                BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
                BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfloat-equal")
                ASSERT_EQ(x, y);
                BEMAN_BIG_INT_DIAGNOSTIC_POP()
            }
        } else if constexpr (sizeof(LongDouble) == sizeof(double)) {
            for (std::size_t i = 0; i < 20'000; ++i) {
                const auto bits = rng();
                const auto x    = std::bit_cast<LongDouble>(bits);
                if (!std::isfinite(x)) {
                    continue;
                }

                const auto rep = BEMAN_BIG_INT_NAMESPACE::detail::decompose_float(x);
                const auto y   = BEMAN_BIG_INT_NAMESPACE::detail::compose_float(rep);

                ASSERT_EQ(std::bit_cast<std::uint64_t>(y), bits);
            }
        }
    }();
}

TEST(FloatConstruction, DoubleZero) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0.0);
    EXPECT_EQ(x, 0);
}

TEST(FloatConstruction, DoublePositive) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(42.9);
    EXPECT_EQ(x, 42);
}

TEST(FloatConstruction, DoubleNegative) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(-42.9);
    EXPECT_EQ(x, -42);
}

TEST(FloatConstruction, DoubleFractionalLessThanOne) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0.9999);
    EXPECT_EQ(x, 0);
}

TEST(FloatConstruction, DoubleExact) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(1e15);
    EXPECT_EQ(x, 1000000000000000);
}

TEST(FloatConstruction, DoubleWithAllocator) {
    std::allocator<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t> a;
    BEMAN_BIG_INT_NAMESPACE::big_int                               x(42.7, a);
    EXPECT_EQ(x, 42);
}

TEST(FloatConstruction, FloatPositive) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(123.9f);
    EXPECT_EQ(x, 123);
}

TEST(FloatConstruction, FloatFractionalLessThanOne) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0.5f);
    EXPECT_EQ(x, 0);
}

TEST(FloatConstruction, LongDoublePositive) {
#ifdef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
    GTEST_SKIP() << "long double not supported on this platform";
#else
    BEMAN_BIG_INT_NAMESPACE::big_int x(42.9L);
    EXPECT_EQ(x, 42);
#endif
}

TEST(FloatConstruction, DoubleBoundaryFastPath) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0x1p62);
    EXPECT_EQ(x, 1ULL << 62);
}

TEST(FloatConstruction, DoubleBoundaryGeneralPath) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0x1p64);
    EXPECT_EQ(x, 0x1'0000'0000'0000'0000_n);
}

TEST(FloatConstruction, DoubleLargeMultiLimb) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0x1p65);
    EXPECT_EQ(x, 0x2'0000'0000'0000'0000_n);
}

TEST(FloatConstruction, FloatMax) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<float>::max());
    EXPECT_EQ(x, 340282346638528859811704183484516925440_n);
}

TEST(FloatConstruction, FloatMinusMax) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(-std::numeric_limits<float>::max());
    EXPECT_EQ(x, -340282346638528859811704183484516925440_n);
}

TEST(FloatConstruction, DoubleSubnormal) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(5e-324);
    EXPECT_EQ(x, 0);
}

} // namespace
