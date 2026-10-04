// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::div_rem_to_zero;
using BEMAN_BIG_INT_NAMESPACE::div_result;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// ----- compile-time sanity -----

[[nodiscard]] consteval bool check_div_rem(const big_int& x, const big_int& y) {
    return div_rem_to_zero(x, y) == div_result{x / y, x % y};
}

static_assert((big_int{0} / big_int{1}) == 0);
static_assert((big_int{0} % big_int{1}) == 0);
static_assert(check_div_rem(0, 1));

static_assert((big_int{21} / big_int{7}) == 3);
static_assert((big_int{21} % big_int{7}) == 0);
static_assert(check_div_rem(21, 7));

static_assert((big_int{42} / big_int{6}) == 7);
static_assert((big_int{42} % big_int{6}) == 0);
static_assert(check_div_rem(42, 6));

static_assert((big_int{7} / big_int{3}) == 2);
static_assert((big_int{7} % big_int{3}) == 1);
static_assert(check_div_rem(7, 3));

static_assert((big_int{-7} / big_int{3}) == -2);
static_assert((big_int{-7} % big_int{3}) == -1);
static_assert(check_div_rem(-7, 3));

static_assert((big_int{7} / big_int{-3}) == -2);
static_assert((big_int{7} % big_int{-3}) == 1);
static_assert(check_div_rem(7, -3));

static_assert((big_int{-7} / big_int{-3}) == 2);
static_assert((big_int{-7} % big_int{-3}) == -1);
static_assert(check_div_rem(-7, -3));

static_assert((big_int{3} / big_int{7}) == 0);
static_assert((big_int{3} % big_int{7}) == 3);
static_assert(check_div_rem(3, 7));

static_assert((big_int{42} / big_int{42}) == 1);
static_assert((big_int{42} % big_int{42}) == 0);
static_assert(check_div_rem(42, 42));

// ----- runtime tests -----

TEST(Division, SmallPositivePositive) {
    EXPECT_EQ(big_int{42} / big_int{7}, 6);
    EXPECT_EQ(big_int{100} / big_int{3}, 33);
    EXPECT_EQ(big_int{1} / big_int{1}, 1);
}

TEST(Division, ExactMultiples) {
    EXPECT_EQ(big_int{0} / big_int{5}, 0);
    EXPECT_EQ(big_int{10} / big_int{2}, 5);
    EXPECT_EQ(big_int{999} / big_int{3}, 333);
}

TEST(Division, TruncationTowardZero) {
    // C++ truncated division: quotient rounds toward zero.
    EXPECT_EQ(big_int{7} / big_int{3}, 2);
    EXPECT_EQ(big_int{-7} / big_int{3}, -2);
    EXPECT_EQ(big_int{7} / big_int{-3}, -2);
    EXPECT_EQ(big_int{-7} / big_int{-3}, 2);
}

TEST(Division, MixedSigns) {
    EXPECT_EQ(big_int{-42} / big_int{7}, -6);
    EXPECT_EQ(big_int{42} / big_int{-7}, -6);
    EXPECT_EQ(big_int{-42} / big_int{-7}, 6);
}

TEST(Division, DividendLessThanDivisor) {
    EXPECT_EQ(big_int{3} / big_int{7}, 0);
    EXPECT_EQ(big_int{-3} / big_int{7}, 0);
    EXPECT_EQ(big_int{3} / big_int{-7}, 0);

    // No negative zero from sign combination.
    const big_int r = big_int{-3} / big_int{7};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Division, ZeroDividend) {
    EXPECT_EQ(big_int{0} / big_int{5}, 0);
    EXPECT_EQ(big_int{0} / big_int{-5}, 0);
    EXPECT_EQ(big_int{0} / big_int{9999999}, 0);
}

TEST(Division, DividendEqualsDivisor) {
    EXPECT_EQ(big_int{42} / big_int{42}, 1);
    EXPECT_EQ(big_int{-42} / big_int{42}, -1);
    EXPECT_EQ(big_int{42} / big_int{-42}, -1);
    EXPECT_EQ(big_int{-42} / big_int{-42}, 1);
}

TEST(Division, IdentityByOne) {
    EXPECT_EQ(big_int{42} / big_int{1}, 42);
    EXPECT_EQ(big_int{-42} / big_int{1}, -42);
    EXPECT_EQ(big_int{42} / big_int{-1}, -42);
    EXPECT_EQ(big_int{-42} / big_int{-1}, 42);
}

TEST(Division, MultiLimbBySingleLimb) {
    // (2^64) / 2 == 2^63
    const big_int two_64   = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r        = two_64 / big_int{2};
    const big_int expected = big_int{static_cast<std::uint64_t>(1) << 63};
    EXPECT_EQ(r, expected);
}

TEST(Division, MultiLimbByMultiLimb) {
    // ((2^64) + 5) / ((2^64) + 1) == 1, remainder 4.
    const big_int two_64_plus_5 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{6};
    const big_int two_64_plus_1 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{2};
    EXPECT_EQ(two_64_plus_5 / two_64_plus_1, 1);
}

TEST(Division, DivideMultiLimbExact) {
    // (a * b) / b == a for randomly constructed big operands.
    const big_int a       = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{123};
    const big_int b       = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{7};
    const big_int product = a * b;
    EXPECT_EQ(product / b, a);
    EXPECT_EQ(product / a, b);
}

TEST(Division, DivSmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            if (y == 0) {
                continue;
            }
            const big_int bx{x};
            const big_int by{y};

            /* move_move */ EXPECT_EQ(big_int{x} / big_int{y}, x / y);
            /* move_copy */ EXPECT_EQ(big_int{x} / by, x / y);
            /* copy_move */ EXPECT_EQ(bx / big_int{y}, x / y);
            /* copy_copy */ EXPECT_EQ(bx / by, x / y);
            /* move_int  */ EXPECT_EQ(big_int{x} / y, x / y);
            /* int_move  */ EXPECT_EQ(x / big_int{y}, x / y);
            /* copy_int  */ EXPECT_EQ(bx / y, x / y);
            /* int_copy  */ EXPECT_EQ(x / by, x / y);
        }
    }
}

TEST(Division, DivRemToZeroSmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            if (y == 0) {
                continue;
            }
            const big_int bx{x};
            const big_int by{y};

            const div_result<big_int> expected{x / y, x % y};
            /* move_move */ EXPECT_EQ(div_rem_to_zero(big_int{x}, big_int{y}), expected);
            /* move_copy */ EXPECT_EQ(div_rem_to_zero(big_int{x}, by), expected);
            /* copy_move */ EXPECT_EQ(div_rem_to_zero(bx, big_int{y}), expected);
            /* copy_copy */ EXPECT_EQ(div_rem_to_zero(bx, by), expected);
            /* move_int  */ EXPECT_EQ(div_rem_to_zero(big_int{x}, y), expected);
            /* int_move  */ EXPECT_EQ(div_rem_to_zero(x, big_int{y}), expected);
            /* copy_int  */ EXPECT_EQ(div_rem_to_zero(bx, y), expected);
            /* int_copy  */ EXPECT_EQ(div_rem_to_zero(x, by), expected);
        }
    }
}

TEST(Division, MultiplyAndDivideRoundTrip) {
    // (x * y) / y == x for a sequence of pairs.
    for (int i = 1; i < 50; ++i) {
        for (int j = 1; j < 50; ++j) {
            const big_int x{i * 1'000'000'007LL};
            const big_int y{j * 99'991LL};
            EXPECT_EQ((x * y) / y, x);
        }
    }
}

TEST(Division, LargeShiftBasis) {
    // (2^200) / (2^100) == 2^100
    const big_int dividend = big_int{1} << 200;
    const big_int divisor  = big_int{1} << 100;
    const big_int expected = big_int{1} << 100;
    EXPECT_EQ(dividend / divisor, expected);
}

TEST(Division, CompoundAssignmentBigInt) {
    big_int a{42};
    a /= big_int{7};
    EXPECT_EQ(a, 6);

    big_int b{-42};
    b /= big_int{7};
    EXPECT_EQ(b, -6);
}

TEST(Division, CompoundAssignmentPrimitive) {
    big_int a{100};
    a /= 7;
    EXPECT_EQ(a, 14);

    big_int b{-100};
    b /= 3;
    EXPECT_EQ(b, -33);

    big_int c{100};
    c /= -3;
    EXPECT_EQ(c, -33);
}

TEST(Division, CompoundAssignmentMultiLimb) {
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    a /= big_int{4};
    const big_int expected = big_int{static_cast<std::uint64_t>(1) << 62};
    EXPECT_EQ(a, expected);
}

TEST(Division, CompoundAssignmentMultiLimbDivisorBigInt) {
    // Multi-limb dividend, multi-limb basic_big_int divisor: exercises the
    // generic divmod_into slow path in operator/=.
    const big_int divisor  = (big_int{1} << 100) + big_int{12345};
    const big_int dividend = (big_int{1} << 250) + big_int{67890};

    big_int       a        = dividend;
    const big_int expected = dividend / divisor;
    a /= divisor;
    EXPECT_EQ(a, expected);

    // Negative dividend, positive multi-limb divisor.
    big_int       b     = -dividend;
    const big_int b_exp = (-dividend) / divisor;
    b /= divisor;
    EXPECT_EQ(b, b_exp);

    // Positive dividend, negative multi-limb divisor.
    big_int       c     = dividend;
    const big_int c_exp = dividend / (-divisor);
    c /= -divisor;
    EXPECT_EQ(c, c_exp);

    // Both negative.
    big_int       d     = -dividend;
    const big_int d_exp = (-dividend) / (-divisor);
    d /= -divisor;
    EXPECT_EQ(d, d_exp);

    // Quotient that trims back to a single limb after division.
    big_int       e     = (big_int{1} << 200);
    const big_int e_div = (big_int{1} << 100);
    const big_int e_exp = e / e_div;
    e /= e_div;
    EXPECT_EQ(e, e_exp);

    // |dividend| < |divisor|: quotient is exactly zero, sign must canonicalize.
    big_int f = (big_int{1} << 100) + big_int{1};
    f /= -((big_int{1} << 200));
    EXPECT_EQ(f, big_int{0});
    EXPECT_FALSE(f < big_int{0});
}

TEST(Division, CompoundAssignmentMultiLimbDivisorPrimitive) {
#ifndef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
    GTEST_SKIP() << "Requires 128-bit integer support.";
#else
    // uint128_t spans multiple limbs: exercises the integer-rhs slow path in
    // operator/= where to_limbs(...) yields a multi-limb span.
    using BEMAN_BIG_INT_NAMESPACE::detail::uint128_t;

    const uint128_t divisor  = (static_cast<uint128_t>(1) << 100) + uint128_t{12345};
    const big_int   big_div  = big_int{divisor};
    const big_int   dividend = (big_int{1} << 250) + big_int{67890};

    big_int       a        = dividend;
    const big_int expected = dividend / big_div;
    a /= divisor;
    EXPECT_EQ(a, expected);

    // Negative dividend; uint128_t divisor is unsigned, so sign comes from dividend.
    big_int       b     = -dividend;
    const big_int b_exp = (-dividend) / big_div;
    b /= divisor;
    EXPECT_EQ(b, b_exp);

    // |dividend| < |divisor|: quotient zero.
    big_int c = (big_int{1} << 50);
    c /= divisor;
    EXPECT_EQ(c, big_int{0});
#endif // BEMAN_BIG_INT_HAS_INT128
}

TEST(Division, CompoundAssignmentSingleLimbDivisorBigInt) {
    // Multi-limb dividend, single-limb basic_big_int divisor: exercises the
    // in-place divide_unsigned_short_inplace fast path in operator/=.
    big_int       a        = (big_int{1} << 200) + big_int{12345};
    const big_int expected = a / big_int{7};
    a /= big_int{7};
    EXPECT_EQ(a, expected);

    // Negative dividend, positive divisor.
    big_int    b    = -((big_int{1} << 200) + big_int{12345});
    const auto bexp = b / big_int{7};
    b /= big_int{7};
    EXPECT_EQ(b, bexp);

    // Positive dividend, negative divisor (sign flips).
    big_int    c    = (big_int{1} << 200) + big_int{12345};
    const auto cexp = c / big_int{-7};
    c /= big_int{-7};
    EXPECT_EQ(c, cexp);

    // Both negative.
    big_int    d    = -((big_int{1} << 200) + big_int{12345});
    const auto dexp = d / big_int{-7};
    d /= big_int{-7};
    EXPECT_EQ(d, dexp);

    // Result is exactly zero (dividend divides exactly into a smaller magnitude).
    big_int e = big_int{42};
    e /= big_int{100};
    EXPECT_EQ(e, big_int{0});

    // Single-limb dividend / single-limb divisor.
    big_int f{123456789};
    f /= big_int{1000};
    EXPECT_EQ(f, big_int{123456});

    // Result trims down from multi-limb to single-limb.
    big_int g = (big_int{1} << 64);
    g /= big_int{2};
    EXPECT_EQ(g, big_int{1} << 63);
}

TEST(Division, CompoundAssignmentSingleLimbDivisorPrimitive) {
    // Multi-limb dividend, primitive divisor that fits in a single limb.
    big_int       a        = (big_int{1} << 200) + big_int{12345};
    const big_int expected = a / 7U;
    a /= 7U;
    EXPECT_EQ(a, expected);

    // Negative result via signed divisor.
    big_int    b    = (big_int{1} << 200) + big_int{12345};
    const auto bexp = b / -7;
    b /= -7;
    EXPECT_EQ(b, bexp);

    // Wide unsigned divisor whose value still fits in one limb.
    big_int             c          = (big_int{1} << 200) + big_int{99999};
    const std::uint64_t d          = 1234567890123ULL;
    const big_int       expected_c = c / d;
    c /= d;
    EXPECT_EQ(c, expected_c);
}

TEST(Division, SelfDivision) {
    // Exercises the move-into-temp aliasing guard in operator/=.
    big_int a{123456789};
    a /= a;
    EXPECT_EQ(a, 1);

    big_int b{-123456789};
    b /= b;
    EXPECT_EQ(b, 1);
}

TEST(Division, PrimitiveRhsSingleLimbFastPath) {
    // Large dividend, small primitive divisor — hits divmod_into_short.
    const big_int huge = (big_int{1} << 256) + big_int{12345};
    const big_int r    = huge / 7U;
    // Verify: r * 7 + (huge % 7) == huge
    EXPECT_EQ(r * big_int{7} + (huge - r * big_int{7}), huge);
}

TEST(Division, PrimitiveLhs) {
    EXPECT_EQ(42 / big_int{7}, 6);
    EXPECT_EQ(-42 / big_int{7}, -6);
    EXPECT_EQ(42 / big_int{-7}, -6);
}

TEST(Division, DivRemMultiLimbMixedSigns) {
    // Multi-limb divisor + div_rem + mixed signs: remainder should follow dividend's sign.
    const big_int dividend = (big_int{1} << 200) + big_int{12345};
    const big_int divisor  = (big_int{1} << 100) + big_int{7};

    {
        const auto dr = div_rem_to_zero(dividend, divisor);
        EXPECT_EQ(dr.quotient, dividend / divisor);
        EXPECT_EQ(dr.remainder, dividend % divisor);
        EXPECT_TRUE(dr.remainder >= big_int{0});
    }
    {
        const auto dr = div_rem_to_zero(-dividend, divisor);
        EXPECT_EQ(dr.quotient, -dividend / divisor);
        EXPECT_EQ(dr.remainder, -dividend % divisor);
        EXPECT_TRUE(dr.remainder <= big_int{0});
    }
    {
        const auto dr = div_rem_to_zero(dividend, -divisor);
        EXPECT_EQ(dr.quotient, dividend / -divisor);
        EXPECT_EQ(dr.remainder, dividend % -divisor);
        EXPECT_TRUE(dr.remainder >= big_int{0});
    }
    {
        const auto dr = div_rem_to_zero(-dividend, -divisor);
        EXPECT_EQ(dr.quotient, -dividend / -divisor);
        EXPECT_EQ(dr.remainder, -dividend % -divisor);
        EXPECT_TRUE(dr.remainder <= big_int{0});
    }
}

TEST(Division, HeapPromotionBoundary) {
    // Value well above default inplace_capacity (64 bits * 4 limbs).
    // Using ~320 bits of dividend to guarantee heap storage.
    big_int dividend{1};
    dividend <<= 320;
    dividend = dividend + big_int{12345};
    const big_int divisor{65537};

    const big_int q = dividend / divisor;
    // Verify round-trip: q * divisor + (dividend - q * divisor) == dividend
    const big_int remainder = dividend - q * divisor;
    EXPECT_EQ(q * divisor + remainder, dividend);
    EXPECT_TRUE(remainder >= big_int{0});
    EXPECT_TRUE(remainder < divisor);
}

// ----- detail::divide_unsigned_short (preinv short division) -----

namespace short_div {

using uint_t = uint_multiprecision_t;

// Reference: the plain one-narrowing_div-per-limb loop the preinv version
// replaced.
uint_t
reference_short(const std::span<uint_t> quotient, const std::span<const uint_t> dividend, const uint_t divisor) {
    uint_t remainder = 0;
    for (std::size_t i = dividend.size(); i-- > 0;) {
        const BEMAN_BIG_INT_NAMESPACE::detail::wide<uint_t> num{.low_bits = dividend[i], .high_bits = remainder};
        const auto [q, r] = BEMAN_BIG_INT_NAMESPACE::detail::narrowing_div(num, divisor);
        quotient[i]       = q;
        remainder         = r;
    }
    return remainder;
}

void check_short(const std::vector<uint_t>& dividend, const uint_t divisor) {
    std::vector<uint_t> got(dividend.size());
    std::vector<uint_t> expected(dividend.size());
    const uint_t        got_r = BEMAN_BIG_INT_NAMESPACE::detail::divide_unsigned_short(
        std::span<uint_t>{got}, std::span<const uint_t>{dividend}, divisor);
    const uint_t expected_r = reference_short(std::span<uint_t>{expected}, std::span<const uint_t>{dividend}, divisor);
    EXPECT_EQ(got_r, expected_r) << "divisor=" << divisor;
    EXPECT_TRUE(std::ranges::equal(got, expected)) << "divisor=" << divisor;
}

} // namespace short_div

TEST(DivisionShortPreinv, MatchesReferenceLoop) {
    using short_div::check_short;
    constexpr auto  max = std::numeric_limits<uint_multiprecision_t>::max();
    std::mt19937_64 rng{0x5d1u};

    for (const std::size_t m : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7}, std::size_t{40}}) {
        std::vector<uint_multiprecision_t> dividend(m);
        for (int trial = 0; trial < 200; ++trial) {
            for (auto& limb : dividend) {
                limb = static_cast<uint_multiprecision_t>(rng());
            }
            // Divisor classes: tiny, arbitrary, normalized, power of two, max.
            check_short(dividend, 1);
            check_short(dividend, 2);
            check_short(dividend, 10);
            check_short(dividend, static_cast<uint_multiprecision_t>(rng()) | 1u);
            check_short(dividend,
                        static_cast<uint_multiprecision_t>(rng()) |
                            (uint_multiprecision_t{1}
                             << (BEMAN_BIG_INT_NAMESPACE::detail::width_v<uint_multiprecision_t> - 1)));
            check_short(dividend, uint_multiprecision_t{1} << 7);
            check_short(dividend, max);
            check_short(dividend, max - 1);
        }
    }
}

TEST(DivisionShortPreinv, InPlaceAliasing) {
    // divmod_in_place_short divides a buffer by a limb in place; the preinv
    // funnel must keep reading ahead of its writes.
    std::mt19937_64                    rng{0xa11a5u};
    std::vector<uint_multiprecision_t> buffer(9);
    for (auto& limb : buffer) {
        limb = static_cast<uint_multiprecision_t>(rng());
    }
    const std::vector<uint_multiprecision_t> original = buffer;
    const uint_multiprecision_t              divisor  = (uint_multiprecision_t{1} << 13) + 12345u;

    std::vector<uint_multiprecision_t> expected(buffer.size());
    const uint_multiprecision_t        expected_r = short_div::reference_short(
        std::span<uint_multiprecision_t>{expected}, std::span<const uint_multiprecision_t>{original}, divisor);

    const auto                  buf_span = std::span<uint_multiprecision_t>{buffer};
    const uint_multiprecision_t got_r =
        BEMAN_BIG_INT_NAMESPACE::detail::divide_unsigned_short(buf_span, buf_span, divisor);

    EXPECT_EQ(got_r, expected_r);
    EXPECT_TRUE(std::ranges::equal(buffer, expected));
}

// ----- detail::submul_single_limb -----

// result_before == result_after + a * val + borrow * B^a.size() over the
// touched limbs.
TEST(SubmulSingleLimb, ReconstructionIdentity) {
    using uint_t = uint_multiprecision_t;
    std::mt19937_64 rng{0x5ab111u};

    for (const std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{5}, std::size_t{17}}) {
        for (int trial = 0; trial < 200; ++trial) {
            std::vector<uint_t> r_before(n);
            std::vector<uint_t> a(n);
            for (auto& limb : r_before) {
                limb = static_cast<uint_t>(rng());
            }
            for (auto& limb : a) {
                limb = static_cast<uint_t>(rng());
            }
            const uint_t val = static_cast<uint_t>(rng());

            std::vector<uint_t> r_after = r_before;
            const uint_t        borrow  = BEMAN_BIG_INT_NAMESPACE::detail::submul_single_limb(
                std::span<uint_t>{r_after}, std::span<const uint_t>{a}, val);

            // r_after + a*val must equal r_before + borrow * B^n.
            std::vector<uint_t> sum(n + 1, 0);
            std::copy(r_after.begin(), r_after.end(), sum.begin());
            std::vector<uint_t> product(n + 1, 0);
            const std::size_t   p_size = BEMAN_BIG_INT_NAMESPACE::detail::multiply_single_limb(
                std::span<uint_t>{product}, std::span<const uint_t>{a}, val);
            const bool carry = BEMAN_BIG_INT_NAMESPACE::detail::add_unsigned_spans(
                std::span<uint_t>{sum}, std::span<const uint_t>{sum}, std::span<const uint_t>{product.data(), p_size});
            EXPECT_FALSE(carry);

            std::vector<uint_t> expected(n + 1, 0);
            std::copy(r_before.begin(), r_before.end(), expected.begin());
            expected[n] = borrow;
            EXPECT_TRUE(std::ranges::equal(sum, expected)) << "n=" << n << " val=" << val;
        }
    }
}

TEST(SubmulSingleLimb, Boundaries) {
    using uint_t       = uint_multiprecision_t;
    constexpr auto max = std::numeric_limits<uint_t>::max();

    // val == 0 leaves the buffer untouched with no borrow.
    std::vector<uint_t>       r{1, 2, 3};
    const std::vector<uint_t> a{max, max, max};
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::detail::submul_single_limb(std::span<uint_t>{r}, std::span<const uint_t>{a}, 0),
              0u);
    EXPECT_EQ(r[0], 1u);
    EXPECT_EQ(r[2], 3u);

    // Maximal everything: 0 - (B^3 - 1)*(B - 1) = -(B^4 - B^3 - B + 1), i.e.
    // r_after = <0, 0, max> with borrow B - 1 (the identity gives
    // borrow * B^3 = (B - 1) * B^3).
    std::vector<uint_t> zero{0, 0, 0};
    const uint_t        borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::submul_single_limb(std::span<uint_t>{zero}, std::span<const uint_t>{a}, max);
    EXPECT_EQ(borrow, max);
    EXPECT_EQ(zero[0], max);
    EXPECT_EQ(zero[1], 0u);
    EXPECT_EQ(zero[2], 0u);

    // Empty `a` is a no-op.
    std::vector<uint_t> untouched{42};
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::detail::submul_single_limb(
                  std::span<uint_t>{untouched}, std::span<const uint_t>{}, max),
              0u);
    EXPECT_EQ(untouched[0], 42u);
}

} // namespace
