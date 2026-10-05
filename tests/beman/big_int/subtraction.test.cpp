// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// ----- compile-time sanity -----
// See addition.test.cpp for why we compare big_int-to-big_int in consteval tests.

consteval bool ce_zero_minus_zero() { return (big_int{0} - big_int{0}) == big_int{0}; }
static_assert(ce_zero_minus_zero());

consteval bool ce_small_positive() { return (big_int{5} - big_int{3}) == big_int{2}; }
static_assert(ce_small_positive());

consteval bool ce_small_negative_result() { return (big_int{3} - big_int{5}) == big_int{-2}; }
static_assert(ce_small_negative_result());

consteval bool ce_cancel_to_zero() { return (big_int{7} - big_int{7}) == big_int{0}; }
static_assert(ce_cancel_to_zero());

consteval bool ce_negative_minus_negative() { return (big_int{-5} - big_int{-3}) == big_int{-2}; }
static_assert(ce_negative_minus_negative());

consteval bool ce_negative_minus_positive() { return (big_int{-5} - big_int{3}) == big_int{-8}; }
static_assert(ce_negative_minus_positive());

consteval bool ce_positive_minus_negative() { return (big_int{5} - big_int{-3}) == big_int{8}; }
static_assert(ce_positive_minus_negative());

// ----- runtime tests -----

TEST(Subtraction, SmallPositive) {
    EXPECT_EQ(big_int{5} - big_int{3}, 2);
    EXPECT_EQ(big_int{42} - big_int{40}, 2);
    EXPECT_EQ(big_int{3} - big_int{5}, -2);
}

TEST(Subtraction, SmallNegative) {
    EXPECT_EQ(big_int{-5} - big_int{-3}, -2);
    EXPECT_EQ(big_int{-3} - big_int{-5}, 2);
}

TEST(Subtraction, MixedSigns) {
    EXPECT_EQ(big_int{5} - big_int{-3}, 8);
    EXPECT_EQ(big_int{-5} - big_int{3}, -8);
    EXPECT_EQ(big_int{3} - big_int{-5}, 8);
    EXPECT_EQ(big_int{-3} - big_int{5}, -8);
}

TEST(Subtraction, CancelToExactZeroIsPositive) {
    // x - x must produce +0, never -0.
    const big_int r = big_int{7} - big_int{7};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);

    const big_int r2 = big_int{-100} - big_int{-100};
    EXPECT_EQ(r2, 0);
    EXPECT_FALSE(r2 < 0);
}

TEST(Subtraction, ZeroIdentity) {
    EXPECT_EQ(big_int{0} - big_int{0}, 0);
    EXPECT_EQ(big_int{42} - big_int{0}, 42);
    EXPECT_EQ(big_int{0} - big_int{42}, -42);
    EXPECT_EQ(big_int{-42} - big_int{0}, -42);
    EXPECT_EQ(big_int{0} - big_int{-42}, 42);
}

TEST(Subtraction, BorrowAcrossLimbPromotesToHeap) {
    // 0 - UINT64_MAX requires two limbs' worth of magnitude? No — the magnitude
    // UINT64_MAX fits in one limb. But `(UINT64_MAX + 1) - 1` goes through the
    // carry path and shrinks back. Here we go the opposite way: pick two values
    // whose sum of magnitudes exceeds a single limb.
    const big_int a{std::numeric_limits<std::uint64_t>::max()};
    const big_int b{-1};
    // a - b = a + 1 = 2^64 → two-limb result, must promote to heap.
    const big_int r = a - b;
    EXPECT_EQ(r, big_int{1} << 64);
    EXPECT_FALSE(is_inplace(r));
}

TEST(Subtraction, NoAllocationWhenInlineFits) {
    using big_int_256 = basic_big_int<256>;
    const big_int_256 a{std::numeric_limits<std::uint64_t>::max()};
    const big_int_256 b{-1};
    const big_int_256 r        = a - b; // 2^64 — still fits inline in a 256-bit inline buffer.
    const big_int_256 expected = big_int_256{1} << 64;
    EXPECT_TRUE(is_inplace(r));
    EXPECT_EQ(r, expected);
}

TEST(Subtraction, MultiLimbSameSignTrimsLeadingZeros) {
    // 2^64 - 1 = UINT64_MAX → must trim back to one limb.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 - big_int{1};
    EXPECT_EQ(r, std::numeric_limits<std::uint64_t>::max());
}

TEST(Subtraction, MultiLimbCancelsToZero) {
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 - two_64;
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Subtraction, MultiLimbSameSignCrossesLimbBoundary) {
    // 2^65 - 2^64 = 2^64, spanning a limb transition on the larger side.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int two_65 = two_64 + two_64;
    const big_int r      = two_65 - two_64;
    EXPECT_EQ(r, big_int{1} << 64);
}

TEST(Subtraction, BigIntMinusPrimitiveUnsigned) {
    EXPECT_EQ(big_int{10} - 5U, 5);
    EXPECT_EQ(big_int{5} - 10U, -5);
    EXPECT_EQ(big_int{-10} - 5U, -15);
    EXPECT_EQ(big_int{10} - 0U, 10);
}

TEST(Subtraction, PrimitiveUnsignedMinusBigInt) {
    EXPECT_EQ(10U - big_int{5}, 5);
    EXPECT_EQ(5U - big_int{10}, -5);
    EXPECT_EQ(5U - big_int{-10}, 15);
    EXPECT_EQ(0U - big_int{10}, -10);
}

TEST(Subtraction, BigIntMinusPrimitiveSigned) {
    EXPECT_EQ(big_int{10} - 5, 5);
    EXPECT_EQ(big_int{10} - -5, 15);
    EXPECT_EQ(big_int{-10} - 5, -15);
    EXPECT_EQ(big_int{-10} - -5, -5);
    EXPECT_EQ(big_int{5} - 5, 0);
}

TEST(Subtraction, PrimitiveSignedMinusBigInt) {
    EXPECT_EQ(5 - big_int{10}, -5);
    EXPECT_EQ(-5 - big_int{10}, -15);
    EXPECT_EQ(5 - big_int{-10}, 15);
    EXPECT_EQ(-5 - big_int{-10}, 5);
    EXPECT_EQ(5 - big_int{5}, 0);
}

TEST(Subtraction, PrimitiveBorrowAcrossLimb) {
    // big_int{UINT64_MAX} - (-1) = 2^64 via primitive rhs.
    const big_int r = big_int{std::numeric_limits<std::uint64_t>::max()} - -1;
    EXPECT_EQ(r, big_int{1} << 64);
}

TEST(Subtraction, UnequalLengthMultiLimb) {
    // (2^64 + 10) - 3 = 2^64 + 7: long minus short, same sign, no trim.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int a      = two_64 + big_int{10};
    const big_int r      = a - big_int{3};
    EXPECT_EQ(r, (big_int{1} << 64) + big_int{7});

    // (2^64 + 10) - (-3) = 2^64 + 13: long minus negative short, same-sign add path.
    const big_int r2 = a - big_int{-3};
    EXPECT_EQ(r2, (big_int{1} << 64) + big_int{13});
}

TEST(Subtraction, AgreesWithAdditionOfNegated) {
    // Property: (a - b) == (a + (-b)) for a variety of inputs.
    const big_int cases[] = {
        big_int{0},
        big_int{1},
        big_int{-1},
        big_int{std::numeric_limits<std::uint64_t>::max()},
        big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1},
        -(big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}),
    };
    for (const auto& a : cases) {
        for (const auto& b : cases) {
            EXPECT_EQ(a - b, a + (-b));
        }
    }
}

// ----- rvalue storage-reuse tests -----
// When either operand is an rvalue `basic_big_int`, `operator-` should reuse its
// storage rather than allocating a new result. For rhs reuse we cheaply negate the
// destination and add; for lhs reuse we add with the negated rhs sign.

TEST(Subtraction, RvalueLhsReusesStorage) {
    big_int     a      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    const auto* a_data = a.representation().data();
    ASSERT_FALSE(is_inplace(a));
    const big_int r = std::move(a) - big_int{1}; // 2^64 - 1 trims to one limb
    EXPECT_EQ(r.representation().data(), a_data);
    EXPECT_EQ(r, std::numeric_limits<std::uint64_t>::max());
}

TEST(Subtraction, RvalueRhsReusesStorage) {
    const big_int small{1};
    big_int       b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    const auto*   b_data = b.representation().data();
    ASSERT_FALSE(is_inplace(b));
    const big_int r = small - std::move(b); // 1 - 2^64 = -(2^64 - 1)
    EXPECT_EQ(r.representation().data(), b_data);
    EXPECT_EQ(r, -big_int{std::numeric_limits<std::uint64_t>::max()});
    EXPECT_TRUE(r < 0);
}

TEST(Subtraction, BothRvaluesReusesLhsStorage) {
    big_int       a      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    big_int       b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto*   a_data = a.representation().data();
    const big_int r      = std::move(a) - std::move(b);
    EXPECT_EQ(r.representation().data(), a_data);
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0); // no negative zero
}

TEST(Subtraction, RvalueLhsCancelToZeroIsPositive) {
    // `std::move(a) - copy-of-a` exercises the differing-sign branch within
    // `add_in_place` where `|this| == |other|`; must normalize to +0 even though
    // the destination's sign might momentarily imply "-0" under a naive impl.
    big_int       a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int b = a;
    const big_int r = std::move(a) - b;
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
}

TEST(Subtraction, RvalueRhsNegativeToNegateBranch) {
    // `lvalue_small - std::move(big)` with `|small| < |big|`: rhs-reuse path
    // negates `big` first (cheap XOR), then in-place adds `small`. The add then
    // hits the differing-sign / `|this| > |other|` branch on the negated
    // destination. Uses an lvalue lhs so the rvalue-lhs dispatch branch doesn't
    // short-circuit in first.
    const big_int three{3};
    big_int       b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    const auto*   b_data = b.representation().data();
    const big_int r      = three - std::move(b); // 3 - 2^64 = -(2^64 - 3)
    EXPECT_EQ(r.representation().data(), b_data);
    const big_int expected = -(big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1} - big_int{3});
    EXPECT_EQ(r, expected);
    EXPECT_TRUE(r < 0);
}

TEST(Subtraction, RvalueLhsSmallerMagnitudeTriggersGrow) {
    // `std::move(small) - big` with `|lhs| < |rhs|` exercises `add_in_place`'s
    // `|other| > |this|` branch, which must grow the (inline) destination to
    // fit the larger magnitude before computing `other - this`. Here the grow
    // is required to hold the intermediate two-limb subtraction; the final
    // result trims back to a single limb (`2^64 - 3 = 0xFFFF...FFFD`).
    big_int       a = big_int{3};                                                      // inline, 1 limb
    const big_int b = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    EXPECT_TRUE(is_inplace(a));
    const big_int r        = std::move(a) - b; // 3 - 2^64 = -(2^64 - 3)
    const big_int expected = -(big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1} - big_int{3});
    EXPECT_EQ(r, expected);
    EXPECT_TRUE(r < 0);
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], std::numeric_limits<std::uint64_t>::max() - 2U);
    // The grow left the destination on the heap, even after the trim.
    EXPECT_FALSE(is_inplace(r));
}

TEST(Subtraction, RvaluePrimitiveMix) {
    // rvalue big_int minus primitive: should reuse lhs.
    big_int       a      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto*   a_data = a.representation().data();
    const big_int r      = std::move(a) - 1U;
    EXPECT_EQ(r.representation().data(), a_data);
    EXPECT_EQ(r, std::numeric_limits<std::uint64_t>::max());

    // primitive minus rvalue big_int: should reuse rhs (via negate + add).
    big_int       b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto*   b_data = b.representation().data();
    const big_int r2     = 1 - std::move(b); // 1 - 2^64 = -(2^64 - 1)
    EXPECT_EQ(r2.representation().data(), b_data);
    EXPECT_EQ(r2, -big_int{std::numeric_limits<std::uint64_t>::max()});
    EXPECT_TRUE(r2 < 0);
}

TEST(Subtraction, LvalueFallbackUnchanged) {
    // Pure lvalue/lvalue dispatch: correctness only.
    const big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int b = big_int{std::numeric_limits<std::uint64_t>::max()};
    const big_int r = a - b; // 2^64 - (2^64 - 1) = 1
    EXPECT_EQ(r, 1);
    // Operands untouched.
    EXPECT_EQ(a.representation().size(), 2u);
    EXPECT_EQ(b.representation().size(), 1u);
}

TEST(Subtraction, LvalueCopiesLargerOperand) {
    // When both operands are lvalues, copy whichever has more limbs. For
    // subtraction this means the rhs-copy path (with negate) runs when
    // `|rhs| > |lhs|`, exercising `negate() + add_in_place`.
    const big_int small{7};                                                              // 1 limb
    const big_int big = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2 limbs

    const big_int r1 = small - big; // rhs larger -> copy-and-negate-rhs path
    EXPECT_EQ(r1, -(big - small));
    EXPECT_GE(r1.representation_capacity(), 2u);

    const big_int r2 = big - small; // lhs larger -> copy-lhs path
    EXPECT_EQ(r2, big - small);
    EXPECT_GE(r2.representation_capacity(), 2u);
}

TEST(Subtraction, SmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            const big_int bx{x};
            const big_int by{y};

            /* move_move */ EXPECT_EQ(big_int{x} - big_int{y}, x - y);
            /* move_copy */ EXPECT_EQ(big_int{x} - by, x - y);
            /* copy_move */ EXPECT_EQ(bx - big_int{y}, x - y);
            /* copy_copy */ EXPECT_EQ(bx - by, x - y);
            /* move_int  */ EXPECT_EQ(big_int{x} - y, x - y);
            /* int_move  */ EXPECT_EQ(x - big_int{y}, x - y);
            /* copy_int  */ EXPECT_EQ(bx - y, x - y);
            /* int_copy  */ EXPECT_EQ(x - by, x - y);
        }
    }
}

// ----- compound assignment (operator-=) tests -----

consteval bool ce_minus_equal_small() {
    big_int a{5};
    a -= big_int{3};
    return a == big_int{2};
}
static_assert(ce_minus_equal_small());

consteval bool ce_minus_equal_cancel_to_zero() {
    big_int a{7};
    a -= big_int{7};
    return a == big_int{0};
}
static_assert(ce_minus_equal_cancel_to_zero());

consteval bool ce_minus_equal_self_cancel() {
    big_int a{7};
    a -= a;
    return a == big_int{0};
}
static_assert(ce_minus_equal_self_cancel());

consteval bool ce_minus_equal_negative_result() {
    big_int a{3};
    a -= big_int{5};
    return a == big_int{-2};
}
static_assert(ce_minus_equal_negative_result());

consteval bool ce_minus_equal_positive_minus_negative() {
    big_int a{5};
    a -= big_int{-3};
    return a == big_int{8};
}
static_assert(ce_minus_equal_positive_minus_negative());

TEST(CompoundSubtraction, ReturnsReferenceToSelf) {
    big_int  a{10};
    big_int& r = (a -= big_int{3});
    EXPECT_EQ(&r, &a);
    EXPECT_EQ(a, 7);
}

TEST(CompoundSubtraction, SmallPositive) {
    big_int a{5};
    a -= big_int{3};
    EXPECT_EQ(a, 2);

    big_int b{3};
    b -= big_int{5};
    EXPECT_EQ(b, -2);
}

TEST(CompoundSubtraction, SmallNegative) {
    big_int a{-5};
    a -= big_int{-3};
    EXPECT_EQ(a, -2);

    big_int b{-3};
    b -= big_int{-5};
    EXPECT_EQ(b, 2);
}

TEST(CompoundSubtraction, MixedSigns) {
    big_int a{5};
    a -= big_int{-3};
    EXPECT_EQ(a, 8);

    big_int b{-5};
    b -= big_int{3};
    EXPECT_EQ(b, -8);
}

TEST(CompoundSubtraction, CancelToExactZeroIsPositive) {
    big_int a{7};
    a -= big_int{7};
    EXPECT_EQ(a, 0);
    EXPECT_FALSE(a < 0);

    big_int b{-100};
    b -= big_int{-100};
    EXPECT_EQ(b, 0);
    EXPECT_FALSE(b < 0);
}

TEST(CompoundSubtraction, ZeroIdentity) {
    big_int a{42};
    a -= big_int{0};
    EXPECT_EQ(a, 42);

    big_int b{0};
    b -= big_int{42};
    EXPECT_EQ(b, -42);

    big_int c{0};
    c -= big_int{-42};
    EXPECT_EQ(c, 42);
}

TEST(CompoundSubtraction, Self) {
    big_int a{5};
    a -= a;
    EXPECT_EQ(a, 0);

    big_int ma{-5};
    ma -= ma;
    EXPECT_EQ(ma, 0);

    big_int b{0x1p128};
    b -= b;
    EXPECT_EQ(b, 0);

    big_int mb{-0x1p128};
    mb -= mb;
    EXPECT_EQ(mb, 0);
}

TEST(CompoundSubtraction, BorrowAcrossLimbPromotesToHeap) {
    big_int a{std::numeric_limits<std::uint64_t>::max()};
    a -= big_int{-1}; // UINT64_MAX + 1 = 2^64
    ASSERT_EQ(a.representation().size(), 2u);
    EXPECT_EQ(a.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(a.representation()[1], uint_multiprecision_t{1});
    EXPECT_FALSE(is_inplace(a));
}

TEST(CompoundSubtraction, MultiLimbShrinksToSingleLimb) {
    // (2^64) - 1 = UINT64_MAX, trims the now-zero upper limb.
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    ASSERT_EQ(a.representation().size(), 2u);
    a -= big_int{1};
    ASSERT_EQ(a.representation().size(), 1u);
    EXPECT_EQ(a.representation()[0], std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(a < 0);
}

TEST(CompoundSubtraction, PrimitiveUnsigned) {
    big_int a{10};
    a -= 5U;
    EXPECT_EQ(a, 5);

    big_int b{3};
    b -= 5U;
    EXPECT_EQ(b, -2);

    big_int c{-10};
    c -= 5U;
    EXPECT_EQ(c, -15);
}

TEST(CompoundSubtraction, PrimitiveSigned) {
    big_int a{10};
    a -= 5;
    EXPECT_EQ(a, 5);

    big_int b{10};
    b -= -5;
    EXPECT_EQ(b, 15);

    big_int c{-10};
    c -= -5;
    EXPECT_EQ(c, -5);

    big_int d{5};
    d -= 5;
    EXPECT_EQ(d, 0);
    EXPECT_FALSE(d < 0);
}

TEST(CompoundSubtraction, PrimitiveBorrowFromUpperLimb) {
    // 2^64 - 1 must trim the upper limb.
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    a -= 1U;
    ASSERT_EQ(a.representation().size(), 1u);
    EXPECT_EQ(a.representation()[0], std::numeric_limits<std::uint64_t>::max());
}

TEST(CompoundSubtraction, SelfSubtractYieldsZero) {
    // `a -= a` must correctly yield `0` even though `rhs.representation()`
    // aliases `*this`'s own limbs.
    big_int a{42};
    a -= a;
    EXPECT_EQ(a, 0);
    EXPECT_FALSE(a < 0);

    big_int b = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    b -= b;
    EXPECT_EQ(b, 0);
    EXPECT_FALSE(b < 0);
}

TEST(CompoundSubtraction, NoAllocationWhenInlineFits) {
    using big_int_256 = basic_big_int<256>;
    big_int_256 a{10};
    a -= big_int_256{3};
    EXPECT_EQ(a, 7);
    EXPECT_TRUE(is_inplace(a));
}

TEST(CompoundSubtraction, ChainedAccumulation) {
    big_int a{100};
    for (int i = 1; i <= 10; ++i) {
        a -= big_int{i};
    }
    EXPECT_EQ(a, 45);
}

// ----- detail::subtract_unsigned_spans_borrow_out -----

inline constexpr uint_multiprecision_t limb_max = std::numeric_limits<uint_multiprecision_t>::max();

consteval bool ce_borrow_out_wraps() {
    // 1 - 2 == B - 1 with borrow out.
    uint_multiprecision_t       a[1] = {1};
    const uint_multiprecision_t b[1] = {2};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{a}, std::span{a}, std::span{b});
    return borrow && a[0] == limb_max;
}
static_assert(ce_borrow_out_wraps());

TEST(SubtractBorrowOut, NoBorrowMatchesPlainSubtract) {
    const uint_multiprecision_t a[3] = {5, 7, 9};
    const uint_multiprecision_t b[3] = {1, 2, 3};
    uint_multiprecision_t       expected[3]{};
    uint_multiprecision_t       actual[3]{};

    BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans(std::span{expected}, std::span{a}, std::span{b});
    const bool borrow = BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(
        std::span{actual}, std::span{a}, std::span{b});

    EXPECT_FALSE(borrow);
    EXPECT_TRUE(std::ranges::equal(std::span{expected}, std::span{actual}));
}

TEST(SubtractBorrowOut, EqualValuesYieldZeroNoBorrow) {
    const uint_multiprecision_t a[2] = {limb_max, 42};
    uint_multiprecision_t       r[2]{};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{r}, std::span{a}, std::span{a});
    EXPECT_FALSE(borrow);
    EXPECT_EQ(r[0], 0u);
    EXPECT_EQ(r[1], 0u);
}

TEST(SubtractBorrowOut, BorrowWrapsMultiLimb) {
    // a = B, b = B + 1: a - b == -1 == B^2 - 1 (mod B^2) with borrow out.
    const uint_multiprecision_t a[2] = {0, 1};
    const uint_multiprecision_t b[2] = {1, 1};
    uint_multiprecision_t       r[2]{};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{r}, std::span{a}, std::span{b});
    EXPECT_TRUE(borrow);
    EXPECT_EQ(r[0], limb_max);
    EXPECT_EQ(r[1], limb_max);
}

TEST(SubtractBorrowOut, RaggedSizesBorrowRipplesThroughTail) {
    // a = B^2, b = 1: result B^2 - 1, no borrow out.
    const uint_multiprecision_t a[3] = {0, 0, 1};
    const uint_multiprecision_t b[1] = {1};
    uint_multiprecision_t       r[3]{};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{r}, std::span{a}, std::span{b});
    EXPECT_FALSE(borrow);
    EXPECT_EQ(r[0], limb_max);
    EXPECT_EQ(r[1], limb_max);
    EXPECT_EQ(r[2], 0u);
}

TEST(SubtractBorrowOut, RaggedSizesFullSpanBorrow) {
    // a = 0 (three limbs), b = 1: wraps to B^3 - 1 with borrow out.
    const uint_multiprecision_t a[3] = {0, 0, 0};
    const uint_multiprecision_t b[1] = {1};
    uint_multiprecision_t       r[3]{};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{r}, std::span{a}, std::span{b});
    EXPECT_TRUE(borrow);
    EXPECT_EQ(r[0], limb_max);
    EXPECT_EQ(r[1], limb_max);
    EXPECT_EQ(r[2], limb_max);
}

TEST(SubtractBorrowOut, ResultMayAliasA) {
    uint_multiprecision_t       a[2] = {3, 5};
    const uint_multiprecision_t b[2] = {4, 5};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{a}, std::span{a}, std::span{b});
    EXPECT_TRUE(borrow);
    EXPECT_EQ(a[0], limb_max);
    EXPECT_EQ(a[1], limb_max);
}

TEST(SubtractBorrowOut, RoundTripWithAddRecoversOperand) {
    // (a - b) + b == a (mod B^n) and the borrow/carry flags match.
    const uint_multiprecision_t a[3] = {0x1234, 0, 0x5678};
    const uint_multiprecision_t b[3] = {limb_max, limb_max, 0x9abc};
    uint_multiprecision_t       r[3]{};
    const bool                  borrow =
        BEMAN_BIG_INT_NAMESPACE::detail::subtract_unsigned_spans_borrow_out(std::span{r}, std::span{a}, std::span{b});
    const bool carry = BEMAN_BIG_INT_NAMESPACE::detail::add_unsigned_spans(std::span{r}, std::span{r}, std::span{b});
    EXPECT_EQ(borrow, carry);
    EXPECT_TRUE(std::ranges::equal(std::span{r}, std::span{a}));
}

} // namespace
