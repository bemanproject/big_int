// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <numeric> // std::gcd/std::lcm: the unqualified calls below must keep resolving to ours
#include <random>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::abs;
using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::gcd;
using BEMAN_BIG_INT_NAMESPACE::in_range;
using BEMAN_BIG_INT_NAMESPACE::lcm;
using BEMAN_BIG_INT_NAMESPACE::saturating_cast;
using BEMAN_BIG_INT_NAMESPACE::to_string;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// ============================================================================
// saturating_cast
// ============================================================================

// The result type is exactly the requested type `R`.
static_assert(std::is_same_v<decltype(saturating_cast<int>(std::declval<big_int>())), int>);
static_assert(
    std::is_same_v<decltype(saturating_cast<unsigned long long>(std::declval<big_int>())), unsigned long long>);
static_assert(std::is_same_v<decltype(saturating_cast<signed char>(std::declval<const big_int&>())), signed char>);

// `R` is constrained to a signed or unsigned integer type: bool, character
// types, and floating-point types are rejected (SFINAE-friendly).
template <class R>
concept has_saturating_cast = requires(const big_int& x) { saturating_cast<R>(x); };
static_assert(has_saturating_cast<int>);
static_assert(has_saturating_cast<unsigned int>);
static_assert(has_saturating_cast<long long>);
static_assert(!has_saturating_cast<bool>);
static_assert(!has_saturating_cast<char>);
static_assert(!has_saturating_cast<double>);

// In-range values pass through unchanged, in a constant expression.
static_assert(saturating_cast<int>(big_int{0}) == 0);
static_assert(saturating_cast<int>(big_int{42}) == 42);
static_assert(saturating_cast<int>(big_int{-42}) == -42);
static_assert(saturating_cast<unsigned>(big_int{42}) == 42U);

// Boundary values are exactly representable and are returned as-is.
static_assert(saturating_cast<int>(big_int{std::numeric_limits<int>::max()}) == std::numeric_limits<int>::max());
static_assert(saturating_cast<int>(big_int{std::numeric_limits<int>::min()}) == std::numeric_limits<int>::min());

// Out-of-range values clamp to the nearest representable bound.
static_assert(saturating_cast<signed char>(big_int{1000}) == std::numeric_limits<signed char>::max());
static_assert(saturating_cast<signed char>(big_int{-1000}) == std::numeric_limits<signed char>::min());
static_assert(saturating_cast<unsigned char>(big_int{1000}) == std::numeric_limits<unsigned char>::max());
static_assert(saturating_cast<unsigned char>(big_int{-1}) == 0);

TEST(SaturatingCast, InRangeSigned) {
    EXPECT_EQ(saturating_cast<int>(big_int{0}), 0);
    EXPECT_EQ(saturating_cast<int>(big_int{1}), 1);
    EXPECT_EQ(saturating_cast<int>(big_int{-1}), -1);
    EXPECT_EQ(saturating_cast<int>(big_int{1234567}), 1234567);
    EXPECT_EQ(saturating_cast<int>(big_int{-1234567}), -1234567);
    EXPECT_EQ(saturating_cast<long long>(big_int{1000000000000LL}), 1000000000000LL);
    EXPECT_EQ(saturating_cast<long long>(big_int{-1000000000000LL}), -1000000000000LL);
}

TEST(SaturatingCast, InRangeUnsigned) {
    EXPECT_EQ(saturating_cast<unsigned>(big_int{0}), 0U);
    EXPECT_EQ(saturating_cast<unsigned>(big_int{42}), 42U);
    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{1000000000000ULL}), 1000000000000ULL);
}

TEST(SaturatingCast, ExactBoundsSigned) {
    EXPECT_EQ(saturating_cast<int>(big_int{std::numeric_limits<int>::max()}), std::numeric_limits<int>::max());
    EXPECT_EQ(saturating_cast<int>(big_int{std::numeric_limits<int>::min()}), std::numeric_limits<int>::min());
    EXPECT_EQ(saturating_cast<long long>(big_int{std::numeric_limits<long long>::max()}),
              std::numeric_limits<long long>::max());
    EXPECT_EQ(saturating_cast<long long>(big_int{std::numeric_limits<long long>::min()}),
              std::numeric_limits<long long>::min());
}

TEST(SaturatingCast, ExactBoundsUnsigned) {
    EXPECT_EQ(saturating_cast<unsigned>(big_int{std::numeric_limits<unsigned>::max()}),
              std::numeric_limits<unsigned>::max());
    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{std::numeric_limits<unsigned long long>::max()}),
              std::numeric_limits<unsigned long long>::max());
}

TEST(SaturatingCast, ClampsAboveMaxSigned) {
    // One past the maximum clamps to the maximum.
    const big_int just_over = big_int{std::numeric_limits<int>::max()} + 1;
    EXPECT_EQ(saturating_cast<int>(just_over), std::numeric_limits<int>::max());

    EXPECT_EQ(saturating_cast<signed char>(big_int{128}), std::numeric_limits<signed char>::max());
    EXPECT_EQ(saturating_cast<signed char>(big_int{1000}), std::numeric_limits<signed char>::max());
    EXPECT_EQ(saturating_cast<long long>(big_int{1} << 200), std::numeric_limits<long long>::max());
}

TEST(SaturatingCast, ClampsBelowMinSigned) {
    // One below the minimum clamps to the minimum.
    const big_int just_under = big_int{std::numeric_limits<int>::min()} - 1;
    EXPECT_EQ(saturating_cast<int>(just_under), std::numeric_limits<int>::min());

    EXPECT_EQ(saturating_cast<signed char>(big_int{-129}), std::numeric_limits<signed char>::min());
    EXPECT_EQ(saturating_cast<signed char>(big_int{-1000}), std::numeric_limits<signed char>::min());
    EXPECT_EQ(saturating_cast<long long>(-(big_int{1} << 200)), std::numeric_limits<long long>::min());
}

TEST(SaturatingCast, ClampsAboveMaxUnsigned) {
    EXPECT_EQ(saturating_cast<unsigned char>(big_int{256}), std::numeric_limits<unsigned char>::max());
    EXPECT_EQ(saturating_cast<unsigned char>(big_int{1000}), std::numeric_limits<unsigned char>::max());

    const big_int just_over = big_int{std::numeric_limits<unsigned>::max()} + 1;
    EXPECT_EQ(saturating_cast<unsigned>(just_over), std::numeric_limits<unsigned>::max());

    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{1} << 200), std::numeric_limits<unsigned long long>::max());
}

TEST(SaturatingCast, NegativeClampsToZeroUnsigned) {
    // Any negative value is below the minimum of an unsigned type (zero).
    EXPECT_EQ(saturating_cast<unsigned>(big_int{-1}), 0U);
    EXPECT_EQ(saturating_cast<unsigned char>(big_int{-1}), 0);
    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{-1}), 0ULL);
    EXPECT_EQ(saturating_cast<unsigned long long>(-(big_int{1} << 200)), 0ULL);
}

TEST(SaturatingCast, LargeHeapValueInRange) {
    // A value that lives on the heap but still fits the destination type
    // round-trips exactly rather than clamping.
    const big_int x = big_int{1} << 62;
    ASSERT_EQ(saturating_cast<long long>(x), 1LL << 62);

    const big_int big_unsigned = big_int{1} << 63;
    EXPECT_EQ(saturating_cast<unsigned long long>(big_unsigned), 1ULL << 63);
    // ...but the same value overflows a signed long long and clamps.
    EXPECT_EQ(saturating_cast<long long>(big_unsigned), std::numeric_limits<long long>::max());
}

// ============================================================================
// in_range
// ============================================================================

static_assert(!in_range<std::size_t>(big_int(-1)), "Error: not in_range");
static_assert(in_range<std::size_t>(big_int(42)), "Error: not in_range");
static_assert(in_range<int>(big_int(-1)), "Error: not in_range");
static_assert(in_range<int>(big_int(42)), "Error: not in_range");
static_assert(in_range<std::int16_t>(big_int(std::numeric_limits<std::int16_t>::min())), "Error: not in_range");
static_assert(in_range<std::int16_t>(big_int(std::numeric_limits<std::int16_t>::max())), "Error: not in_range");
static_assert(!in_range<std::int16_t>(big_int(INT32_C(37678))), "Error: not in_range");
static_assert(in_range<std::uint16_t>(big_int(UINT16_C(0))), "Error: not in_range");
static_assert(in_range<std::uint16_t>(big_int(UINT16_C(65535))), "Error: not in_range");
static_assert(!in_range<std::uint16_t>(big_int(UINT32_C(65536))), "Error: not in_range");

TEST(InRange, ValuesInRange) {
    ASSERT_EQ(in_range<std::size_t>(big_int(-1)), false);
    ASSERT_EQ(in_range<std::size_t>(big_int(42)), true);
    ASSERT_EQ(in_range<int>(big_int(-1)), true);
    ASSERT_EQ(in_range<int>(big_int(42)), true);
    ASSERT_EQ(in_range<std::int16_t>(big_int(std::numeric_limits<std::int16_t>::min())), true);
    ASSERT_EQ(in_range<std::int16_t>(big_int(std::numeric_limits<std::int16_t>::max())), true);
    ASSERT_EQ(in_range<std::int16_t>(big_int(INT32_C(37678))), false);
    ASSERT_EQ(in_range<std::uint16_t>(big_int(UINT16_C(0))), true);
    ASSERT_EQ(in_range<std::uint16_t>(big_int(UINT16_C(65535))), true);
    ASSERT_EQ(in_range<std::uint16_t>(big_int(UINT32_C(65536))), false);
}

// ============================================================================
// gcd
// ============================================================================

// The result is the common big_int type, whichever side the big_int sits on.
static_assert(std::is_same_v<decltype(gcd(std::declval<big_int>(), std::declval<big_int>())), big_int>);
static_assert(std::is_same_v<decltype(gcd(std::declval<const big_int&>(), std::declval<int>())), big_int>);
static_assert(std::is_same_v<decltype(gcd(std::declval<unsigned long long>(), std::declval<big_int&>())), big_int>);

// At least one argument must be a basic_big_int, and the other must be either the
// same specialization or a signed or unsigned integer type. Everything else is
// rejected without a hard error: bool, character types, floating-point types,
// two built-in integers (which are `std::gcd`'s business), and two unrelated
// basic_big_int specializations.
template <class M, class N>
concept has_gcd = requires(M m, N n) { gcd(m, n); };
static_assert(has_gcd<big_int, big_int>);
static_assert(has_gcd<big_int, int>);
static_assert(has_gcd<int, big_int>);
static_assert(has_gcd<big_int, unsigned long long>);
static_assert(has_gcd<const big_int&, short>);
static_assert(!has_gcd<big_int, bool>);
static_assert(!has_gcd<big_int, char>);
static_assert(!has_gcd<big_int, double>);
static_assert(!has_gcd<int, long>);
static_assert(!has_gcd<big_int, BEMAN_BIG_INT_NAMESPACE::pmr::big_int>);

// The function is usable in a constant expression, for in-place values and for
// multi-limb values whose reduction allocates.
static_assert(gcd(big_int{12}, big_int{18}) == 6);
static_assert(gcd(big_int{12}, 18) == 6);
static_assert(gcd(-12, big_int{18}) == 6);
static_assert(gcd(big_int{7}, big_int{13}) == 1);
static_assert(gcd(big_int{0}, big_int{0}) == 0);
static_assert(gcd(big_int{0}, big_int{-7}) == 7);
static_assert(gcd(big_int{1} << 200, big_int{1} << 120) == big_int{1} << 120);
static_assert(gcd(((big_int{1} << 400) - 1) * 3, ((big_int{1} << 400) - 1) * 5) == (big_int{1} << 400) - 1);

namespace adl_probe {
// `std::gcd` is visible here through <numeric>, and every basic_big_int drags
// namespace std into argument-dependent lookup through its allocator, so an
// unqualified call finds both. Ours is the more constrained overload and wins;
// this pins that down.
constexpr bool resolves_unqualified() { return gcd(BEMAN_BIG_INT_NAMESPACE::big_int{270}, 192) == 6; }
} // namespace adl_probe
static_assert(adl_probe::resolves_unqualified());

// Euclid's algorithm over the (separately tested) division operator, used as an
// independent reference for the property tests below.
[[nodiscard]] big_int euclid_gcd(big_int a, big_int b) {
    a = abs(std::move(a));
    b = abs(std::move(b));
    while (b != 0) {
        big_int r = a % b;
        a         = std::move(b);
        b         = std::move(r);
    }
    return a;
}

// A value with exactly `bits` bits, reproducible across runs.
[[nodiscard]] big_int random_value(std::mt19937_64& rng, const std::size_t bits) {
    big_int v{0};
    for (std::size_t produced = 0; produced < bits; produced += 32) {
        v = (v << 32) | big_int{static_cast<std::uint32_t>(rng())};
    }
    return (v >> (((bits + 31) / 32) * 32 - bits)) | (big_int{1} << (bits - 1));
}

TEST(Gcd, SmallValues) {
    EXPECT_EQ(gcd(big_int{12}, big_int{18}), 6);
    EXPECT_EQ(gcd(big_int{18}, big_int{12}), 6);
    EXPECT_EQ(gcd(big_int{270}, big_int{192}), 6);
    EXPECT_EQ(gcd(big_int{7}, big_int{13}), 1);
    EXPECT_EQ(gcd(big_int{1071}, big_int{462}), 21);
    EXPECT_EQ(gcd(big_int{std::numeric_limits<uint_multiprecision_t>::max()},
                  big_int{std::numeric_limits<uint_multiprecision_t>::max()}),
              std::numeric_limits<uint_multiprecision_t>::max());
}

TEST(Gcd, ZeroAndOne) {
    // gcd(x, 0) is |x|, which makes gcd(0, 0) zero.
    EXPECT_EQ(gcd(big_int{0}, big_int{0}), 0);
    EXPECT_EQ(gcd(big_int{0}, big_int{42}), 42);
    EXPECT_EQ(gcd(big_int{42}, big_int{0}), 42);
    EXPECT_EQ(gcd(big_int{-42}, big_int{0}), 42);
    EXPECT_EQ(gcd(big_int{0}, big_int{1} << 200), big_int{1} << 200);
    EXPECT_EQ(gcd(-(big_int{1} << 200), big_int{0}), big_int{1} << 200);

    // One is coprime to everything.
    EXPECT_EQ(gcd(big_int{1}, big_int{0}), 1);
    EXPECT_EQ(gcd(big_int{1}, big_int{1} << 200), 1);
    EXPECT_EQ(gcd((big_int{1} << 200) + 1, big_int{1}), 1);
}

TEST(Gcd, NegativeOperands) {
    // The result is the gcd of the magnitudes, so it is never negative.
    EXPECT_EQ(gcd(big_int{-12}, big_int{18}), 6);
    EXPECT_EQ(gcd(big_int{12}, big_int{-18}), 6);
    EXPECT_EQ(gcd(big_int{-12}, big_int{-18}), 6);
    EXPECT_EQ((gcd(big_int{-12}, big_int{-18}) <=> 0), std::strong_ordering::greater);

    const big_int large = (big_int{1} << 200) * 6;
    EXPECT_EQ(gcd(-large, large), large);
    EXPECT_EQ(gcd(-large, -large), large);
    EXPECT_EQ(gcd(-large, big_int{1} << 200), big_int{1} << 200);
}

TEST(Gcd, Symmetric) {
    std::mt19937_64 rng{7};
    for (const std::size_t bits : {31U, 64U, 130U, 400U, 1500U}) {
        const big_int a = random_value(rng, bits);
        const big_int b = random_value(rng, bits + 17);
        EXPECT_EQ(gcd(a, b), gcd(b, a)) << "bits=" << bits;
        EXPECT_EQ(gcd(a, -b), gcd(-b, a)) << "bits=" << bits;
    }
}

TEST(Gcd, MixedIntegerTypes) {
    const big_int x{462};

    EXPECT_EQ(gcd(x, 1071), 21);
    EXPECT_EQ(gcd(1071, x), 21);
    EXPECT_EQ(gcd(x, -1071), 21);
    EXPECT_EQ(gcd(x, 1071U), 21);
    EXPECT_EQ(gcd(x, static_cast<short>(1071)), 21);
    EXPECT_EQ(gcd(x, 1071LL), 21);
    EXPECT_EQ(gcd(x, 1071ULL), 21);
    EXPECT_EQ(gcd(std::numeric_limits<long long>::min(), big_int{1} << 70), big_int{1} << 63);

    // A built-in operand also works against a value far outside its own range.
    const big_int huge = (big_int{1} << 300) * 15;
    EXPECT_EQ(gcd(huge, 35), 5);
    EXPECT_EQ(gcd(35, huge), 5);
    EXPECT_EQ(gcd(huge, 0), huge);
    EXPECT_EQ(gcd(0, huge), huge);
}

TEST(Gcd, LargeCommonFactor) {
    const big_int factor = (big_int{1} << 130) + 12345;
    const big_int a      = factor * ((big_int{1} << 200) + 7);
    const big_int b      = factor * ((big_int{1} << 190) + 11);
    ASSERT_FALSE(is_inplace(a));

    const big_int g = gcd(a, b);
    EXPECT_EQ(g % factor, 0);
    EXPECT_EQ(a % g, 0);
    EXPECT_EQ(b % g, 0);
    EXPECT_EQ(g, euclid_gcd(a, b));
}

TEST(Gcd, PowersOfTwo) {
    EXPECT_EQ(gcd(big_int{1} << 300, big_int{1} << 300), big_int{1} << 300);
    EXPECT_EQ(gcd(big_int{1} << 300, big_int{1} << 64), big_int{1} << 64);
    EXPECT_EQ(gcd(big_int{1} << 64, big_int{1} << 300), big_int{1} << 64);
    EXPECT_EQ(gcd(big_int{1} << 300, big_int{1}), 1);

    // An odd operand strips every factor of two from the other one.
    EXPECT_EQ(gcd(big_int{1} << 300, big_int{3}), 1);
    EXPECT_EQ(gcd((big_int{1} << 300) * 3, big_int{12}), 12);
}

TEST(Gcd, Mersenne) {
    // gcd(2^a - 1, 2^b - 1) == 2^gcd(a, b) - 1.
    const auto mersenne = [](const unsigned e) { return (big_int{1} << e) - 1; };
    for (const unsigned a : {6U, 12U, 64U, 127U, 300U}) {
        for (const unsigned b : {4U, 9U, 65U, 128U, 210U}) {
            EXPECT_EQ(gcd(mersenne(a), mersenne(b)), mersenne(std::gcd(a, b))) << "a=" << a << " b=" << b;
        }
    }
}

TEST(Gcd, UnbalancedSizes) {
    // A wide gap between the operand sizes is closed with division before the
    // reduction proper; the result must not depend on that shortcut.
    std::mt19937_64 rng{11};
    for (const std::size_t wide_bits : {600U, 4096U}) {
        for (const std::size_t narrow_bits : {2U, 64U, 65U, 200U}) {
            const big_int wide_value   = random_value(rng, wide_bits);
            const big_int narrow_value = random_value(rng, narrow_bits);
            EXPECT_EQ(gcd(wide_value, narrow_value), euclid_gcd(wide_value, narrow_value))
                << "wide=" << wide_bits << " narrow=" << narrow_bits;
            EXPECT_EQ(gcd(narrow_value, wide_value), euclid_gcd(wide_value, narrow_value))
                << "wide=" << wide_bits << " narrow=" << narrow_bits;
        }
    }
}

TEST(Gcd, Fibonacci) {
    // Consecutive Fibonacci numbers are the worst case for a Euclidean
    // reduction: every quotient is one, so the step count is maximal.
    big_int previous{1};
    big_int current{1};
    for (int i = 0; i < 500; ++i) {
        big_int next = previous + current;
        previous     = std::move(current);
        current      = std::move(next);
    }
    EXPECT_EQ(gcd(current, previous), 1);
    EXPECT_EQ(gcd(current * 30, previous * 30), 30);
    EXPECT_EQ(gcd(current, current), current);
}

TEST(Gcd, RandomAgainstEuclid) {
    std::mt19937_64       rng{42};
    constexpr std::size_t widths[] = {1, 32, 63, 64, 65, 128, 193, 256, 512, 1024};
    for (const std::size_t a_bits : widths) {
        for (const std::size_t b_bits : widths) {
            for (int trial = 0; trial < 2; ++trial) {
                big_int a = random_value(rng, a_bits);
                big_int b = random_value(rng, b_bits);
                if ((trial & 1) != 0) {
                    a = -a;
                }
                if ((trial & 2) != 0) {
                    b = -b;
                }
                ASSERT_EQ(gcd(a, b), euclid_gcd(a, b))
                    << "a_bits=" << a_bits << " b_bits=" << b_bits << " trial=" << trial;
            }
        }
    }
}

TEST(Gcd, DividesBothOperandsExactly) {
    std::mt19937_64 rng{99};
    for (const std::size_t bits : {70U, 256U, 1000U}) {
        const big_int common = random_value(rng, bits / 2) * 6;
        const big_int a      = common * random_value(rng, bits);
        const big_int b      = common * random_value(rng, bits + 5);

        const big_int g = gcd(a, b);
        ASSERT_NE(g, 0);
        EXPECT_EQ(a % g, 0) << "bits=" << bits;
        EXPECT_EQ(b % g, 0) << "bits=" << bits;
        EXPECT_EQ(g % common, 0) << "bits=" << bits;
        // Dividing out the gcd leaves coprime cofactors.
        EXPECT_EQ(gcd(a / g, b / g), 1) << "bits=" << bits;
    }
}

TEST(Gcd, ArgumentsUnchanged) {
    // The arguments are taken by value, so lvalues are left alone.
    big_int       a      = (big_int{1} << 200) * 12;
    big_int       b      = (big_int{1} << 190) * 18;
    const big_int a_copy = a;
    const big_int b_copy = b;

    const big_int g = gcd(a, b);

    EXPECT_EQ(g, (big_int{1} << 190) * 6);
    EXPECT_EQ(a, a_copy);
    EXPECT_EQ(b, b_copy);
}

TEST(Gcd, RvalueOperands) {
    // An rvalue hands its storage to the reduction; the result is the same.
    EXPECT_EQ(gcd((big_int{1} << 200) * 12, (big_int{1} << 190) * 18), (big_int{1} << 190) * 6);
    EXPECT_EQ(gcd(big_int{1} << 200, 48), 16); // 48 == 2^4 * 3, and 2^200 has no factor of three

    big_int moved_from = (big_int{1} << 200) * 462;
    EXPECT_EQ(gcd(std::move(moved_from), 1071 * 2), 42);
}

TEST(Gcd, SmallResultFitsInPlace) {
    // A result that fits the small-object buffer is not left on the heap, even
    // when the operands were.
    const big_int a = (big_int{1} << 300) * 21;
    const big_int b = (big_int{1} << 4) * 35;
    ASSERT_FALSE(is_inplace(a));

    const big_int g = gcd(a, b);

    EXPECT_EQ(g, 7 * 16);
    EXPECT_TRUE(is_inplace(g));
}

TEST(Gcd, LargerInplaceBuffer) {
    // A specialization whose small-object buffer spans several limbs exercises
    // the reduction against in-place rather than heap storage.
    using wide_big_int = basic_big_int<256>;
    const wide_big_int a{(big_int{1} << 150) * 12};
    const wide_big_int b{(big_int{1} << 140) * 18};
    ASSERT_TRUE(is_inplace(a));

    const wide_big_int g = gcd(a, b);

    EXPECT_EQ(g, wide_big_int{(big_int{1} << 140) * 6});
    EXPECT_EQ(gcd(a, 48), wide_big_int{48});
    EXPECT_EQ(gcd(a, a), a);
    EXPECT_EQ(static_cast<std::uint64_t>(gcd(a, wide_big_int{48})), 48U);
    EXPECT_EQ(to_string(gcd(a, wide_big_int{48})), "48");

    // Two multi-limb operands whose gcd is one: the reduction runs over the
    // in-place buffers and the result shrinks back to a single limb, which must
    // convert and print as itself.
    const wide_big_int coprime_lhs{(big_int{1} << 150) + 1};
    const wide_big_int coprime_rhs{(big_int{1} << 150) + 3};
    ASSERT_TRUE(is_inplace(coprime_lhs));
    ASSERT_GT(coprime_lhs.representation().size(), 1U);

    const wide_big_int one = gcd(coprime_lhs, coprime_rhs);

    EXPECT_EQ(one, wide_big_int{1});
    EXPECT_EQ(one.representation().size(), 1U);
    EXPECT_EQ(static_cast<std::uint64_t>(one), 1U);
    EXPECT_EQ(to_string(one), "1");
}

// Counts allocations so a test can pin down which calls allocate.
class counting_resource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t allocations() const noexcept { return m_allocations; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++m_allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::size_t m_allocations{0};
};

TEST(Gcd, BorrowedOperandsAreNotCopied) {
    // The operands are forwarding references, so a call that needs no mutable copy
    // of either one -- every path where a magnitude fits a single limb -- allocates
    // nothing at all, however wide the borrowed operand is.
    using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;
    counting_resource resource;
    const pmr_big_int wide{(big_int{1} << 4096) * 21, &resource};
    const pmr_big_int narrow{35, &resource};
    ASSERT_FALSE(is_inplace(wide));

    const std::size_t before = resource.allocations();
    EXPECT_EQ(gcd(wide, narrow), pmr_big_int{7});
    EXPECT_EQ(gcd(wide, 35), pmr_big_int{7});
    EXPECT_EQ(gcd(35, wide), pmr_big_int{7});
    EXPECT_EQ(resource.allocations(), before);

    // Handing an operand over still works, and the result carries its value.
    pmr_big_int handed_over{(big_int{1} << 4096) * 21, &resource};
    EXPECT_EQ(gcd(std::move(handed_over), narrow), pmr_big_int{7});
}

TEST(Gcd, PmrOperands) {
    using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;
    std::pmr::monotonic_buffer_resource resource;
    const pmr_big_int                   a{(big_int{1} << 300) * 12, &resource};
    const pmr_big_int                   b{(big_int{1} << 290) * 18, &resource};

    const pmr_big_int g = gcd(a, b);

    EXPECT_EQ(g, pmr_big_int{(big_int{1} << 290) * 6});
    // Borrowed operands are copied, so the result's allocator comes from
    // `select_on_container_copy_construction`: the default resource here.
    EXPECT_EQ(g.get_allocator().resource(), std::pmr::get_default_resource());
    EXPECT_EQ(gcd(a, 42), pmr_big_int{6});

    // An operand handed over keeps its resource: the storage is taken over, not
    // copied, so there is nothing for the trait to guard against.
    pmr_big_int       consumed{(big_int{1} << 300) * 12, &resource};
    const pmr_big_int from_rvalue = gcd(std::move(consumed), b);
    EXPECT_EQ(from_rvalue, g);
    EXPECT_EQ(from_rvalue.get_allocator().resource(), &resource);
}

TEST(Gcd, CallableFullyQualified) {
    // Reachable both through the using-declaration above (and ADL) and when
    // named explicitly through its namespace.
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::gcd(big_int{270}, 192), 6);
}

#ifdef BEMAN_BIG_INT_HAS_BITINT
TEST(Gcd, BitPreciseIntegers) {
    // Bit-precise operands go through the same path as the standard integer types.
    using u12 = bit_uint<12>;

    EXPECT_EQ(gcd(big_int{462}, static_cast<u12>(1071)), 21);
    EXPECT_EQ(gcd(static_cast<u12>(1071), big_int{462}), 21);
    EXPECT_EQ(gcd((big_int{1} << 300) * 15, static_cast<u12>(35)), 5);

    #if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 96
    // An operand wider than a limb is spelled out into limbs before the reduction.
    using s96 = bit_int<96>;
    EXPECT_EQ(gcd((big_int{1} << 300) * 15, static_cast<s96>(-35)), 5);
    EXPECT_EQ(gcd(big_int{1} << 300, static_cast<s96>(1) << 80), big_int{1} << 80);
    #endif
}
#endif

// ============================================================================
// lcm
// ============================================================================

// The result is the common big_int type, whichever side the big_int sits on.
static_assert(std::is_same_v<decltype(lcm(std::declval<big_int>(), std::declval<big_int>())), big_int>);
static_assert(std::is_same_v<decltype(lcm(std::declval<const big_int&>(), std::declval<int>())), big_int>);
static_assert(std::is_same_v<decltype(lcm(std::declval<unsigned long long>(), std::declval<big_int&>())), big_int>);

// The admissible operand pairs are exactly `gcd`'s: at least one basic_big_int,
// and the other either the same specialization or a signed or unsigned integer
// type. Everything else drops out of overload resolution without a hard error.
template <class M, class N>
concept has_lcm = requires(M m, N n) { lcm(m, n); };
static_assert(has_lcm<big_int, big_int>);
static_assert(has_lcm<big_int, int>);
static_assert(has_lcm<int, big_int>);
static_assert(has_lcm<big_int, unsigned long long>);
static_assert(has_lcm<const big_int&, short>);
static_assert(!has_lcm<big_int, bool>);
static_assert(!has_lcm<big_int, char>);
static_assert(!has_lcm<big_int, double>);
static_assert(!has_lcm<int, long>);
static_assert(!has_lcm<big_int, BEMAN_BIG_INT_NAMESPACE::pmr::big_int>);

// The function is usable in a constant expression, for in-place values and for
// multi-limb values whose computation allocates.
static_assert(lcm(big_int{4}, big_int{6}) == 12);
static_assert(lcm(big_int{4}, 6) == 12);
static_assert(lcm(-4, big_int{6}) == 12);
static_assert(lcm(big_int{7}, big_int{13}) == 91);
static_assert(lcm(big_int{0}, big_int{0}) == 0);
static_assert(lcm(big_int{0}, big_int{-7}) == 0);
static_assert(lcm(big_int{1} << 200, big_int{1} << 120) == big_int{1} << 200);
static_assert(lcm((big_int{1} << 400) - 1, ((big_int{1} << 400) - 1) * 3) == ((big_int{1} << 400) - 1) * 3);

namespace lcm_adl_probe {
// `std::lcm` is visible here through <numeric>, and every basic_big_int drags
// namespace std into argument-dependent lookup through its allocator, so an
// unqualified call finds both. Ours is the more constrained overload and wins;
// this pins that down.
constexpr bool resolves_unqualified() { return lcm(BEMAN_BIG_INT_NAMESPACE::big_int{270}, 192) == 8640; }
} // namespace lcm_adl_probe
static_assert(lcm_adl_probe::resolves_unqualified());

// |a * b| / gcd(a, b) over the (separately tested) multiplication and division
// operators, used as an independent reference for the property tests below.
[[nodiscard]] big_int product_lcm(const big_int& a, const big_int& b) {
    if (a == 0 || b == 0) {
        return big_int{0};
    }
    return abs(a * b) / euclid_gcd(a, b);
}

TEST(Lcm, SmallValues) {
    EXPECT_EQ(lcm(big_int{4}, big_int{6}), 12);
    EXPECT_EQ(lcm(big_int{6}, big_int{4}), 12);
    EXPECT_EQ(lcm(big_int{270}, big_int{192}), 8640);
    EXPECT_EQ(lcm(big_int{7}, big_int{13}), 91);
    EXPECT_EQ(lcm(big_int{1071}, big_int{462}), 23562);
    EXPECT_EQ(lcm(big_int{12}, big_int{12}), 12);
    // A product that overflows a limb is exactly where a fixed-width lcm would
    // have to give up.
    const auto limb_max = std::numeric_limits<uint_multiprecision_t>::max();
    EXPECT_EQ(lcm(big_int{limb_max}, big_int{limb_max}), limb_max);
    EXPECT_EQ(lcm(big_int{limb_max}, big_int{2}), big_int{limb_max} * 2);
}

TEST(Lcm, ZeroAndOne) {
    // Zero is the only multiple that any operand has in common with zero.
    EXPECT_EQ(lcm(big_int{0}, big_int{0}), 0);
    EXPECT_EQ(lcm(big_int{0}, big_int{42}), 0);
    EXPECT_EQ(lcm(big_int{42}, big_int{0}), 0);
    EXPECT_EQ(lcm(big_int{-42}, big_int{0}), 0);
    EXPECT_EQ(lcm(big_int{0}, big_int{1} << 200), 0);
    EXPECT_EQ(lcm(-(big_int{1} << 200), big_int{0}), 0);

    // One is a divisor of everything, so it never enlarges the other operand.
    EXPECT_EQ(lcm(big_int{1}, big_int{0}), 0);
    EXPECT_EQ(lcm(big_int{1}, big_int{1} << 200), big_int{1} << 200);
    EXPECT_EQ(lcm((big_int{1} << 200) + 1, big_int{1}), (big_int{1} << 200) + 1);
}

TEST(Lcm, NegativeOperands) {
    // The result is the lcm of the magnitudes, so it is never negative.
    EXPECT_EQ(lcm(big_int{-4}, big_int{6}), 12);
    EXPECT_EQ(lcm(big_int{4}, big_int{-6}), 12);
    EXPECT_EQ(lcm(big_int{-4}, big_int{-6}), 12);
    EXPECT_EQ((lcm(big_int{-4}, big_int{-6}) <=> 0), std::strong_ordering::greater);

    const big_int large = (big_int{1} << 200) * 6;
    EXPECT_EQ(lcm(-large, large), large);
    EXPECT_EQ(lcm(-large, -large), large);
    EXPECT_EQ(lcm(-large, big_int{1} << 200), large);
}

TEST(Lcm, Symmetric) {
    std::mt19937_64 rng{7};
    for (const std::size_t bits : {31U, 64U, 130U, 400U, 1500U}) {
        const big_int a = random_value(rng, bits);
        const big_int b = random_value(rng, bits + 17);
        EXPECT_EQ(lcm(a, b), lcm(b, a)) << "bits=" << bits;
        EXPECT_EQ(lcm(a, -b), lcm(-b, a)) << "bits=" << bits;
    }
}

TEST(Lcm, MixedIntegerTypes) {
    const big_int x{462};

    EXPECT_EQ(lcm(x, 1071), 23562);
    EXPECT_EQ(lcm(1071, x), 23562);
    EXPECT_EQ(lcm(x, -1071), 23562);
    EXPECT_EQ(lcm(x, 1071U), 23562);
    EXPECT_EQ(lcm(x, static_cast<short>(1071)), 23562);
    EXPECT_EQ(lcm(x, 1071LL), 23562);
    EXPECT_EQ(lcm(x, 1071ULL), 23562);
    // The magnitude of the most negative value of a type is not representable in
    // that type, but it is in the result.
    EXPECT_EQ(lcm(std::numeric_limits<long long>::min(), big_int{1} << 70), big_int{1} << 70);

    // A built-in operand also works against a value far outside its own range.
    const big_int huge = (big_int{1} << 300) * 15;
    EXPECT_EQ(lcm(huge, 35), huge * 7);
    EXPECT_EQ(lcm(35, huge), huge * 7);
    EXPECT_EQ(lcm(huge, 0), 0);
    EXPECT_EQ(lcm(0, huge), 0);
}

TEST(Lcm, MultipleOfBothOperands) {
    const big_int factor = (big_int{1} << 130) + 12345;
    const big_int a      = factor * ((big_int{1} << 200) + 7);
    const big_int b      = factor * ((big_int{1} << 190) + 11);
    ASSERT_FALSE(is_inplace(a));

    const big_int l = lcm(a, b);

    EXPECT_EQ(l % a, 0);
    EXPECT_EQ(l % b, 0);
    EXPECT_EQ(l, product_lcm(a, b));
    // gcd and lcm split the product of the operands between them.
    EXPECT_EQ(gcd(a, b) * l, a * b);
}

TEST(Lcm, DivisorAndMultiplePairs) {
    // Where one operand divides the other, the larger one is already the least
    // common multiple.
    EXPECT_EQ(lcm(big_int{1} << 300, big_int{1} << 300), big_int{1} << 300);
    EXPECT_EQ(lcm(big_int{1} << 300, big_int{1} << 64), big_int{1} << 300);
    EXPECT_EQ(lcm(big_int{1} << 64, big_int{1} << 300), big_int{1} << 300);
    EXPECT_EQ(lcm(big_int{1} << 300, big_int{1}), big_int{1} << 300);

    // Coprime operands multiply out in full; an operand that divides the other
    // adds nothing to it.
    EXPECT_EQ(lcm(big_int{1} << 300, big_int{3}), (big_int{1} << 300) * 3);
    EXPECT_EQ(lcm((big_int{1} << 300) * 3, big_int{12}), (big_int{1} << 300) * 3);
}

TEST(Lcm, Mersenne) {
    // gcd(2^a - 1, 2^b - 1) == 2^gcd(a, b) - 1, so the lcm is the product with
    // that factor divided out once.
    const auto mersenne = [](const unsigned e) { return (big_int{1} << e) - 1; };
    for (const unsigned a : {6U, 12U, 64U, 127U, 300U}) {
        for (const unsigned b : {4U, 9U, 65U, 128U, 210U}) {
            EXPECT_EQ(lcm(mersenne(a), mersenne(b)), mersenne(a) * mersenne(b) / mersenne(std::gcd(a, b)))
                << "a=" << a << " b=" << b;
        }
    }
}

TEST(Lcm, UnbalancedSizes) {
    // The operand with fewer limbs is the one that gets divided; the result must
    // not depend on which side it was passed as.
    std::mt19937_64 rng{11};
    for (const std::size_t wide_bits : {600U, 4096U}) {
        for (const std::size_t narrow_bits : {2U, 64U, 65U, 200U}) {
            const big_int wide_value   = random_value(rng, wide_bits);
            const big_int narrow_value = random_value(rng, narrow_bits);
            EXPECT_EQ(lcm(wide_value, narrow_value), product_lcm(wide_value, narrow_value))
                << "wide=" << wide_bits << " narrow=" << narrow_bits;
            EXPECT_EQ(lcm(narrow_value, wide_value), product_lcm(wide_value, narrow_value))
                << "wide=" << wide_bits << " narrow=" << narrow_bits;
        }
    }
}

TEST(Lcm, Fibonacci) {
    // Consecutive Fibonacci numbers are coprime, so their lcm is their product.
    big_int previous{1};
    big_int current{1};
    for (int i = 0; i < 500; ++i) {
        big_int next = previous + current;
        previous     = std::move(current);
        current      = std::move(next);
    }
    EXPECT_EQ(lcm(current, previous), current * previous);
    EXPECT_EQ(lcm(current * 30, previous * 30), current * previous * 30);
    EXPECT_EQ(lcm(current, current), current);
}

TEST(Lcm, RandomAgainstProduct) {
    std::mt19937_64       rng{42};
    constexpr std::size_t widths[] = {1, 32, 63, 64, 65, 128, 193, 256, 512, 1024};
    for (const std::size_t a_bits : widths) {
        for (const std::size_t b_bits : widths) {
            for (int trial = 0; trial < 2; ++trial) {
                big_int a = random_value(rng, a_bits);
                big_int b = random_value(rng, b_bits);
                if ((trial & 1) != 0) {
                    a = -a;
                }
                if ((trial & 2) != 0) {
                    b = -b;
                }
                ASSERT_EQ(lcm(a, b), product_lcm(a, b))
                    << "a_bits=" << a_bits << " b_bits=" << b_bits << " trial=" << trial;
            }
        }
    }
}

TEST(Lcm, IsLeastCommonMultiple) {
    std::mt19937_64 rng{99};
    for (const std::size_t bits : {70U, 256U, 1000U}) {
        const big_int common = random_value(rng, bits / 2) * 6;
        const big_int a      = common * random_value(rng, bits);
        const big_int b      = common * random_value(rng, bits + 5);

        const big_int l = lcm(a, b);
        ASSERT_NE(l, 0);
        EXPECT_EQ(l % a, 0) << "bits=" << bits;
        EXPECT_EQ(l % b, 0) << "bits=" << bits;
        // Least: dividing out one operand leaves the other's cofactor, and those
        // two cofactors are coprime.
        EXPECT_EQ(gcd(l / a, l / b), 1) << "bits=" << bits;
        EXPECT_EQ(l * gcd(a, b), a * b) << "bits=" << bits;
    }
}

TEST(Lcm, ArgumentsUnchanged) {
    // Borrowed operands are left alone.
    big_int       a      = (big_int{1} << 200) * 12;
    big_int       b      = (big_int{1} << 190) * 18;
    const big_int a_copy = a;
    const big_int b_copy = b;

    const big_int l = lcm(a, b);

    EXPECT_EQ(l, (big_int{1} << 200) * 36);
    EXPECT_EQ(a, a_copy);
    EXPECT_EQ(b, b_copy);
}

TEST(Lcm, RvalueOperands) {
    // An rvalue hands its storage to the division or the multiplication; the
    // result is the same.
    EXPECT_EQ(lcm((big_int{1} << 200) * 12, (big_int{1} << 190) * 18), (big_int{1} << 200) * 36);
    EXPECT_EQ(lcm(big_int{1} << 200, 48), (big_int{1} << 200) * 3); // 48 == 2^4 * 3

    // 2^200 * 462 and 2142 share 42, so the cofactor the lcm picks up is 51.
    big_int moved_from = (big_int{1} << 200) * 462;
    EXPECT_EQ(lcm(std::move(moved_from), 1071 * 2), (big_int{1} << 200) * 462 * 51);
}

TEST(Lcm, SmallResultFitsInPlace) {
    // A result that fits the small-object buffer is not left on the heap, even
    // when an operand was.
    const big_int a = (big_int{1} << 300) * 21;
    const big_int b = big_int{35};
    ASSERT_FALSE(is_inplace(a));

    EXPECT_EQ(lcm(a, b), a * 5);
    // Both magnitudes fitting a limb is the scalar path: its result stays in the
    // buffer whenever it fits one limb.
    const big_int small = lcm(big_int{21}, big_int{35});
    EXPECT_EQ(small, 105);
    EXPECT_TRUE(is_inplace(small));
}

TEST(Lcm, LargerInplaceBuffer) {
    // A specialization whose small-object buffer spans several limbs exercises
    // the computation against in-place rather than heap storage.
    using wide_big_int = basic_big_int<256>;
    const wide_big_int a{(big_int{1} << 150) * 12};
    const wide_big_int b{(big_int{1} << 140) * 18};
    ASSERT_TRUE(is_inplace(a));

    const wide_big_int l = lcm(a, b);

    EXPECT_EQ(l, wide_big_int{(big_int{1} << 150) * 36});
    EXPECT_EQ(lcm(a, 48), wide_big_int{(big_int{1} << 150) * 12});
    EXPECT_EQ(lcm(a, a), a);
    EXPECT_EQ(static_cast<std::uint64_t>(lcm(wide_big_int{21}, wide_big_int{35})), 105U);
    EXPECT_EQ(to_string(lcm(wide_big_int{21}, wide_big_int{35})), "105");

    // Two multi-limb coprime operands: the product is taken in full.
    const wide_big_int coprime_lhs{(big_int{1} << 150) + 1};
    const wide_big_int coprime_rhs{(big_int{1} << 150) + 3};
    ASSERT_GT(coprime_lhs.representation().size(), 1U);

    EXPECT_EQ(lcm(coprime_lhs, coprime_rhs), wide_big_int{((big_int{1} << 150) + 1) * ((big_int{1} << 150) + 3)});
}

TEST(Lcm, SingleLimbOperandsDoNotAllocate) {
    // Both magnitudes fitting a limb is the scalar path: a result that fits a
    // limb as well is built directly, so the call allocates nothing -- and a zero
    // operand short-circuits before any of that.
    using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;
    counting_resource resource;
    const pmr_big_int a{21, &resource};
    const pmr_big_int b{35, &resource};

    const std::size_t before = resource.allocations();
    EXPECT_EQ(lcm(a, b), pmr_big_int{105});
    EXPECT_EQ(lcm(a, 35), pmr_big_int{105});
    EXPECT_EQ(lcm(35, a), pmr_big_int{105});
    EXPECT_EQ(lcm(a, 0), 0);
    EXPECT_EQ(lcm(0, a), 0);
    EXPECT_EQ(resource.allocations(), before);
}

TEST(Lcm, PmrOperands) {
    using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;
    std::pmr::monotonic_buffer_resource resource;
    const pmr_big_int                   a{(big_int{1} << 300) * 12, &resource};
    const pmr_big_int                   b{(big_int{1} << 290) * 18, &resource};

    const pmr_big_int l = lcm(a, b);

    EXPECT_EQ(l, pmr_big_int{(big_int{1} << 300) * 36});
    EXPECT_EQ(l.get_allocator().resource(), std::pmr::get_default_resource());
    EXPECT_EQ(lcm(a, 42), pmr_big_int{(big_int{1} << 300) * 84});
    // The zero result selects its allocator the same way.
    const pmr_big_int zero = lcm(a, 0);
    EXPECT_EQ(zero, 0);
    EXPECT_EQ(zero.get_allocator().resource(), std::pmr::get_default_resource());
}

TEST(Lcm, CallableFullyQualified) {
    // Reachable both through the using-declaration above (and ADL) and when
    // named explicitly through its namespace.
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::lcm(big_int{270}, 192), 8640);
}

#ifdef BEMAN_BIG_INT_HAS_BITINT
TEST(Lcm, BitPreciseIntegers) {
    // Bit-precise operands go through the same path as the standard integer types.
    using u12 = bit_uint<12>;

    EXPECT_EQ(lcm(big_int{462}, static_cast<u12>(1071)), 23562);
    EXPECT_EQ(lcm(static_cast<u12>(1071), big_int{462}), 23562);
    EXPECT_EQ(lcm((big_int{1} << 300) * 15, static_cast<u12>(35)), (big_int{1} << 300) * 105);

    #if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 96
    // An operand wider than a limb is spelled out into limbs first.
    using s96 = bit_int<96>;
    EXPECT_EQ(lcm((big_int{1} << 300) * 15, static_cast<s96>(-35)), (big_int{1} << 300) * 105);
    EXPECT_EQ(lcm(big_int{1} << 300, static_cast<s96>(1) << 80), big_int{1} << 300);
    #endif
}
#endif

#ifdef BEMAN_BIG_INT_HAS_BITINT
TEST(SaturatingCast, BitPreciseIntegers) {
    // Extended/bit-precise integers (no std::numeric_limits specialization) use
    // the width-derived bounds. Results are widened to standard types so the
    // comparison and any diagnostic printing stay on well-supported types.
    using u12 = bit_uint<12>;
    using s12 = bit_int<12>;

    EXPECT_EQ(static_cast<unsigned>(saturating_cast<u12>(big_int{4095})), 4095U); // 2^12 - 1
    EXPECT_EQ(static_cast<unsigned>(saturating_cast<u12>(big_int{4096})), 4095U); // clamps to max
    EXPECT_EQ(static_cast<unsigned>(saturating_cast<u12>(big_int{-1})), 0U);      // clamps to zero

    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{2047})), 2047);   // 2^11 - 1
    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{2048})), 2047);   // clamps to max
    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{-2048})), -2048); // -2^11
    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{-2049})), -2048); // clamps to min
}
#endif

} // namespace
