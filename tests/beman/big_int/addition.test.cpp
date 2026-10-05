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
// Note: we compare big_int-to-big_int rather than big_int-to-primitive to avoid
// the `_BitInt` bit_cast path that some toolchains don't yet support in constant
// evaluation. Sign / storage-promotion properties are covered by the runtime tests.

consteval bool ce_zero_plus_zero() { return (big_int{0} + big_int{0}) == big_int{0}; }
static_assert(ce_zero_plus_zero());

consteval bool ce_small_positive() { return (big_int{1} + big_int{2}) == big_int{3}; }
static_assert(ce_small_positive());

consteval bool ce_cancel_to_zero() { return (big_int{-5} + big_int{5}) == big_int{0}; }
static_assert(ce_cancel_to_zero());

consteval bool ce_mixed_sign_positive_result() { return (big_int{5} + big_int{-3}) == big_int{2}; }
static_assert(ce_mixed_sign_positive_result());

consteval bool ce_mixed_sign_negative_result() { return (big_int{-5} + big_int{3}) == big_int{-2}; }
static_assert(ce_mixed_sign_negative_result());

consteval bool ce_same_sign_negative() { return (big_int{-5} + big_int{-3}) == big_int{-8}; }
static_assert(ce_same_sign_negative());

// ----- runtime tests -----

TEST(Addition, SmallPositivePositive) {
    EXPECT_EQ(big_int{1} + big_int{2}, 3);
    EXPECT_EQ(big_int{40} + big_int{2}, 42);
}

TEST(Addition, SmallNegativeNegative) {
    EXPECT_EQ(big_int{-1} + big_int{-2}, -3);
    EXPECT_EQ(big_int{-40} + big_int{-2}, -42);
}

TEST(Addition, MixedSigns) {
    EXPECT_EQ(big_int{-5} + big_int{3}, -2);
    EXPECT_EQ(big_int{5} + big_int{-3}, 2);
    EXPECT_EQ(big_int{3} + big_int{-5}, -2);
    EXPECT_EQ(big_int{-3} + big_int{5}, 2);
}

TEST(Addition, CancelToExactZeroIsPositive) {
    // The "no negative zero" invariant must hold: -a + a must be +0.
    const big_int r = big_int{-7} + big_int{7};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);

    const big_int r2 = big_int{100} + big_int{-100};
    EXPECT_EQ(r2, 0);
    EXPECT_FALSE(r2 < 0);
}

TEST(Addition, ZeroIdentity) {
    EXPECT_EQ(big_int{0} + big_int{0}, 0);
    EXPECT_EQ(big_int{42} + big_int{0}, 42);
    EXPECT_EQ(big_int{0} + big_int{42}, 42);
    EXPECT_EQ(big_int{-42} + big_int{0}, -42);
    EXPECT_EQ(big_int{0} + big_int{-42}, -42);
}

TEST(Addition, CarryOutOfTopLimbPromotesToHeap) {
    // `basic_big_int<64>` with a 64-bit limb has exactly one inline limb.
    // UINT64_MAX + 1 requires two limbs, so the result must live on the heap.
    const big_int a{std::numeric_limits<std::uint64_t>::max()};
    const big_int b{1};
    ASSERT_TRUE(a.representation().size() == 1);
    const big_int r = a + b;
    EXPECT_EQ(r, big_int{1} << 64);
    EXPECT_FALSE(r < 0);
    // Storage should have promoted out of inline.
    EXPECT_FALSE(is_inplace(r));
}

TEST(Addition, NoAllocationWhenInlineFits) {
    // `basic_big_int<256>` has at least 4 inline limbs on any supported limb width.
    // Small-value sum must remain in inline storage (capacity() == 0).
    using big_int_256 = basic_big_int<256>;
    const big_int_256 a{1};
    const big_int_256 b{2};
    const big_int_256 r = a + b;
    EXPECT_EQ(r, 3);
    EXPECT_TRUE(is_inplace(r));

    // Even with both operands near the inline limit (but no carry out of
    // the top inline limb), we should not allocate.
    const big_int_256 c{std::numeric_limits<std::uint64_t>::max()};
    const big_int_256 d{1};
    const big_int_256 s        = c + d;
    const big_int_256 expected = big_int_256{1} << 64;
    EXPECT_TRUE(is_inplace(s));
    EXPECT_EQ(s, expected);
}

TEST(Addition, MultiLimbValuesSameSign) {
    // Build 2^64 via UINT64_MAX + 1, then add another 2^64 to reach 2^65.
    const big_int two_64     = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int two_64_alt = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int two_65     = two_64 + two_64_alt;
    EXPECT_EQ(two_65, big_int{1} << 65);
}

TEST(Addition, MultiLimbOppositeSignTrimsLeadingZeros) {
    // 2^64 + (-1) = 2^64 - 1 = UINT64_MAX, which fits in a single limb.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    ASSERT_EQ(two_64.representation().size(), 2u);

    const big_int r = two_64 + big_int{-1};
    EXPECT_EQ(r, std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(r < 0);
}

TEST(Addition, MultiLimbSubtractionToZero) {
    // 2^64 + (-2^64) = 0 → must produce +0 with limb_count == 1.
    const big_int two_64     = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int neg_two_64 = -two_64;
    const big_int r          = two_64 + neg_two_64;
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Addition, BigIntPlusPrimitiveUnsigned) {
    EXPECT_EQ(big_int{10} + 5U, 15);
    EXPECT_EQ(big_int{-10} + 5U, -5);
    EXPECT_EQ(big_int{-10} + 20U, 10);
    EXPECT_EQ(big_int{10} + 0U, 10);
}

TEST(Addition, PrimitiveUnsignedPlusBigInt) {
    EXPECT_EQ(5U + big_int{10}, 15);
    EXPECT_EQ(5U + big_int{-10}, -5);
    EXPECT_EQ(20U + big_int{-10}, 10);
    EXPECT_EQ(0U + big_int{10}, 10);
}

TEST(Addition, BigIntPlusPrimitiveSigned) {
    EXPECT_EQ(big_int{10} + 5, 15);
    EXPECT_EQ(big_int{10} + -5, 5);
    EXPECT_EQ(big_int{-10} + 5, -5);
    EXPECT_EQ(big_int{-10} + -5, -15);
    EXPECT_EQ(big_int{5} + -5, 0);
}

TEST(Addition, PrimitiveSignedPlusBigInt) {
    EXPECT_EQ(5 + big_int{10}, 15);
    EXPECT_EQ(-5 + big_int{10}, 5);
    EXPECT_EQ(5 + big_int{-10}, -5);
    EXPECT_EQ(-5 + big_int{-10}, -15);
    EXPECT_EQ(-5 + big_int{5}, 0);
}

TEST(Addition, PrimitiveCarryIntoUpperLimb) {
    // big_int{UINT64_MAX} + 1U must promote to two limbs.
    const big_int r = big_int{std::numeric_limits<std::uint64_t>::max()} + 1U;
    EXPECT_EQ(r, big_int{1} << 64);
}

TEST(Addition, NegativePrimitiveIntoMultiLimb) {
    // 2^64 + (-1) via primitive operand.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 + -1;
    EXPECT_EQ(r, std::numeric_limits<std::uint64_t>::max());
}

TEST(Addition, UnequalLengthMultiLimb) {
    // (2^64 + 7) + 3 = 2^64 + 10, spanning two limbs on the left and one on the right.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int a      = two_64 + big_int{7};
    const big_int r      = a + big_int{3};
    EXPECT_EQ(r, (big_int{1} << 64) + big_int{10});

    // Same but with a negative short operand: (2^64 + 7) + (-3) = 2^64 + 4.
    const big_int r2 = a + big_int{-3};
    EXPECT_EQ(r2, (big_int{1} << 64) + big_int{4});
}

// ----- rvalue storage-reuse tests -----
// When either operand is an rvalue `basic_big_int`, `operator+` should steal its
// storage instead of allocating a new result buffer. Correctness is checked across
// all (lvalue, rvalue) combinations; heap reuse is verified by comparing the data
// pointer before and after the sum.

TEST(Addition, RvalueLhsReusesStorage) {
    // Heap-allocated `a`; `std::move(a) + small` must write the result into `a`'s buffer.
    big_int     a      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64, two limbs
    const auto* a_data = a.representation().data();
    ASSERT_FALSE(is_inplace(a)); // heap-allocated
    const big_int r = std::move(a) + big_int{5};
    EXPECT_EQ(r.representation().data(), a_data);
    EXPECT_EQ(r, (big_int{1} << 64) + big_int{5});
}

TEST(Addition, RvalueRhsReusesStorage) {
    // Commutative path: lhs is lvalue, rhs is rvalue; rhs's buffer should be reused.
    const big_int small{5};
    big_int       b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto*   b_data = b.representation().data();
    ASSERT_FALSE(is_inplace(b));
    const big_int r = small + std::move(b);
    EXPECT_EQ(r.representation().data(), b_data);
    EXPECT_EQ(r, (big_int{1} << 64) + big_int{5});
}

TEST(Addition, BothRvaluesReusesLhsStorage) {
    // When both operands are rvalues, lhs is preferred per dispatch order.
    big_int     a      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    big_int     b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto* a_data = a.representation().data();
    ASSERT_FALSE(is_inplace(a));
    ASSERT_FALSE(is_inplace(b));
    const big_int r = std::move(a) + std::move(b);
    EXPECT_EQ(r.representation().data(), a_data);
    EXPECT_EQ(r, big_int{1} << 65);
}

TEST(Addition, RvalueCarryGrowsStorage) {
    // Same-sign add where the ripple carry forces one extra limb beyond the
    // rvalue's current capacity. The helper's `grow(big+1)` path must handle this.
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()}; // 1 limb, inline
    EXPECT_TRUE(is_inplace(a));
    const big_int r = std::move(a) + big_int{1}; // 2^64 requires 2 limbs
    EXPECT_EQ(r, big_int{1} << 64);
}

TEST(Addition, RvalueCancelToZeroIsPositive) {
    // Differing-sign, equal-magnitude rvalue add: must produce +0, never -0.
    big_int       a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    const big_int b = -a;                                                              // -2^64
    const big_int r = std::move(a) + b;
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Addition, RvalueLhsSmallerMagnitudeThanRhs) {
    // Differing-sign, `|lhs| < |rhs|`: exercises `add_in_place`'s third branch,
    // which must rewrite lhs's buffer as `other - this` and adopt rhs's sign.
    big_int       a        = big_int{5};                                                // 1 limb
    const big_int b        = -(big_int{std::numeric_limits<std::uint64_t>::max()} + 1); // -2^64
    const big_int r        = std::move(a) + b;                                          // 5 + (-2^64) = -(2^64 - 5)
    const big_int expected = -(big_int{std::numeric_limits<std::uint64_t>::max()} + 1 - big_int{5});
    EXPECT_EQ(r, expected);
    EXPECT_TRUE(r < 0);
}

TEST(Addition, RvaluePrimitiveMix) {
    // rvalue big_int + primitive: should reuse lhs.
    big_int       a      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto*   a_data = a.representation().data();
    const big_int r      = std::move(a) + 5U;
    EXPECT_EQ(r.representation().data(), a_data);
    EXPECT_EQ(r, (big_int{1} << 64) + big_int{5});

    // primitive + rvalue big_int: should reuse rhs.
    big_int       b      = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const auto*   b_data = b.representation().data();
    const big_int r2     = 7 + std::move(b);
    EXPECT_EQ(r2.representation().data(), b_data);
    EXPECT_EQ(r2, (big_int{1} << 64) + big_int{7});
}

TEST(Addition, LvalueFallbackUnchanged) {
    // Pure lvalue/lvalue dispatch: correctness only (destination selection is
    // covered by LvalueCopiesLargerOperand).
    const big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int b = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r = a + b;
    EXPECT_EQ(r, big_int{1} << 65);
    // Operands untouched.
    EXPECT_EQ(a.representation().size(), 2u);
    EXPECT_EQ(b.representation().size(), 2u);
}

TEST(Addition, LvalueHeapReservesCarryHeadroom) {
    // `a + b` with two lvalue heap operands copies the larger operand with one
    // extra limb of headroom so that a carry-out never triggers a second
    // allocation.
    const big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64, 2 limbs
    const big_int b = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    ASSERT_EQ(a.representation().size(), 2U);
    ASSERT_EQ(b.representation().size(), 2U);

    const big_int r = a + b; // 2 * 2^64 -- limbs [0, 2], still 2 limbs but room for 3.
    ASSERT_EQ(r.representation().size(), 2U);
    EXPECT_GE(r.representation_capacity(), 3U);
}

TEST(Addition, InlineInlineNoCarryStaysInline) {
    // When both lvalue operands fit inline and the ripple-carry doesn't
    // overflow, the result must stay in inline storage. `assign_value` without
    // extra headroom keeps the copy inline; `add_in_place` only allocates
    // if a carry actually escapes.
    using big_int_256 = basic_big_int<256>;
    const big_int_256 a{5};
    const big_int_256 b{7};
    ASSERT_TRUE(is_inplace(a));
    ASSERT_TRUE(is_inplace(b));
    const big_int_256 r = a + b;
    EXPECT_EQ(r, 12);
    EXPECT_TRUE(is_inplace(r)); // still inline -- no speculative heap allocation

    // Boundary case for the default `big_int` (basic_big_int<64>, inplace_limbs=1):
    // both inline, no carry => stays inline. This is the specific pessimization
    // that would appear if assign_value(..., 1) were called unconditionally.
    const big_int c{5};
    const big_int d{7};
    ASSERT_TRUE(is_inplace(c));
    ASSERT_TRUE(is_inplace(d));
    const big_int r2 = c + d;
    EXPECT_EQ(r2, 12);
    EXPECT_TRUE(is_inplace(r2)); // still inline
}

TEST(Addition, SmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            const big_int bx{x};
            const big_int by{y};

            /* move_move */ EXPECT_EQ(big_int{x} + big_int{y}, x + y);
            /* move_copy */ EXPECT_EQ(big_int{x} + by, x + y);
            /* copy_move */ EXPECT_EQ(bx + big_int{y}, x + y);
            /* copy_copy */ EXPECT_EQ(bx + by, x + y);
            /* move_int  */ EXPECT_EQ(big_int{x} + y, x + y);
            /* int_move  */ EXPECT_EQ(x + big_int{y}, x + y);
            /* copy_int  */ EXPECT_EQ(bx + y, x + y);
            /* int_copy  */ EXPECT_EQ(x + by, x + y);
        }
    }
}

// ----- compound assignment (operator+=) tests -----

consteval bool ce_plus_equal_small() {
    big_int a{1};
    a += big_int{2};
    return a == big_int{3};
}
static_assert(ce_plus_equal_small());

consteval bool ce_plus_equal_cancel_to_zero() {
    big_int a{-5};
    a += big_int{5};
    return a == big_int{0};
}
static_assert(ce_plus_equal_cancel_to_zero());

consteval bool ce_plus_equal_mixed_sign() {
    big_int a{-5};
    a += big_int{3};
    return a == big_int{-2};
}
static_assert(ce_plus_equal_mixed_sign());

TEST(CompoundAddition, ReturnsReferenceToSelf) {
    big_int  a{10};
    big_int& r = (a += big_int{5});
    EXPECT_EQ(&r, &a);
    EXPECT_EQ(a, 15);
}

TEST(CompoundAddition, SmallPositivePositive) {
    big_int a{1};
    a += big_int{2};
    EXPECT_EQ(a, 3);

    big_int b{40};
    b += big_int{2};
    EXPECT_EQ(b, 42);
}

TEST(CompoundAddition, SmallNegativeNegative) {
    big_int a{-1};
    a += big_int{-2};
    EXPECT_EQ(a, -3);
}

TEST(CompoundAddition, MixedSigns) {
    big_int a{5};
    a += big_int{-3};
    EXPECT_EQ(a, 2);

    big_int b{-5};
    b += big_int{3};
    EXPECT_EQ(b, -2);

    big_int c{3};
    c += big_int{-5};
    EXPECT_EQ(c, -2);
}

TEST(CompoundAddition, CancelToExactZeroIsPositive) {
    big_int a{-7};
    a += big_int{7};
    EXPECT_EQ(a, 0);
    EXPECT_FALSE(a < 0);
}

TEST(CompoundAddition, ZeroIdentity) {
    big_int a{42};
    a += big_int{0};
    EXPECT_EQ(a, 42);

    big_int b{0};
    b += big_int{-42};
    EXPECT_EQ(b, -42);
}

TEST(CompoundAddition, CarryOutOfTopLimbPromotesToHeap) {
    big_int a{std::numeric_limits<std::uint64_t>::max()};
    ASSERT_EQ(a.representation().size(), 1u);
    a += big_int{1};
    ASSERT_EQ(a.representation().size(), 2u);
    EXPECT_EQ(a.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(a.representation()[1], uint_multiprecision_t{1});
    EXPECT_FALSE(is_inplace(a));
}

TEST(CompoundAddition, NoAllocationWhenInlineFits) {
    using big_int_256 = basic_big_int<256>;
    big_int_256 a{1};
    a += big_int_256{2};
    EXPECT_EQ(a, 3);
    EXPECT_TRUE(is_inplace(a));
}

TEST(CompoundAddition, MultiLimbOppositeSignTrimsLeadingZeros) {
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    ASSERT_EQ(a.representation().size(), 2u);
    a += big_int{-1};
    ASSERT_EQ(a.representation().size(), 1u);
    EXPECT_EQ(a.representation()[0], std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(a < 0);
}

TEST(CompoundAddition, PrimitiveUnsigned) {
    big_int a{10};
    a += 5U;
    EXPECT_EQ(a, 15);

    big_int b{-10};
    b += 5U;
    EXPECT_EQ(b, -5);

    big_int c{-10};
    c += 20U;
    EXPECT_EQ(c, 10);
}

TEST(CompoundAddition, PrimitiveSigned) {
    big_int a{10};
    a += 5;
    EXPECT_EQ(a, 15);

    big_int b{10};
    b += -5;
    EXPECT_EQ(b, 5);

    big_int c{-10};
    c += -5;
    EXPECT_EQ(c, -15);

    big_int d{5};
    d += -5;
    EXPECT_EQ(d, 0);
    EXPECT_FALSE(d < 0);
}

TEST(CompoundAddition, PrimitiveCarryIntoUpperLimb) {
    big_int a{std::numeric_limits<std::uint64_t>::max()};
    a += 1U;
    ASSERT_EQ(a.representation().size(), 2u);
    EXPECT_EQ(a.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(a.representation()[1], uint_multiprecision_t{1});
}

TEST(CompoundAddition, SelfAddDoublesValue) {
    // `a += a` must correctly yield `2 * a` even though `rhs.representation()`
    // aliases `*this`'s own limbs.
    big_int a{7};
    a += a;
    EXPECT_EQ(a, 14);

    big_int b = big_int{std::numeric_limits<std::uint64_t>::max()};
    b += b;
    // 2 * (2^64 - 1) = 2^65 - 2 = [UINT64_MAX - 1, 1]
    ASSERT_EQ(b.representation().size(), 2u);
    EXPECT_EQ(b.representation()[0], std::numeric_limits<std::uint64_t>::max() - 1);
    EXPECT_EQ(b.representation()[1], uint_multiprecision_t{1});
}

TEST(CompoundAddition, ChainedAccumulation) {
    big_int a{0};
    for (int i = 1; i <= 10; ++i) {
        a += big_int{i};
    }
    EXPECT_EQ(a, 55);
}

TEST(Addition, LvalueCopiesLargerOperand) {
    // When both operands are lvalues, the implementation should copy whichever
    // has more limbs so that add_in_place does not need to grow. We can't peek
    // at the source of the copy directly, but we can observe that the result's
    // allocated capacity matches the larger operand's limb count (heap copy
    // allocates exactly `source.limb_count()` limbs).
    const big_int small{7};                                                              // 1 limb, inline
    const big_int big = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2 limbs, heap
    ASSERT_EQ(small.representation().size(), 1u);
    ASSERT_EQ(big.representation().size(), 2u);

    // `small + big` must copy `big` (not `small`); the result should already
    // have room for the two-limb value without growing.
    const big_int r1 = small + big;
    EXPECT_EQ(r1, big + small);
    EXPECT_GE(r1.representation_capacity(), 2u);

    // Symmetric case: `big + small` copies `big` too.
    const big_int r2 = big + small;
    EXPECT_EQ(r2, big + small);
    EXPECT_GE(r2.representation_capacity(), 2u);
}

} // namespace
