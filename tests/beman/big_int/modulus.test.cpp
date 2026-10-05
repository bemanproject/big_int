// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// ----- compile-time sanity -----

consteval bool ce_zero_mod_one() { return (big_int{0} % big_int{1}) == big_int{0}; }
static_assert(ce_zero_mod_one());

consteval bool ce_small_positive() { return (big_int{7} % big_int{3}) == big_int{1}; }
static_assert(ce_small_positive());

consteval bool ce_exact_multiple() { return (big_int{42} % big_int{6}) == big_int{0}; }
static_assert(ce_exact_multiple());

consteval bool ce_sign_neg_pos() { return (big_int{-7} % big_int{3}) == big_int{-1}; }
static_assert(ce_sign_neg_pos());

consteval bool ce_sign_pos_neg() { return (big_int{7} % big_int{-3}) == big_int{1}; }
static_assert(ce_sign_pos_neg());

consteval bool ce_sign_neg_neg() { return (big_int{-7} % big_int{-3}) == big_int{-1}; }
static_assert(ce_sign_neg_neg());

consteval bool ce_self_mod() { return (big_int{42} % big_int{42}) == big_int{0}; }
static_assert(ce_self_mod());

consteval bool ce_mod_by_one() { return (big_int{42} % big_int{1}) == big_int{0}; }
static_assert(ce_mod_by_one());

consteval bool ce_dividend_less_than_divisor() { return (big_int{3} % big_int{7}) == big_int{3}; }
static_assert(ce_dividend_less_than_divisor());

// ----- runtime tests -----

TEST(Modulus, SmallPositivePositive) {
    EXPECT_EQ(big_int{7} % big_int{3}, 1);
    EXPECT_EQ(big_int{100} % big_int{3}, 1);
    EXPECT_EQ(big_int{42} % big_int{7}, 0);
}

TEST(Modulus, ExactMultiples) {
    EXPECT_EQ(big_int{0} % big_int{5}, 0);
    EXPECT_EQ(big_int{10} % big_int{2}, 0);
    EXPECT_EQ(big_int{999} % big_int{3}, 0);
}

TEST(Modulus, SignRuleRemainderFollowsDividend) {
    // C++ truncated modulus: remainder sign matches dividend sign.
    EXPECT_EQ(big_int{7} % big_int{3}, 1);
    EXPECT_EQ(big_int{-7} % big_int{3}, -1);
    EXPECT_EQ(big_int{7} % big_int{-3}, 1);
    EXPECT_EQ(big_int{-7} % big_int{-3}, -1);
}

TEST(Modulus, MixedSigns) {
    EXPECT_EQ(big_int{-42} % big_int{7}, 0);
    EXPECT_EQ(big_int{42} % big_int{-7}, 0);
    EXPECT_EQ(big_int{-42} % big_int{-7}, 0);

    EXPECT_EQ(big_int{-100} % big_int{3}, -1);
    EXPECT_EQ(big_int{100} % big_int{-3}, 1);
    EXPECT_EQ(big_int{-100} % big_int{-3}, -1);
}

TEST(Modulus, DividendLessThanDivisor) {
    EXPECT_EQ(big_int{3} % big_int{7}, 3);
    EXPECT_EQ(big_int{-3} % big_int{7}, -3);
    EXPECT_EQ(big_int{3} % big_int{-7}, 3);
    EXPECT_EQ(big_int{-3} % big_int{-7}, -3);
}

TEST(Modulus, ZeroDividend) {
    EXPECT_EQ(big_int{0} % big_int{5}, 0);
    EXPECT_EQ(big_int{0} % big_int{-5}, 0);
    EXPECT_EQ(big_int{0} % big_int{9999999}, 0);
}

TEST(Modulus, DividendEqualsDivisor) {
    EXPECT_EQ(big_int{42} % big_int{42}, 0);
    EXPECT_EQ(big_int{-42} % big_int{42}, 0);
    EXPECT_EQ(big_int{42} % big_int{-42}, 0);
    EXPECT_EQ(big_int{-42} % big_int{-42}, 0);
}

TEST(Modulus, ModulusByOne) {
    EXPECT_EQ(big_int{42} % big_int{1}, 0);
    EXPECT_EQ(big_int{-42} % big_int{1}, 0);
    EXPECT_EQ(big_int{42} % big_int{-1}, 0);
    EXPECT_EQ(big_int{-42} % big_int{-1}, 0);
}

TEST(Modulus, MultiLimbBySingleLimb) {
    // (2^64 + 5) % 10 == 1 (since 2^64 = 18446744073709551616 ends in 6)
    const big_int huge = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{6};
    EXPECT_EQ(huge % big_int{10}, 1);
}

TEST(Modulus, MultiLimbByMultiLimb) {
    // ((2^64) + 5) % ((2^64) + 1) == 4
    const big_int two_64_plus_5 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{6};
    const big_int two_64_plus_1 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{2};
    EXPECT_EQ(two_64_plus_5 % two_64_plus_1, 4);
}

TEST(Modulus, DivisionInvariant) {
    // For any a, b != 0: a == (a / b) * b + (a % b).
    const big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{123};
    const big_int b = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{7};
    EXPECT_EQ((a / b) * b + (a % b), a);

    const big_int c{-1'000'000'007LL};
    const big_int d{99'991LL};
    EXPECT_EQ((c / d) * d + (c % d), c);
}

TEST(Modulus, MultiplyAndMod) {
    // (x * y) % y == 0 for a sequence of pairs.
    for (int i = 1; i < 50; ++i) {
        for (int j = 1; j < 50; ++j) {
            const big_int x{i * 1'000'000'007LL};
            const big_int y{j * 99'991LL};
            EXPECT_EQ((x * y) % y, 0);
        }
    }
}

TEST(Modulus, LargeShiftBasis) {
    // (2^200 + 7) % (2^100) == 7 (low 100 bits)
    const big_int dividend = (big_int{1} << 200) + big_int{7};
    const big_int divisor  = big_int{1} << 100;
    EXPECT_EQ(dividend % divisor, 7);
}

TEST(Modulus, CompoundAssignmentBigInt) {
    big_int a{42};
    a %= big_int{5};
    EXPECT_EQ(a, 2);

    big_int b{-42};
    b %= big_int{5};
    EXPECT_EQ(b, -2);
}

TEST(Modulus, CompoundAssignmentPrimitive) {
    big_int a{100};
    a %= 7;
    EXPECT_EQ(a, 2);

    big_int b{-100};
    b %= 3;
    EXPECT_EQ(b, -1);

    big_int c{100};
    c %= -3;
    EXPECT_EQ(c, 1);
}

TEST(Modulus, CompoundAssignmentMultiLimb) {
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{10}; // 2^64 + 9
    a %= big_int{7};
    // (2^64 + 9) mod 7: 2^64 mod 7 = 2, so (2 + 9) mod 7 = 11 mod 7 = 4
    EXPECT_EQ(a, 4);
}

TEST(Modulus, CompoundAssignmentMultiLimbDivisorBigInt) {
    // Multi-limb dividend, multi-limb basic_big_int divisor: exercises the
    // generic divmod_into slow path in operator%=.
    const big_int divisor  = (big_int{1} << 100) + big_int{12345};
    const big_int dividend = (big_int{1} << 250) + big_int{67890};

    big_int       a        = dividend;
    const big_int expected = dividend % divisor;
    a %= divisor;
    EXPECT_EQ(a, expected);
    EXPECT_EQ((dividend / divisor) * divisor + a, dividend);

    // Negative dividend, positive multi-limb divisor — remainder follows dividend sign.
    big_int       b     = -dividend;
    const big_int b_exp = (-dividend) % divisor;
    b %= divisor;
    EXPECT_EQ(b, b_exp);
    EXPECT_TRUE(b <= big_int{0});

    // Positive dividend, negative multi-limb divisor — remainder still follows dividend sign.
    big_int       c     = dividend;
    const big_int c_exp = dividend % (-divisor);
    c %= -divisor;
    EXPECT_EQ(c, c_exp);
    EXPECT_TRUE(c >= big_int{0});

    // Both negative.
    big_int       d     = -dividend;
    const big_int d_exp = (-dividend) % (-divisor);
    d %= -divisor;
    EXPECT_EQ(d, d_exp);

    // |dividend| < |divisor|: remainder equals dividend, sign canonical.
    big_int       e_div  = (big_int{1} << 200);
    big_int       e      = (big_int{1} << 100) + big_int{1};
    const big_int e_orig = e;
    e %= e_div;
    EXPECT_EQ(e, e_orig);

    // Result becomes zero — sign must not be left as negative zero.
    const big_int multiple = divisor * ((big_int{1} << 64) + big_int{3});
    big_int       f        = -multiple;
    f %= -divisor;
    EXPECT_EQ(f, big_int{0});
    EXPECT_FALSE(f < big_int{0});
}

TEST(Modulus, CompoundAssignmentMultiLimbDivisorPrimitive) {
#ifndef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
    GTEST_SKIP() << "Requires 128-bit integer support.";
#else
    // uint128_t spans multiple limbs: exercises the integer-rhs slow path in
    // operator%= where to_limbs(...) yields a multi-limb span.
    using BEMAN_BIG_INT_NAMESPACE::detail::uint128_t;

    const uint128_t divisor  = (static_cast<uint128_t>(1) << 100) + uint128_t{12345};
    const big_int   big_div  = big_int{divisor};
    const big_int   dividend = (big_int{1} << 250) + big_int{67890};

    big_int       a        = dividend;
    const big_int expected = dividend % big_div;
    a %= divisor;
    EXPECT_EQ(a, expected);
    EXPECT_EQ((dividend / big_div) * big_div + a, dividend);

    // Negative dividend.
    big_int       b     = -dividend;
    const big_int b_exp = (-dividend) % big_div;
    b %= divisor;
    EXPECT_EQ(b, b_exp);

    // |dividend| < |divisor|: remainder equals dividend.
    big_int       c      = (big_int{1} << 50);
    const big_int c_orig = c;
    c %= divisor;
    EXPECT_EQ(c, c_orig);
#endif // BEMAN_BIG_INT_HAS_INT128
}

TEST(Modulus, CompoundAssignmentSingleLimbDivisorBigInt) {
    // Multi-limb dividend, single-limb big_int divisor: hits divmod_in_place_short.
    big_int    a          = (big_int{1} << 200) + big_int{12345};
    const auto r_expected = a % big_int{7};
    a %= big_int{7};
    EXPECT_EQ(a, r_expected);
    EXPECT_TRUE(a >= big_int{0});
    EXPECT_TRUE(a < big_int{7});

    // Negative dividend retains its sign per truncated modulus.
    big_int b = -((big_int{1} << 200) + big_int{12345});
    b %= big_int{7};
    EXPECT_EQ(b, -r_expected);

    // Negative single-limb divisor: remainder sign follows dividend.
    big_int c = (big_int{1} << 200) + big_int{12345};
    c %= big_int{-7};
    EXPECT_EQ(c, r_expected);

    // Result becomes zero — sign must not be left as negative zero.
    big_int d = big_int{7} * ((big_int{1} << 128) + big_int{1});
    d %= big_int{-7};
    EXPECT_EQ(d, 0);
    EXPECT_FALSE(d < big_int{0});
}

TEST(Modulus, CompoundAssignmentSingleLimbDivisorPrimitive) {
    // Multi-limb dividend, primitive divisor: hits divmod_in_place_short.
    big_int    a          = (big_int{1} << 200) + big_int{12345};
    const auto r_expected = a % 7;
    a %= 7;
    EXPECT_EQ(a, r_expected);

    big_int b = -((big_int{1} << 200) + big_int{12345});
    b %= 7;
    EXPECT_EQ(b, -r_expected);

    big_int c = (big_int{1} << 200) + big_int{12345};
    c %= -7;
    EXPECT_EQ(c, r_expected);
}

TEST(Modulus, SelfMod) {
    // Exercises the move-into-temp aliasing guard in operator%=.
    big_int a{123456789};
    a %= a;
    EXPECT_EQ(a, 0);

    big_int b{-123456789};
    b %= b;
    EXPECT_EQ(b, 0);
}

TEST(Modulus, PrimitiveRhsSingleLimbFastPath) {
    // Large dividend, small primitive divisor — hits divmod_into_short.
    const big_int huge = (big_int{1} << 256) + big_int{12345};
    const big_int r    = huge % 7U;
    // Verify: huge == (huge / 7) * 7 + r, and 0 <= r < 7.
    EXPECT_TRUE(r >= big_int{0});
    EXPECT_TRUE(r < big_int{7});
    EXPECT_EQ((huge / 7U) * big_int{7} + r, huge);
}

TEST(Modulus, PrimitiveLhs) {
    EXPECT_EQ(7 % big_int{3}, 1);
    EXPECT_EQ(-7 % big_int{3}, -1);
    EXPECT_EQ(7 % big_int{-3}, 1);
}

TEST(Modulus, HeapPromotionBoundary) {
    // Value well above default inplace_capacity.
    big_int dividend{1};
    dividend <<= 320;
    dividend = dividend + big_int{12345};
    const big_int divisor{65537};

    const big_int r = dividend % divisor;
    EXPECT_TRUE(r >= big_int{0});
    EXPECT_TRUE(r < divisor);
    // Division invariant holds.
    EXPECT_EQ((dividend / divisor) * divisor + r, dividend);
}

TEST(Modulus, SmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            if (y == 0) {
                continue;
            }
            const big_int bx{x};
            const big_int by{y};

            /* move_move */ EXPECT_EQ(big_int{x} % big_int{y}, x % y);
            /* move_copy */ EXPECT_EQ(big_int{x} % by, x % y);
            /* copy_move */ EXPECT_EQ(bx % big_int{y}, x % y);
            /* copy_copy */ EXPECT_EQ(bx % by, x % y);
            /* move_int  */ EXPECT_EQ(big_int{x} % y, x % y);
            /* int_move  */ EXPECT_EQ(x % big_int{y}, x % y);
            /* copy_int  */ EXPECT_EQ(bx % y, x % y);
            /* int_copy  */ EXPECT_EQ(x % by, x % y);
        }
    }
}

} // namespace
