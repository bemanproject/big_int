// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::abs;
using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// A wide instantiation: inplace_capacity == 256 / 64 == 4, so values up to four
// limbs stay inline and never reach the allocator.
using big_int_256 = basic_big_int<256>;

using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;

// ----- type-level checks -----

// Both overloads return a prvalue of the argument's own specialization, whatever
// the value category or cv-qualification of the argument.
static_assert(std::is_same_v<decltype(abs(std::declval<big_int>())), big_int>);
static_assert(std::is_same_v<decltype(abs(std::declval<big_int&>())), big_int>);
static_assert(std::is_same_v<decltype(abs(std::declval<const big_int&>())), big_int>);
static_assert(std::is_same_v<decltype(abs(std::declval<const big_int>())), big_int>);
static_assert(std::is_same_v<decltype(abs(std::declval<big_int_256>())), big_int_256>);
static_assert(std::is_same_v<decltype(abs(std::declval<pmr_big_int>())), pmr_big_int>);

// The rvalue overload takes the argument's storage as-is, so it allocates nothing
// and is unconditionally `noexcept`; the copying overload is not.
static_assert(noexcept(abs(std::declval<big_int>())));
static_assert(noexcept(abs(std::declval<big_int_256>())));
static_assert(noexcept(abs(std::declval<pmr_big_int>())));
static_assert(!noexcept(abs(std::declval<const big_int&>())));
static_assert(!noexcept(abs(std::declval<big_int&>())));

// A const rvalue cannot bind to `basic_big_int&&`, so it selects the copying
// overload rather than silently moving out of a const object.
static_assert(!noexcept(abs(std::declval<const big_int>())));

// Neither overload is viable for anything that is not a `basic_big_int`, so an
// unqualified call on a fundamental type still finds `std::abs` (or none at all).
template <class T>
concept has_beman_abs = requires(T&& t) { BEMAN_BIG_INT_NAMESPACE::abs(static_cast<T&&>(t)); };
static_assert(has_beman_abs<big_int>);
static_assert(!has_beman_abs<int>);
static_assert(!has_beman_abs<long long>);
static_assert(!has_beman_abs<double>);
static_assert(!has_beman_abs<unsigned>);

// ----- compile-time checks -----

static_assert(abs(big_int{-5}) == 5);
static_assert(abs(big_int{5}) == 5);
static_assert(abs(big_int{0}) == 0);
static_assert(abs(big_int{std::numeric_limits<uint_multiprecision_t>::max()}) ==
              std::numeric_limits<uint_multiprecision_t>::max());

// The copying overload, exercised through a named lvalue so that the copy
// constructor -- not the move constructor -- is the one running.
consteval bool test_lvalue_copies() {
    const big_int x{-123456};
    const big_int a = abs(x);
    return a == 123456 && x == -123456;
}
static_assert(test_lvalue_copies());

// A multi-limb value in constant evaluation: the magnitude survives the sign flip
// limb for limb.
consteval bool test_multi_limb() {
    big_int x{1};
    x <<= 200;
    const big_int magnitude = x;
    return abs(-x) == magnitude && abs(std::move(x)) == magnitude;
}
static_assert(test_multi_limb());

// `abs` never yields the corrupt negative-zero state: the result of `abs(-0)`
// compares equal to zero and so does its negation.
consteval bool test_zero_is_canonical() {
    const big_int z = abs(-big_int{0});
    return z == 0 && -z == 0 && (z <=> 0) == std::strong_ordering::equal;
}
static_assert(test_zero_is_canonical());

// ----- runtime tests -----

TEST(Abs, SmallPositive) {
    EXPECT_EQ(abs(big_int{1}), 1);
    EXPECT_EQ(abs(big_int{42}), 42);
    EXPECT_EQ(abs(big_int{std::numeric_limits<uint_multiprecision_t>::max()}),
              std::numeric_limits<uint_multiprecision_t>::max());
}

TEST(Abs, SmallNegative) {
    EXPECT_EQ(abs(big_int{-1}), 1);
    EXPECT_EQ(abs(big_int{-42}), 42);
    EXPECT_EQ((abs(big_int{-42}) <=> 0), std::strong_ordering::greater);
}

TEST(Abs, Zero) {
    const big_int z = abs(big_int{0});
    EXPECT_EQ(z, 0);
    // Never the corrupt negative-zero state: it compares equal to zero, and so
    // does its negation.
    EXPECT_EQ((z <=> 0), std::strong_ordering::equal);
    EXPECT_EQ(-z, 0);
    EXPECT_TRUE(is_normalized(z));
}

TEST(Abs, LargePositiveStaysOnHeap) {
    const big_int x = big_int{1} << 200;
    ASSERT_FALSE(is_inplace(x));

    const big_int a = abs(x);

    EXPECT_EQ(a, x);
    EXPECT_FALSE(is_inplace(a));
    EXPECT_EQ((a <=> 0), std::strong_ordering::greater);
}

TEST(Abs, LargeNegative) {
    const big_int magnitude = big_int{1} << 200;
    const big_int x         = -magnitude;
    ASSERT_FALSE(is_inplace(x));

    const big_int a = abs(x);

    EXPECT_EQ(a, magnitude);
    EXPECT_EQ((a <=> 0), std::strong_ordering::greater);
    EXPECT_TRUE(is_normalized(a));
}

TEST(Abs, CrossesLimbBoundary) {
    const big_int magnitude = big_int{1} << 64;

    EXPECT_EQ(abs(magnitude), magnitude);
    EXPECT_EQ(abs(-magnitude), magnitude);
}

TEST(Abs, LvalueLeavesArgumentUnchanged) {
    big_int x{-42};

    const big_int a = abs(x);

    EXPECT_EQ(a, 42);
    EXPECT_EQ(x, -42); // The lvalue overload copies; the argument is untouched.
}

TEST(Abs, LvalueHeapArgumentIsIndependent) {
    // The copy owns its own buffer, so mutating the result cannot disturb the
    // argument it was taken from.
    big_int x = -(big_int{1} << 300);
    ASSERT_FALSE(is_inplace(x));

    big_int a = abs(x);
    ASSERT_NE(a.representation().data(), x.representation().data());

    a += 1;

    EXPECT_EQ(x, -(big_int{1} << 300));
    EXPECT_EQ(a, (big_int{1} << 300) + 1);
}

TEST(Abs, RvalueReusesHeapStorage) {
    // Sign and magnitude are stored apart, so handing the value over lets `abs`
    // clear the sign bit on the existing limbs: the buffer is neither copied nor
    // reallocated.
    big_int           x    = -(big_int{1} << 300);
    const void*       data = x.representation().data();
    const std::size_t size = x.representation().size();
    ASSERT_FALSE(is_inplace(x));

    const big_int a = abs(std::move(x));

    EXPECT_EQ(a.representation().data(), data);
    EXPECT_EQ(a.representation().size(), size);
    EXPECT_EQ(a, big_int{1} << 300);
}

TEST(Abs, RvalueLargeProducesMagnitude) {
    // An rvalue argument is moved into the result; the heap storage survives.
    const big_int a = abs(-(big_int{1} << 200));

    EXPECT_EQ(a, big_int{1} << 200);
    EXPECT_FALSE(is_inplace(a));
}

TEST(Abs, RvalueInPlaceValue) {
    // The same path with an inline value: nothing was allocated to begin with, so
    // the result stays inline too.
    const big_int a = abs(big_int{-99});

    EXPECT_EQ(a, 99);
    EXPECT_TRUE(is_inplace(a));
}

TEST(Abs, SelfAssignThroughRvalue) {
    // `x = abs(std::move(x))` is the in-place idiom callers reach for. The move
    // constructor runs before the assignment, so no object is assigned from itself.
    big_int x = -(big_int{1} << 300);

    x = abs(std::move(x));

    EXPECT_EQ(x, big_int{1} << 300);
    EXPECT_TRUE(is_normalized(x));
}

TEST(Abs, WideInstantiationStaysInline) {
    // Four limbs fit inline in a 256-bit instantiation, so this whole test runs
    // without touching the allocator.
    const big_int_256 magnitude = (big_int_256{1} << 255) - 1;
    const big_int_256 negative  = -magnitude;
    ASSERT_TRUE(is_inplace(negative));

    const big_int_256 from_lvalue = abs(negative);
    const big_int_256 from_rvalue = abs(-((big_int_256{1} << 255) - 1));

    EXPECT_EQ(from_lvalue, magnitude);
    EXPECT_EQ(from_rvalue, magnitude);
    EXPECT_TRUE(is_inplace(from_lvalue));
    EXPECT_TRUE(is_inplace(from_rvalue));
}

TEST(Abs, SelectsAllocator) {
    // The lvalue overload copies, so the result's allocator comes from
    // `select_on_container_copy_construction`, which for `polymorphic_allocator`
    // is the default resource. The rvalue overload takes over the storage, so it
    // keeps the operand's resource.
    std::pmr::monotonic_buffer_resource resource;
    pmr_big_int                         x{-1, &resource};
    x <<= 300;

    const pmr_big_int from_lvalue = abs(x);
    EXPECT_EQ(from_lvalue.get_allocator().resource(), std::pmr::get_default_resource());

    const pmr_big_int from_rvalue = abs(std::move(x));
    EXPECT_EQ(from_rvalue.get_allocator().resource(), &resource);
    EXPECT_EQ(from_rvalue, from_lvalue);
}

TEST(Abs, ReservedCapacityIsCarriedAcross) {
    // A value whose magnitude fits inline but which has reserved heap capacity
    // takes the non-inplace branch of the copy constructor.
    big_int x{-7};
    x.reserve(4000);
    ASSERT_FALSE(is_inplace(x));

    EXPECT_EQ(abs(x), 7);
    EXPECT_EQ(abs(std::move(x)), 7);
}

TEST(Abs, Idempotent) {
    const big_int positive{1234567};
    const big_int negative{-1234567};

    EXPECT_EQ(abs(abs(negative)), abs(negative));
    EXPECT_EQ(abs(abs(positive)), positive);
}

TEST(Abs, EqualsAbsOfNegation) {
    big_int x{-98765};

    EXPECT_EQ(abs(x), abs(-x));
    EXPECT_EQ(abs(-x), 98765);

    const big_int big = big_int{1} << 150;
    EXPECT_EQ(abs(big), abs(-big));
}

TEST(Abs, MatchesSignedMagnitudeOfFundamentalTypes) {
    // Including the value `std::abs` cannot represent: `abs` is total on
    // `basic_big_int` because the type is unbounded.
    constexpr auto int_min = std::numeric_limits<long long>::min();

    EXPECT_EQ(abs(big_int{int_min}), big_int{int_min} * -1);
    EXPECT_EQ(abs(big_int{int_min}) - 1, std::numeric_limits<long long>::max());
    EXPECT_EQ(abs(big_int{-7}), std::abs(-7));
}

TEST(Abs, CallableFullyQualified) {
    // The function is reachable both via the using-declaration above (ADL) and
    // when named explicitly through its namespace.
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::abs(big_int{-7}), 7);
    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::abs(big_int{7}), 7);
}

} // namespace
