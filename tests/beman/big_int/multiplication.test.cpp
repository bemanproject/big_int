// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <bit>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::from_chars;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// ----- compile-time sanity -----

consteval bool ce_zero_times_zero() { return (big_int{0} * big_int{0}) == big_int{0}; }
static_assert(ce_zero_times_zero());

consteval bool ce_small_positive() { return (big_int{3} * big_int{7}) == big_int{21}; }
static_assert(ce_small_positive());

consteval bool ce_identity() { return (big_int{42} * big_int{1}) == big_int{42}; }
static_assert(ce_identity());

consteval bool ce_negative_identity() { return (big_int{42} * big_int{-1}) == big_int{-42}; }
static_assert(ce_negative_identity());

consteval bool ce_both_negative() { return (big_int{-3} * big_int{-7}) == big_int{21}; }
static_assert(ce_both_negative());

consteval bool ce_mixed_sign() { return (big_int{-3} * big_int{7}) == big_int{-21}; }
static_assert(ce_mixed_sign());

consteval bool ce_zero_absorbs() { return (big_int{12345} * big_int{0}) == big_int{0}; }
static_assert(ce_zero_absorbs());

consteval bool ce_255() {
    big_int result{2};
    result *= 10;
    result += 5;
    result *= 10;
    result += 5;
    return result == big_int{255};
}
static_assert(ce_255());

consteval bool move_heap_mul() {
    big_int result{2};

    big_int lhs{2};
    lhs <<= 155;

    result *= lhs;
    big_int result2 = std::move(result);
    result2 *= lhs;
    result2 += 10;
    return result2 > big_int{255};
}
static_assert(move_heap_mul());

consteval bool ce_power_of_two_multi_limb() {
    // At compile time power-of-two operands take the shifted-copy path
    // unconditionally (no cutoff gate), keeping large products within
    // consteval step limits.
    const big_int a = big_int{1} << 2600;
    const big_int b = big_int{1} << 2600;
    const big_int c = big_int{1} << 130;
    return (a * b) == (big_int{1} << 5200) && (c * c) == (big_int{1} << 260);
}
static_assert(ce_power_of_two_multi_limb());

consteval bool ce_squaring_multi_limb() {
    // Same-object multiplication takes the consteval squaring path; verify
    // against the closed form (2^N - 5)^2 = 2^(2N) - 5*2^(N+1) + 25.
    const big_int x = (big_int{1} << 1024) - 5;
    return x * x == (big_int{1} << 2048) - (big_int{5} << 1025) + 25;
}
static_assert(ce_squaring_multi_limb());

// ----- runtime tests -----

TEST(Multiplication, SmallPositivePositive) {
    EXPECT_EQ(big_int{2} * big_int{3}, 6);
    EXPECT_EQ(big_int{7} * big_int{11}, 77);
    EXPECT_EQ(big_int{100} * big_int{200}, 20000);
}

TEST(Multiplication, SmallNegativeNegative) {
    EXPECT_EQ(big_int{-2} * big_int{-3}, 6);
    EXPECT_EQ(big_int{-7} * big_int{-11}, 77);
}

TEST(Multiplication, MixedSigns) {
    EXPECT_EQ(big_int{-5} * big_int{3}, -15);
    EXPECT_EQ(big_int{5} * big_int{-3}, -15);
    EXPECT_EQ(big_int{-1} * big_int{100}, -100);
    EXPECT_EQ(big_int{100} * big_int{-1}, -100);
}

TEST(Multiplication, ZeroIdentity) {
    EXPECT_EQ(big_int{0} * big_int{0}, 0);
    EXPECT_EQ(big_int{42} * big_int{0}, 0);
    EXPECT_EQ(big_int{0} * big_int{42}, 0);
    EXPECT_EQ(big_int{-42} * big_int{0}, 0);
    EXPECT_EQ(big_int{0} * big_int{-42}, 0);

    // No negative zero from negative * zero
    const big_int r = big_int{-42} * big_int{0};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Multiplication, MultiplicativeIdentity) {
    EXPECT_EQ(big_int{42} * big_int{1}, 42);
    EXPECT_EQ(big_int{1} * big_int{42}, 42);
    EXPECT_EQ(big_int{-42} * big_int{1}, -42);
    EXPECT_EQ(big_int{1} * big_int{-42}, -42);
}

TEST(Multiplication, SingleLimbOverflow) {
    // UINT64_MAX * 2 should produce a two-limb result.
    const auto    max_val = std::numeric_limits<std::uint64_t>::max();
    const big_int a{max_val};
    const big_int b{2};
    const big_int r = a * b;
    EXPECT_EQ(r, (big_int{1} << 65) + big_int{-2});
}

TEST(Multiplication, NoAllocationWhenInlineFits) {
    // Default `big_int` has one inplace limb.
    // A small product like 2 * 2 fits in that single limb and must not allocate
    const big_int a{2};
    const big_int b{2};
    const big_int r = a * b;
    EXPECT_EQ(r, 4);
    EXPECT_TRUE(is_inplace(r));

    // Just because we have heap space doesn't mean we should use it
    big_int c{3};
    c.reserve_representation(8);
    ASSERT_FALSE(is_inplace(c));
    const big_int d{4};
    const big_int r2 = c * d;
    EXPECT_EQ(r2, 12);
    EXPECT_TRUE(is_inplace(r2));

    // `basic_big_int<256>` has at least 4 inline limbs.
    // A product that fits within those 4 limbs must not allocate.
    using big_int_256 = basic_big_int<256>;
    const big_int_256 e{std::numeric_limits<std::uint64_t>::max()};
    const big_int_256 f{std::numeric_limits<std::uint64_t>::max()};
    const big_int_256 r3 = e * f;
    EXPECT_TRUE(is_inplace(r3));
}

TEST(Multiplication, SingleLimbTimesMultiLimb) {
    // (2^64) * 3 = 3 * 2^64
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 * big_int{3};
    EXPECT_EQ(r, big_int{3} << 64);
}

TEST(Multiplication, MultiLimbTimesMultiLimb) {
    // (2^64) * (2^64) = 2^128
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 * two_64;
    EXPECT_EQ(r, big_int{1} << 128);
}

TEST(Multiplication, SelfMultiplication) {
    // 1000 * 1000 = 1000000
    const big_int a{1000};
    const big_int r = a * a;
    EXPECT_EQ(r, 1000000);
}

TEST(Multiplication, Commutativity) {
    const big_int a{123456789};
    const big_int b{987654321};
    EXPECT_EQ(a * b, b * a);
}

TEST(Multiplication, PowerOfTwoMatchesShift) {
    // x * 2^k should equal x << k
    const big_int x{12345};
    const big_int two = big_int{1} << 10; // 1024
    EXPECT_EQ(x * two, x << 10);
}

TEST(Multiplication, BigIntTimesPrimitiveUnsigned) {
    EXPECT_EQ(big_int{10} * 5U, 50);
    EXPECT_EQ(big_int{-10} * 5U, -50);
    EXPECT_EQ(big_int{0} * 5U, 0);
    EXPECT_EQ(big_int{10} * 0U, 0);
}

TEST(Multiplication, PrimitiveUnsignedTimesBigInt) {
    EXPECT_EQ(5U * big_int{10}, 50);
    EXPECT_EQ(5U * big_int{-10}, -50);
    EXPECT_EQ(0U * big_int{10}, 0);
}

TEST(Multiplication, BigIntTimesPrimitiveSigned) {
    EXPECT_EQ(big_int{10} * 5, 50);
    EXPECT_EQ(big_int{10} * -5, -50);
    EXPECT_EQ(big_int{-10} * 5, -50);
    EXPECT_EQ(big_int{-10} * -5, 50);
    EXPECT_EQ(big_int{10} * 0, 0);
}

TEST(Multiplication, PrimitiveSignedTimesBigInt) {
    EXPECT_EQ(5 * big_int{10}, 50);
    EXPECT_EQ(-5 * big_int{10}, -50);
    EXPECT_EQ(5 * big_int{-10}, -50);
    EXPECT_EQ(-5 * big_int{-10}, 50);
}

TEST(Multiplication, SmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            const big_int bx{x};
            const big_int by{y};

            /* move_move */ EXPECT_EQ(big_int{x} * big_int{y}, x * y);
            /* move_copy */ EXPECT_EQ(big_int{x} * by, x * y);
            /* copy_move */ EXPECT_EQ(bx * big_int{y}, x * y);
            /* copy_copy */ EXPECT_EQ(bx * by, x * y);
            /* move_int  */ EXPECT_EQ(big_int{x} * y, x * y);
            /* int_move  */ EXPECT_EQ(x * big_int{y}, x * y);
            /* copy_int  */ EXPECT_EQ(bx * y, x * y);
            /* int_copy  */ EXPECT_EQ(x * by, x * y);
        }
    }
}

TEST(Multiplication, CompoundAssignmentBasic) {
    big_int a{6};
    a *= big_int{7};
    EXPECT_EQ(a, 42);
}

TEST(Multiplication, CompoundAssignmentPrimitive) {
    big_int a{6};
    a *= 7;
    EXPECT_EQ(a, 42);

    big_int b{-10};
    b *= -3;
    EXPECT_EQ(b, 30);
}

TEST(Multiplication, CompoundAssignmentZero) {
    big_int a{42};
    a *= big_int{0};
    EXPECT_EQ(a, 0);
    EXPECT_FALSE(a < 0);
}

TEST(Multiplication, CompoundAssignmentSelf) {
    // `y *= y` must not be confused by the fact that rhs aliases *this.
    big_int a{1000};
    a *= a;
    EXPECT_EQ(a, 1000000);

    // Multi-limb self-squaring.
    big_int b = big_int{1} << 80; // 2^80
    b *= b;                       // 2^160
    EXPECT_EQ(b, big_int{1} << 160);

    // Negative self-square stays positive.
    big_int c{-12345};
    c *= c;
    EXPECT_EQ(c, 12345 * 12345);

    // Zero self-square.
    big_int d{0};
    d *= d;
    EXPECT_EQ(d, 0);
}

TEST(Multiplication, CompoundAssignmentMultiLimb) {
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    a *= big_int{2};
    EXPECT_EQ(a, big_int{1} << 65);
}

TEST(Multiplication, LargeMultiLimbLongMul) {
    // Construct numbers that span several limbs but stay below the Karatsuba cutoff.
    // a = 2^256 - 1 (4 limbs of all 1s on 64-bit)
    big_int a{1};
    a <<= 256;
    a = a + big_int{-1}; // 2^256 - 1

    // b = 2
    // a * 2 = 2^257 - 2
    const big_int r        = a * big_int{2};
    const big_int expected = (big_int{1} << 257) + big_int{-2};
    EXPECT_EQ(r, expected);
}

TEST(Multiplication, FactorialSmoke) {
    // Compute 20! and verify.
    // 20! = 2432902008176640000
    big_int factorial{1};
    for (int i = 2; i <= 20; ++i) {
        factorial *= i;
    }
    // Verify against known value: 20! = 2432902008176640000
    const big_int expected{static_cast<std::uint64_t>(2432902008176640000ULL)};
    EXPECT_EQ(factorial, expected);
}

TEST(Multiplication, LargeFactorial) {
    // Compute 50! which requires multiple limbs.
    // 50! = 30414093201713378043612608166979581188299763898377856820553615673507270386838265
    // We verify by checking a * b = b * a consistency and that the result has the right number
    // of limbs (50! has about 214 bits, so 4 limbs on 64-bit).
    big_int factorial{1};
    for (int i = 2; i <= 50; ++i) {
        factorial *= i;
    }
    EXPECT_GT(factorial.representation().size(), 2u);
    EXPECT_TRUE(factorial > 0);

    // Cross-check: compute in reverse order and verify equality.
    big_int factorial_rev{1};
    for (int i = 50; i >= 2; --i) {
        factorial_rev *= i;
    }
    EXPECT_EQ(factorial, factorial_rev);
}

TEST(Multiplication, MultiLimbSquaring) {
    // (2^128 - 1)^2 = 2^256 - 2^129 + 1
    big_int a{1};
    a <<= 128;
    a = a + big_int{-1}; // 2^128 - 1

    const big_int r        = a * a;
    const big_int expected = (big_int{1} << 256) + (-(big_int{1} << 129)) + big_int{1};
    EXPECT_EQ(r, expected);
}

TEST(Multiplication, PowerOfTwoMultiLimbTimesDense) {
    // The power-of-two dispatch path engages once BOTH operands clear the
    // karatsuba cutoff (here only k = 4096: a 65-limb 2^k against the 79-limb
    // dense operand); smaller k values exercise tiny-pow2 * large-dense via
    // the long-mul fallback. The result must match the independently
    // implemented shift operator. Bit positions cover limb-aligned shifts,
    // top-bit carry-out, and both operand orders.
    const big_int dense = (big_int{1} << 5000) - 1;
    for (const unsigned k : {64U, 65U, 127U, 128U, 191U, 200U, 4096U}) {
        const big_int p2 = big_int{1} << k;
        EXPECT_EQ(dense * p2, dense << k);
        EXPECT_EQ(p2 * dense, dense << k);
    }
}

TEST(Multiplication, PowerOfTwoTimesPowerOfTwo) {
    // Bit positions below and above the karatsuba cutoff (40 limbs = 2560
    // bits) cover both the long-mul fallback and the shifted-copy path
    // (4096 x 4096 is the pair where both operands clear the cutoff).
    for (const unsigned j : {64U, 127U, 128U, 1000U, 4096U}) {
        for (const unsigned k : {64U, 127U, 128U, 1000U, 4096U}) {
            const big_int lhs = big_int{1} << j;
            const big_int rhs = big_int{1} << k;
            EXPECT_EQ(lhs * rhs, big_int{1} << (j + k));
        }
    }
}

TEST(Multiplication, NearPowerOfTwoNotMisdetected) {
    // Values whose top limb is a single bit but with non-zero limbs below must
    // not take the shift path; verify products via algebraic identities.
    const big_int m = (big_int{1} << 300) - 1;

    const big_int just_above = (big_int{1} << 256) + 1;
    EXPECT_EQ(just_above * m, (m << 256) + m);

    const big_int two_sparse_bits = (big_int{1} << 256) + (big_int{1} << 32);
    EXPECT_EQ(two_sparse_bits * m, (m << 256) + (m << 32));

    const big_int three_p2 = big_int{3} << 256;
    EXPECT_EQ(three_p2 * m, (m << 257) + (m << 256));

    const big_int all_ones = (big_int{1} << 256) - 1;
    EXPECT_EQ(all_ones * m, (m << 256) - m);
}

TEST(Multiplication, PowerOfTwoSignsPreserved) {
    const big_int x  = (big_int{1} << 200) - 12345;
    const big_int p2 = big_int{1} << 100;
    EXPECT_EQ((-x) * p2, -(x << 100));
    EXPECT_EQ(x * (-p2), -(x << 100));
    EXPECT_EQ((-x) * (-p2), x << 100);
}

TEST(Multiplication, PowerOfTwoLargeOperands) {
    // Sizes that previously dispatched into Karatsuba / Toom-Cook.
    const big_int  dense = (big_int{1} << 320000) - 1; // 5000 limbs
    const unsigned k     = 320000U;
    const big_int  p2    = big_int{1} << k;
    EXPECT_EQ(dense * p2, dense << k);
    EXPECT_EQ(p2 * p2, big_int{1} << (2U * k));
}

TEST(Multiplication, SquaringClosedForm) {
    // (2^N - 1)^2 = 2^(2N) - 2^(N+1) + 1, with N spanning every squaring tier
    // up to square_toom_cook_6_5.
    for (const unsigned n : {640U, 6400U, 64000U, 320000U, 3840000U}) {
        const big_int x        = (big_int{1} << n) - 1;
        const big_int expected = (big_int{1} << (2U * n)) - (big_int{1} << (n + 1U)) + 1;
        EXPECT_EQ(x * x, expected) << "n=" << n;
    }
}

TEST(Multiplication, SquaringRandomDifferential) {
    // x * x passes the same span twice and takes the squaring path; x * copy
    // goes through the general kernels. Random values exercise the
    // sign-dependent evaluation branches at every dispatch tier.
    std::mt19937_64                    rng{0x5EEDU};
    std::uniform_int_distribution<int> pick{0, 15};
    constexpr const char*              digits = "0123456789abcdef";
    for (const std::size_t hex_len :
         {48UL, 112UL, 160UL, 480UL, 1024UL, 3100UL, 25000UL, 90000UL, 200000UL, 800000UL}) {
        std::string s(hex_len, '0');
        for (auto& ch : s) {
            ch = digits[pick(rng)];
        }
        s.front() = 'f'; // keep the top limb significant

        big_int x;
        const auto [p, ec] = from_chars(s.data(), s.data() + s.size(), x, 16);
        ASSERT_EQ(ec, std::errc{});
        ASSERT_EQ(p, s.data() + s.size());

        const big_int x_copy = x;
        EXPECT_EQ(x * x, x * x_copy) << "hex_len=" << hex_len;
        EXPECT_EQ((-x) * (-x), x * x_copy) << "hex_len=" << hex_len;
    }
}

TEST(Multiplication, CompoundSelfSquaring) {
    // y *= y routes through the same operand-identity detection.
    big_int       y  = (big_int{1} << 1000) - 12345;
    const big_int x  = y;
    const big_int xc = y;
    y *= y;
    EXPECT_EQ(y, x * xc);
}

TEST(Multiplication, Mersenne) {
    // 2^1398269 - 1
    std::string control_val(349'568, 'f');
    control_val.front() = '1';

    // Compute 2^1398269 - 1 via square-and-multiply ladder.
    constexpr std::uint32_t exponent = 1398269;
    big_int                 result{1};
    for (int i = std::bit_width(exponent) - 1; i >= 0; --i) {
        result *= result;
        if ((exponent >> i) & 1u) {
            result *= 2;
        }
    }
    result -= 1;

    big_int expected;
    const auto [p, ec] = from_chars(control_val.data(), control_val.data() + control_val.size(), expected, 16);
    ASSERT_EQ(ec, std::errc{});
    ASSERT_EQ(p, control_val.data() + control_val.size());
    EXPECT_EQ(result, expected);

    // Test also via shifting
    big_int expected_shift{1};
    expected_shift <<= 1398269;
    expected_shift -= 1;
    EXPECT_EQ(result, expected_shift);
}

} // namespace
