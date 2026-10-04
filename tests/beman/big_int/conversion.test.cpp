// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <gtest/gtest.h>
#include <limits>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

namespace {

using namespace BEMAN_BIG_INT_NAMESPACE::big_int_literals;

// [big.int.conv] compile-time tests

static_assert(!static_cast<bool>(BEMAN_BIG_INT_NAMESPACE::big_int{}));
static_assert(static_cast<bool>(BEMAN_BIG_INT_NAMESPACE::big_int{1}));
static_assert(static_cast<bool>(BEMAN_BIG_INT_NAMESPACE::big_int{-1}));
static_assert(static_cast<bool>(BEMAN_BIG_INT_NAMESPACE::big_int{42}));

static_assert(static_cast<int>(BEMAN_BIG_INT_NAMESPACE::big_int{42}) == 42);
static_assert(static_cast<int>(BEMAN_BIG_INT_NAMESPACE::big_int{-42}) == -42);
static_assert(static_cast<int>(BEMAN_BIG_INT_NAMESPACE::big_int{0}) == 0);
static_assert(static_cast<long long>(BEMAN_BIG_INT_NAMESPACE::big_int{1000000000000LL}) == 1000000000000LL);
static_assert(static_cast<long long>(BEMAN_BIG_INT_NAMESPACE::big_int{-1000000000000LL}) == -1000000000000LL);

static_assert(static_cast<unsigned int>(BEMAN_BIG_INT_NAMESPACE::big_int{42}) == 42U);
static_assert(static_cast<unsigned int>(BEMAN_BIG_INT_NAMESPACE::big_int{0}) == 0U);
static_assert(static_cast<unsigned int>(BEMAN_BIG_INT_NAMESPACE::big_int{-1}) ==
              std::numeric_limits<unsigned int>::max());
static_assert(static_cast<unsigned long long>(BEMAN_BIG_INT_NAMESPACE::big_int{-1}) ==
              std::numeric_limits<unsigned long long>::max());

static_assert(static_cast<int>(BEMAN_BIG_INT_NAMESPACE::big_int{0x1'0000'0042LL}) == 0x42);

// TODO(alcxpr): Use `<<` of the operator instead of IILE when implemented.
//                  Currently, only `<<=` and `>>=` are available.

// MSVC
#ifndef BEMAN_BIG_INT_MSVC
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wfloat-equal")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfloat-equal")
static_assert([] {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    return static_cast<float>(x);
}() == std::numeric_limits<float>::infinity());

static_assert([] {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    return static_cast<float>(-x);
}() == -std::numeric_limits<float>::infinity());

static_assert([] {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<double>::max());
    x <<= 1;
    return static_cast<double>(x);
}() == std::numeric_limits<double>::infinity());
BEMAN_BIG_INT_DIAGNOSTIC_POP()
#endif

TEST(Conversion, ToBool) {
    constexpr BEMAN_BIG_INT_NAMESPACE::big_int zero;
    constexpr BEMAN_BIG_INT_NAMESPACE::big_int one(1);
    static_assert(!static_cast<bool>(zero));
    static_assert(static_cast<bool>(one));
    EXPECT_FALSE(static_cast<bool>(zero));
    EXPECT_TRUE(static_cast<bool>(one));
    EXPECT_TRUE(static_cast<bool>(BEMAN_BIG_INT_NAMESPACE::big_int(-42)));
}

TEST(Conversion, ToInt) {
    constexpr BEMAN_BIG_INT_NAMESPACE::big_int x(42);
    constexpr BEMAN_BIG_INT_NAMESPACE::big_int neg(-42);
    static_assert(static_cast<int>(x) == 42);
    static_assert(static_cast<int>(neg) == -42);
    EXPECT_EQ(static_cast<int>(x), 42);
    EXPECT_EQ(static_cast<long long>(x), 42LL);
    EXPECT_EQ(static_cast<int>(neg), -42);
    EXPECT_EQ(static_cast<unsigned int>(x), 42U);
}

TEST(Conversion, ToIntTruncates) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(0x1'0000'0042LL);
    EXPECT_EQ(static_cast<int>(x), 0x42);
}

TEST(Conversion, ToDouble) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(1000000000000000LL);
    EXPECT_DOUBLE_EQ(static_cast<double>(x), 1e15);
    BEMAN_BIG_INT_NAMESPACE::big_int neg(-42);
    EXPECT_DOUBLE_EQ(static_cast<double>(neg), -42.0);
}

TEST(Conversion, ToFloat) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(123);
    EXPECT_FLOAT_EQ(static_cast<float>(x), 123.0f);
}

TEST(Conversion, ToUnsignedWrapsNegative) {
    BEMAN_BIG_INT_NAMESPACE::big_int neg(-1);
    EXPECT_EQ(static_cast<unsigned int>(neg), std::numeric_limits<unsigned int>::max());
    EXPECT_EQ(static_cast<unsigned long long>(neg), std::numeric_limits<unsigned long long>::max());
}

TEST(Conversion, ToDoubleMultiLimb) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(static_cast<double>(1ULL << 63) * 4.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(x), static_cast<double>(1ULL << 63) * 4.0);
}

TEST(Conversion, ToFloatLarge) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<float>::max());
    EXPECT_FLOAT_EQ(static_cast<float>(x), std::numeric_limits<float>::max());
}

TEST(Conversion, ZeroToAllTypes) {
    constexpr BEMAN_BIG_INT_NAMESPACE::big_int zero;
    static_assert(static_cast<int>(zero) == 0);
    static_assert(static_cast<unsigned int>(zero) == 0U);
    static_assert(static_cast<bool>(zero) == false);
    EXPECT_EQ(static_cast<double>(zero), 0.0);
    EXPECT_EQ(static_cast<float>(zero), 0.0f);
}

TEST(Conversion, ToFloatInfinity) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    EXPECT_EQ(static_cast<float>(x), std::numeric_limits<float>::infinity());
}

TEST(Conversion, ToNegativeFloatInfinity) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    x = -x;
    EXPECT_EQ(static_cast<float>(x), -std::numeric_limits<float>::infinity());
}

TEST(Conversion, ToDoubleInfinity) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(std::numeric_limits<double>::max());
    x <<= 1;
    EXPECT_EQ(static_cast<double>(x), std::numeric_limits<double>::infinity());
}

TEST(Conversion, ToFloatRoundsToInfinity) {
    // Integer = 2^128 - 1: width is exactly `max_exponent` for `float`,
    // so the value itself is finite,
    // but the top 24 bits are all ones with a sticky tail,
    // which rounds the mantissa up and bumps the exponent to `max_exponent`, producing infinity.
    // We bypass `to<float>`'s up-front overflow shortcut to exercise the
    // post-rounding overflow check inside `compose_float`.
    const BEMAN_BIG_INT_NAMESPACE::big_int x = 0xFFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF_n;
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::detail::compose_float<float>(x.representation(), false),
              std::numeric_limits<float>::infinity());
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::detail::compose_float<float>(x.representation(), true),
              -std::numeric_limits<float>::infinity());
}

TEST(Conversion, ToFloatTieWithStickyBit) {
    // Analog of ToLongDoubleThreeLimbTieWithStickyBit, but for binary32 `float`.
    if constexpr (BEMAN_BIG_INT_NAMESPACE::detail::ieee_traits<float>::width != 32) {
        GTEST_SKIP() << "Requires binary32 float (32-bit IEEE 754).";
    } else {
        // Strictly above the tie via a sticky bit far below the round bit.
        EXPECT_EQ(static_cast<float>(0x8000'0080'0000'0000'0000'0001_n), 0x1.000002p95f);
        // Exact half-ULP tie with even mantissa LSB and no sticky: tie-to-even rounds DOWN.
        EXPECT_EQ(static_cast<float>(0x8000'0080'0000'0000'0000'0000_n), 0x1p95f);
        // Exact half-ULP tie with odd mantissa LSB and no sticky: tie-to-even rounds UP.
        EXPECT_EQ(static_cast<float>(0x8000'0180'0000'0000'0000'0000_n), 0x1.000004p95f);
        // Strictly below the tie (round bit = 0): truncate, regardless of any lower bits.
        EXPECT_EQ(static_cast<float>(0x8000'007F'FFFF'FFFF'FFFF'FFFF_n), 0x1p95f);
        // Rounding propagates a carry through the mantissa and bumps the exponent.
        EXPECT_EQ(static_cast<float>(0xFFFF'FFFF'FFFF'FFFF'FFFF'FFFF_n), 0x1p96f);
        // Single-limb value that exactly fills the precision: must be representable exactly.
        EXPECT_EQ(static_cast<float>(0xFFFFFF_n), 0x1.fffffep23f);
    }
}

TEST(Conversion, ToDoubleTieWithStickyBit) {
    // Analog of ToLongDoubleThreeLimbTieWithStickyBit, but for binary64 `double`.
    if constexpr (BEMAN_BIG_INT_NAMESPACE::detail::ieee_traits<double>::width != 64) {
        GTEST_SKIP() << "Requires binary64 double (64-bit IEEE 754).";
    } else {
        // Strictly above the tie via a sticky bit far below the round bit.
        EXPECT_EQ(static_cast<double>(0x8000'0000'0000'0400'0000'0000'0000'0000'0000'0000'0000'0001_n),
                  0x1.0000000000001p191);
        // Exact half-ULP tie with even mantissa LSB and no sticky: tie-to-even rounds DOWN.
        EXPECT_EQ(static_cast<double>(0x8000'0000'0000'0400'0000'0000'0000'0000'0000'0000'0000'0000_n), 0x1p191);
        // Exact half-ULP tie with odd mantissa LSB and no sticky: tie-to-even rounds UP.
        EXPECT_EQ(static_cast<double>(0x8000'0000'0000'0C00'0000'0000'0000'0000'0000'0000'0000'0000_n),
                  0x1.0000000000002p191);
        // Strictly below the tie (round bit = 0): truncate, regardless of any lower bits.
        EXPECT_EQ(static_cast<double>(0x8000'0000'0000'03FF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF_n), 0x1p191);
        // Rounding propagates a carry through the mantissa and bumps the exponent.
        EXPECT_EQ(static_cast<double>(0xFFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF_n), 0x1p192);
        // Single-limb value that exactly fills the precision: must be representable exactly.
        EXPECT_EQ(static_cast<double>(0x1F'FFFF'FFFF'FFFF_n), 0x1.fffffffffffffp52);
    }
}

TEST(Conversion, ToLongDoubleThreeLimbTieWithStickyBit) {
    // This regression targets x87 long double (64-bit precision in an 80-bit format).
    // We build a 3-limb value (little-endian limbs):
    //   high = 2^63, mid = 2^63 (exact half ULP tie), low = 1 (sticky bit)
    // Correct final rounding is upward because the value is strictly above the tie.
    // This requires an implementation that correctly rounds the integer value
    // before forming the floating-point value.
    if constexpr (BEMAN_BIG_INT_NAMESPACE::detail::ieee_traits<long double>::width != 80) {
        GTEST_SKIP() << "Requires x87 long double (80-bit storage, 64-bit precision).";
    } else {

        // pow(2, 191) + pow(2, 127) + 1
        const long double actual =
            static_cast<long double>(0x8000'0000'0000'0000'8000'0000'0000'0000'0000'0000'0000'0001_n);
        // pow(2, 191) + pow(2, 128)
        constexpr long double expected = 0x1.0000000000000002p191L;

        EXPECT_EQ(actual, expected);

        // Exact half-ULP tie with even mantissa LSB and no sticky: tie-to-even rounds DOWN.
        // limbs: high = 2^63, mid = 2^63 (round bit only), low = 0
        // mantissa = 0x8000'0000'0000'0000 (LSB = 0), round = 1, sticky = 0  ->  no round-up.
        EXPECT_EQ(static_cast<long double>(0x8000'0000'0000'0000'8000'0000'0000'0000'0000'0000'0000'0000_n), 0x1p191L);

        // Exact half-ULP tie with odd mantissa LSB and no sticky: tie-to-even rounds UP.
        // limbs: high = 2^63 + 1, mid = 2^63, low = 0
        // mantissa = 0x8000'0000'0000'0001 (LSB = 1), round = 1, sticky = 0  ->  round up by one ULP.
        EXPECT_EQ(static_cast<long double>(0x8000'0000'0000'0001'8000'0000'0000'0000'0000'0000'0000'0000_n),
                  0x1.0000000000000004p191L);

        // Strictly below the tie (round bit = 0): truncate, regardless of any lower bits.
        // limbs: high = 2^63, mid = 0x7FFF'FFFF'FFFF'FFFF, low = all-ones
        // mantissa = 0x8000'0000'0000'0000, round = 0, sticky = 1  ->  no round-up.
        EXPECT_EQ(static_cast<long double>(0x8000'0000'0000'0000'7FFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF_n), 0x1p191L);

        // Rounding propagates a carry through the mantissa and bumps the exponent.
        // value = 2^192 - 1 (all bits set across 192 bits): rounds up to 2^192.
        EXPECT_EQ(static_cast<long double>(0xFFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF_n), 0x1p192L);

        // Single-limb value that exactly fills the precision: must be representable exactly.
        // 2^64 - 1 = 0x1.fffffffffffffffep63 in 64-bit precision long double.
        EXPECT_EQ(static_cast<long double>(0xFFFF'FFFF'FFFF'FFFF_n), 0x1.fffffffffffffffep63L);
    }

    // This is just to verify that the test itself is valid,
    // assuming that _BitInt -> long double conversions are implemented correctly.
#if LDBL_MANT_DIG == 64 && BEMAN_BIG_INT_BITINT_MAXWIDTH >= 192
    BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfloat-equal")
    BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wfloat-equal")
    constexpr auto expected_int = (static_cast<bit_uint<192>>(1) << 191) | (static_cast<bit_uint<128>>(1));
    static_assert(static_cast<long double>(expected_int) == expected_int);
    BEMAN_BIG_INT_DIAGNOSTIC_POP()
#endif
}

#ifdef BEMAN_BIG_INT_HAS_BITINT
TEST(Conversion, InplaceFastPathInt) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(42);
    BEMAN_BIG_INT_NAMESPACE::big_int neg(-42);
    ASSERT_TRUE(is_inplace(x));
    ASSERT_TRUE(is_inplace(neg));
    EXPECT_EQ(static_cast<int>(x), 42);
    EXPECT_EQ(static_cast<int>(neg), -42);
    EXPECT_EQ(static_cast<unsigned long long>(neg), std::numeric_limits<unsigned long long>::max() - 41U);
}

TEST(Conversion, InplaceFastPathFloat) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(1000000000000000LL);
    ASSERT_TRUE(is_inplace(x));
    EXPECT_DOUBLE_EQ(static_cast<double>(x), 1e15);
    EXPECT_FLOAT_EQ(static_cast<float>(x), 1e15f);
}

TEST(Conversion, InplaceFastPathNegativeFloat) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(-1000000000000000LL);
    ASSERT_TRUE(is_inplace(x));
    EXPECT_DOUBLE_EQ(static_cast<double>(x), -1e15);
    EXPECT_FLOAT_EQ(static_cast<float>(x), -1e15f);
}
#endif

} // namespace
