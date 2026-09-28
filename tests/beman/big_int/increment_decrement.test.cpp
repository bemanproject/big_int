// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using beman::big_int::big_int;
using beman::big_int::uint_multiprecision_t;
using beman::big_int::detail::int_multiprecision_t;

TEST(IncrementDecrement, PrefixIncrement) {
    big_int x{41};
    big_int y{-5};
    big_int z{std::numeric_limits<uint_multiprecision_t>::max()};

    big_int& rx = ++x;
    big_int& ry = ++y;
    big_int& rz = ++z;

    EXPECT_EQ(&rx, &x);
    EXPECT_EQ(&ry, &y);
    EXPECT_EQ(&rz, &z);
    EXPECT_EQ(x, 42);
    EXPECT_EQ(y, -4);
    EXPECT_EQ(z, big_int{1} << 64);
}

TEST(IncrementDecrement, PostfixIncrement) {
    big_int x{41};
    big_int y{-5};

    const big_int old_x = x++;
    const big_int old_y = y++;

    EXPECT_EQ(old_x, 41);
    EXPECT_EQ(x, 42);
    EXPECT_EQ(old_y, -5);
    EXPECT_EQ(y, -4);
}

TEST(IncrementDecrement, PrefixDecrement) {
    big_int p{5};
    big_int q{0};
    big_int r{-5};
    big_int s{std::numeric_limits<uint_multiprecision_t>::max()};
    s = -s;

    big_int& rp = --p;
    big_int& rq = --q;
    big_int& rr = --r;
    big_int& rs = --s;

    EXPECT_EQ(&rp, &p);
    EXPECT_EQ(&rq, &q);
    EXPECT_EQ(&rr, &r);
    EXPECT_EQ(&rs, &s);
    EXPECT_EQ(p, 4);
    EXPECT_EQ(q, -1);
    EXPECT_EQ(r, -6);
    EXPECT_EQ(s, -(big_int{1} << 64));
    EXPECT_EQ((s <=> 0), std::strong_ordering::less);
}

TEST(IncrementDecrement, PostfixDecrement) {
    big_int x{41};
    big_int y{0};
    big_int z{-5};

    const big_int old_x = x--;
    const big_int old_y = y--;
    const big_int old_z = z--;

    EXPECT_EQ(old_x, 41);
    EXPECT_EQ(x, 40);
    EXPECT_EQ(old_y, 0);
    EXPECT_EQ(y, -1);
    EXPECT_EQ(old_z, -5);
    EXPECT_EQ(z, -6);
}

TEST(IncrementDecrement, PrefixIncrementRequiresAllocationForLargeValue) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_TRUE(is_inplace(x));

    ++x;

    EXPECT_EQ(x, big_int{1} << 64);
    EXPECT_FALSE(is_inplace(x));
}

TEST(IncrementDecrement, ZeroAndOneTransitions) {
    big_int a{1};
    big_int b{-1};
    big_int c{0};
    big_int d{0};

    --a;
    ++b;
    ++c;
    --d;

    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(c, 1);
    EXPECT_EQ(d, -1);
}

TEST(IncrementDecrement, PrefixIncrementAllocatedCarryChain) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    ++x;
    x = -x;

    EXPECT_FALSE(is_inplace(x));
    EXPECT_EQ(x, -(big_int{1} << 64));

    ++x;

    EXPECT_EQ(x, -big_int{std::numeric_limits<uint_multiprecision_t>::max()});
    EXPECT_FALSE(is_inplace(x));
}

TEST(IncrementDecrement, PrefixDecrementAllocatedBorrowChain) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    ++x;

    EXPECT_FALSE(is_inplace(x));
    EXPECT_EQ(x, big_int{1} << 64);

    --x;

    EXPECT_EQ(x, big_int{std::numeric_limits<uint_multiprecision_t>::max()});
    EXPECT_FALSE(is_inplace(x));
}

TEST(IncrementDecrement, BitwiseNotSmallIntegers) {
    big_int a{0};
    big_int b{1};
    big_int c{-1};
    big_int d{42};
    big_int e{-42};

    const big_int ra = ~a;
    const big_int rb = ~b;
    const big_int rc = ~c;
    const big_int rd = ~d;
    const big_int re = ~e;

    EXPECT_EQ(ra, -1);
    EXPECT_EQ(rb, -2);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(rd, -43);
    EXPECT_EQ(re, 41);

    const big_int rr = ~big_int{7};
    EXPECT_EQ(rr, -8);
}

TEST(IncrementDecrement, BitwiseNotBigInteger) {
    big_int x{1};
    x <<= 130;

    const big_int y = ~x;

    EXPECT_EQ((y <=> 0), std::strong_ordering::less);
    EXPECT_EQ(y, -x - 1);

    big_int       n = -x;
    const big_int z = ~n;

    EXPECT_EQ((z <=> 0), std::strong_ordering::greater);
    EXPECT_EQ(z, x - 1);
}

TEST(IncrementDecrement, BitwiseNotCanAllocate) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    EXPECT_TRUE(is_inplace(x));

    const big_int y = ~x;

    EXPECT_EQ(y, -x - 1);
    EXPECT_EQ((y <=> 0), std::strong_ordering::less);
    EXPECT_FALSE(is_inplace(y));
}

constexpr std::size_t limb_bits = std::size_t{std::numeric_limits<uint_multiprecision_t>::digits};

using big_int_256 = beman::big_int::basic_big_int<256>;

// `--` and `++` are the only operations that can shrink a magnitude across a
// limb boundary. is_normalized() lives in testing.hpp.

consteval bool decrement_is_normalized(unsigned shift) {
    big_int x = big_int{1} << shift;
    --x;
    return is_normalized(x) && x.size() == shift && x == (big_int{1} << shift) - big_int{1};
}
static_assert(decrement_is_normalized(64));
static_assert(decrement_is_normalized(128));
static_assert(decrement_is_normalized(192));

consteval bool increment_negative_is_normalized(unsigned shift) {
    big_int x = -(big_int{1} << shift);
    ++x;
    return is_normalized(x) && x == -((big_int{1} << shift) - big_int{1});
}
static_assert(increment_negative_is_normalized(64));
static_assert(increment_negative_is_normalized(128));

// big_int_256 holds four limbs in the in-place buffer, so these never allocate.
// Returns bool rather than the value: MSVC 19.44 cannot materialize a consteval
// big_int result whose active union member is the in-place limb array.
consteval bool decremented_inplace_is_normalized(unsigned shift) {
    big_int_256 x = 1;
    x <<= shift;
    --x;
    return is_normalized(x) && x.size() == shift;
}
static_assert(decremented_inplace_is_normalized(128));
static_assert(decremented_inplace_is_normalized(192));

TEST(IncrementDecrement, PrefixDecrementAcrossLimbBoundaryIsNormalized) {
    for (const unsigned shift : {64U, 128U, 192U}) {
        big_int x = big_int{1} << shift;
        --x;

        EXPECT_EQ(x, (big_int{1} << shift) - big_int{1}) << "shift " << shift;
        EXPECT_TRUE(is_normalized(x)) << "shift " << shift;
        EXPECT_EQ(x.representation().size(), shift / limb_bits) << "shift " << shift;
        EXPECT_EQ(x.size(), shift) << "shift " << shift;
    }
}

TEST(IncrementDecrement, PostfixDecrementAcrossLimbBoundaryIsNormalized) {
    for (const unsigned shift : {64U, 128U, 192U}) {
        big_int       x     = big_int{1} << shift;
        const big_int old_x = x--;

        EXPECT_TRUE(is_normalized(old_x)) << "shift " << shift;
        EXPECT_TRUE(is_normalized(x)) << "shift " << shift;
        EXPECT_EQ(x.size(), shift) << "shift " << shift;
    }
}

TEST(IncrementDecrement, PrefixIncrementNegativeAcrossLimbBoundary) {
    for (const unsigned shift : {64U, 128U, 192U}) {
        big_int x = -(big_int{1} << shift);
        ++x;

        EXPECT_EQ(x, -((big_int{1} << shift) - big_int{1})) << "shift " << shift;
        EXPECT_TRUE(is_normalized(x)) << "shift " << shift;
        EXPECT_EQ(x.size(), shift) << "shift " << shift;
    }
}

// operator~ has a const& and an && overload, and both end in a decrement.
TEST(IncrementDecrement, BitwiseNotAcrossLimbBoundaryIsNormalized) {
    for (const unsigned shift : {64U, 128U, 192U}) {
        const big_int lvalue = -(big_int{1} << shift);

        EXPECT_TRUE(is_normalized(~lvalue)) << "shift " << shift;
        EXPECT_TRUE(is_normalized(~(-(big_int{1} << shift)))) << "shift " << shift;
        EXPECT_EQ(~lvalue, (big_int{1} << shift) - big_int{1}) << "shift " << shift;
    }
}

TEST(IncrementDecrement, PrefixDecrementInplaceAcrossLimbBoundaryIsNormalized) {
    for (const unsigned shift : {64U, 128U, 192U}) {
        big_int_256 x{1};
        x <<= shift;
        big_int_256 expected = x;
        expected -= big_int_256{1};
        --x;

        EXPECT_TRUE(is_inplace(x)) << "shift " << shift;
        EXPECT_EQ(x, expected) << "shift " << shift;
        EXPECT_TRUE(is_normalized(x)) << "shift " << shift;
        EXPECT_EQ(x.size(), shift) << "shift " << shift;
    }
}

// A de-normalized magnitude reports size() == 0, and the free operator>>
// discards everything at or beyond that width, so it loses the value outright
// rather than only mis-printing it. Compound >>= does not read size().
TEST(IncrementDecrement, DecrementAcrossLimbBoundaryKeepsMagnitudeObservers) {
    const big_int expected = (big_int{1} << 128) - big_int{1};
    big_int       x        = big_int{1} << 128;
    --x;

    ASSERT_EQ(x, expected);
    EXPECT_EQ(x.size(), expected.size());
    EXPECT_EQ(x.representation_size(), x.representation().size());
    EXPECT_EQ(x >> 0, expected);
    EXPECT_EQ(x >> 1, expected >> 1);
    EXPECT_EQ(x >> 64, expected >> 64);
    EXPECT_EQ(to_string(x), "340282366920938463463374607431768211455");
}

TEST(IncrementDecrement, DecrementToSingleLimbCanShrinkBackToInplace) {
    big_int x = big_int{1} << 64;
    ASSERT_FALSE(is_inplace(x));

    --x;
    x.shrink_to_fit();

    EXPECT_TRUE(is_normalized(x));
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x, big_int{std::numeric_limits<uint_multiprecision_t>::max()});
}

// Increment and decrement are not ref-qualified, so they apply to an rvalue
// just as the built-in integer operators do on a class-type operand.
static_assert(requires(big_int x) { ++std::move(x); });
static_assert(requires(big_int x) { std::move(x)++; });
static_assert(requires(big_int x) { --std::move(x); });
static_assert(requires(big_int x) { std::move(x)--; });

TEST(IncrementDecrement, AppliesToRvalue) {
    const big_int post_incremented = (big_int{41})++;
    const big_int post_decremented = (big_int{41})--;

    EXPECT_EQ(++big_int{41}, 42);
    EXPECT_EQ(--big_int{41}, 40);
    EXPECT_EQ(post_incremented, 41); // The copy is of the value before the increment.
    EXPECT_EQ(post_decremented, 41);
}

consteval bool test_rvalue_increment_carries() {
    // The temporary is heap-allocated, so the rvalue path also exercises the
    // carry chain and the allocation it needs.
    const big_int limb_max{std::numeric_limits<uint_multiprecision_t>::max()};
    return ++big_int{limb_max} == big_int{1} << 64 && --big_int{big_int{1} << 64} == limb_max;
}
static_assert(test_rvalue_increment_carries());

} // namespace
