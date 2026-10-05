// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::detail::int_multiprecision_t;

TEST(Comparison, EqualitySmallIntegers) {
    const big_int zero{0};
    const big_int one{1};
    const big_int neg_one{-1};

    EXPECT_EQ(zero, zero);
    EXPECT_EQ(zero, 0U);
    EXPECT_EQ(0U, zero);
    EXPECT_EQ(zero, 0);
    EXPECT_EQ(0, zero);

    EXPECT_NE(zero, one);
    EXPECT_NE(zero, 1U);
    EXPECT_NE(0U, one);
    EXPECT_NE(zero, 1);
    EXPECT_NE(0, one);

    EXPECT_NE(one, zero);
    EXPECT_NE(one, 0U);
    EXPECT_NE(1U, zero);
    EXPECT_NE(one, 0);
    EXPECT_NE(1, zero);

    EXPECT_EQ(one, one);
    EXPECT_EQ(one, 1U);
    EXPECT_EQ(1U, one);
    EXPECT_EQ(one, 1);
    EXPECT_EQ(1, one);

    EXPECT_NE(one, neg_one);
    EXPECT_NE(one, -1);
    EXPECT_NE(1U, neg_one);
    EXPECT_NE(1, neg_one);

    EXPECT_NE(neg_one, one);
    EXPECT_NE(neg_one, 1U);
    EXPECT_NE(neg_one, 1);
    EXPECT_NE(-1, one);

    EXPECT_EQ(neg_one, neg_one);
    EXPECT_EQ(neg_one, -1);
    EXPECT_EQ(-1, neg_one);
}

TEST(Comparison, EqualityWideIntegers) {
    const big_int wide_one{1.0e20};
    const big_int wide_two{2.0e20};
    const big_int wide_neg = -wide_one;

    EXPECT_EQ(wide_one, wide_one);
    EXPECT_EQ(wide_two, wide_two);
    EXPECT_NE(wide_one, wide_two);
    EXPECT_NE(wide_two, wide_one);
    EXPECT_NE(wide_one, 1U);
    EXPECT_NE(wide_one, big_int{1U});
    EXPECT_NE(1U, wide_one);
    EXPECT_NE(big_int{1U}, wide_one);
    EXPECT_NE(1, wide_one);
    EXPECT_NE(big_int{1}, wide_one);
    EXPECT_NE(wide_one, wide_neg);
    EXPECT_NE(wide_neg, wide_one);
    EXPECT_EQ(wide_neg, -wide_one);
    EXPECT_EQ(-wide_one, wide_neg);
}

TEST(Comparison, OrderingSmallIntegers) {
    constexpr big_int zero{0};
    constexpr big_int one{1};
    constexpr big_int two{2};
    constexpr big_int neg_one{-1};

    EXPECT_EQ((zero <=> zero), std::strong_ordering::equal);
    EXPECT_EQ((zero <=> 0u), std::strong_ordering::equal);
    EXPECT_EQ((0u <=> zero), std::strong_ordering::equal);
    EXPECT_EQ((zero <=> 0), std::strong_ordering::equal);
    EXPECT_EQ((0 <=> zero), std::strong_ordering::equal);

    EXPECT_EQ((zero <=> one), std::strong_ordering::less);
    EXPECT_EQ((zero <=> 1u), std::strong_ordering::less);
    EXPECT_EQ((0u <=> one), std::strong_ordering::less);
    EXPECT_EQ((zero <=> 1), std::strong_ordering::less);
    EXPECT_EQ((0 <=> one), std::strong_ordering::less);

    EXPECT_EQ((one <=> zero), std::strong_ordering::greater);
    EXPECT_EQ((one <=> 0u), std::strong_ordering::greater);
    EXPECT_EQ((1u <=> zero), std::strong_ordering::greater);
    EXPECT_EQ((one <=> 0), std::strong_ordering::greater);
    EXPECT_EQ((1 <=> zero), std::strong_ordering::greater);

    EXPECT_EQ((one <=> one), std::strong_ordering::equal);
    EXPECT_EQ((one <=> 1u), std::strong_ordering::equal);
    EXPECT_EQ((1U <=> one), std::strong_ordering::equal);
    EXPECT_EQ((one <=> 1), std::strong_ordering::equal);
    EXPECT_EQ((1 <=> one), std::strong_ordering::equal);

    EXPECT_EQ((one <=> two), std::strong_ordering::less);
    EXPECT_EQ((one <=> 2u), std::strong_ordering::less);
    EXPECT_EQ((1u <=> two), std::strong_ordering::less);
    EXPECT_EQ((one <=> 2), std::strong_ordering::less);
    EXPECT_EQ((1 <=> two), std::strong_ordering::less);

    EXPECT_EQ((two <=> one), std::strong_ordering::greater);
    EXPECT_EQ((two <=> 1u), std::strong_ordering::greater);
    EXPECT_EQ((2u <=> one), std::strong_ordering::greater);
    EXPECT_EQ((two <=> 1), std::strong_ordering::greater);
    EXPECT_EQ((2 <=> one), std::strong_ordering::greater);

    EXPECT_EQ((one <=> neg_one), std::strong_ordering::greater);
    EXPECT_EQ((one <=> -1), std::strong_ordering::greater);
    EXPECT_EQ((1u <=> neg_one), std::strong_ordering::greater);
    EXPECT_EQ((1 <=> neg_one), std::strong_ordering::greater);

    EXPECT_EQ((neg_one <=> one), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 1u), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 1), std::strong_ordering::less);
    EXPECT_EQ((-1 <=> one), std::strong_ordering::less);

    EXPECT_EQ((neg_one <=> two), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 2u), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 2), std::strong_ordering::less);
    EXPECT_EQ((-1 <=> two), std::strong_ordering::less);

    EXPECT_EQ((neg_one <=> -1), std::strong_ordering::equal);
    EXPECT_EQ((-1 <=> neg_one), std::strong_ordering::equal);
}

TEST(Comparison, OrderingWideIntegers) {
    const big_int wide_one{1.0e20};
    const big_int wide_two{2.0e20};
    const big_int wide_neg = -wide_one;

    EXPECT_EQ((wide_one <=> wide_one), std::strong_ordering::equal);
    EXPECT_EQ((wide_two <=> wide_two), std::strong_ordering::equal);
    EXPECT_EQ((wide_one <=> wide_two), std::strong_ordering::less);
    EXPECT_EQ((wide_two <=> wide_one), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> 1U), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> big_int{1U}), std::strong_ordering::greater);
    EXPECT_EQ((1U <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((big_int{1U} <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((wide_one <=> 1), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> big_int{1}), std::strong_ordering::greater);
    EXPECT_EQ((1 <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((big_int{1} <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((wide_one <=> -1), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> big_int{-1}), std::strong_ordering::greater);
    EXPECT_EQ((-1 <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((big_int{-1} <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((wide_neg <=> 1), std::strong_ordering::less);
    EXPECT_EQ((wide_neg <=> big_int{1}), std::strong_ordering::less);
    EXPECT_EQ((1 <=> wide_neg), std::strong_ordering::greater);
    EXPECT_EQ((big_int{1} <=> wide_neg), std::strong_ordering::greater);
}

// Covers same-sign negative operands of differing magnitude, which the other
// comparison suites do not exercise.
TEST(Comparison, EqualityNegativeMagnitudes) {
    const big_int neg_three{-3};
    const big_int neg_five{-5};
    const big_int neg_three_copy{-3};

    EXPECT_EQ(neg_three, neg_three_copy);
    EXPECT_NE(neg_three, neg_five);
    EXPECT_NE(neg_five, neg_three);
    EXPECT_EQ(neg_three, -3);
    EXPECT_NE(neg_three, -5);
    EXPECT_EQ(-3, neg_three);
    EXPECT_NE(-5, neg_three);
}

TEST(Comparison, OrderingNegativeMagnitudes) {
    const big_int neg_three{-3};
    const big_int neg_five{-5};

    // big_int <=> big_int via compare_big_int (dynamic-extent path).
    EXPECT_EQ((neg_five <=> neg_three), std::strong_ordering::less);
    EXPECT_EQ((neg_three <=> neg_five), std::strong_ordering::greater);

    // big_int <=> int via compare_integer (fixed-extent path).
    EXPECT_EQ((neg_five <=> -3), std::strong_ordering::less);
    EXPECT_EQ((-3 <=> neg_five), std::strong_ordering::greater);

    // Wide negative magnitudes exercise the `rep.size() > limbs.size()`
    // branch (wide-vs-narrow) and the equal-size common-compare branch.
    const big_int neg_one{-1};
    const big_int wide_neg_small = -big_int{1.0e20};
    const big_int wide_neg_large = -big_int{2.0e20};
    EXPECT_EQ((wide_neg_small <=> neg_one), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> wide_neg_small), std::strong_ordering::greater);
    EXPECT_EQ((wide_neg_large <=> wide_neg_small), std::strong_ordering::less);
    EXPECT_EQ((wide_neg_small <=> wide_neg_large), std::strong_ordering::greater);
}

} // namespace
