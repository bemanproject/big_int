// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Consumer-side tests of <beman/big_int/numeric.hpp>'s free functions (abs,
// saturating_cast, in_range, gcd, lcm, midpoint) through `import beman.big_int;`.

#include <limits>
#include <numeric> // std::gcd/std::lcm: the unqualified call below must still resolve to ours.
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;
namespace {

using BEMAN_BIG_INT_NAMESPACE::abs;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::gcd;
using BEMAN_BIG_INT_NAMESPACE::in_range;
using BEMAN_BIG_INT_NAMESPACE::lcm;
using BEMAN_BIG_INT_NAMESPACE::midpoint;
using BEMAN_BIG_INT_NAMESPACE::saturating_cast;

// ----- abs on lvalue and rvalue -----

// The lvalue overload copies its argument (a copy that may allocate), so it is
// not noexcept; the rvalue overload consumes storage the caller handed over and
// only clears the sign, so it is noexcept.
static_assert(!noexcept(abs(std::declval<const big_int&>())));
static_assert(noexcept(abs(std::declval<big_int&&>())));

TEST(Numeric, AbsLvalueAndRvalue) {
    const big_int neg{-42};
    const big_int pos{42};
    big_int       movable_neg{-42};

    EXPECT_EQ(abs(neg), pos); // lvalue overload
    EXPECT_EQ(neg, -42);      // the lvalue argument is unchanged
    EXPECT_EQ(abs(big_int{-7}), 7);
    EXPECT_EQ(abs(std::move(movable_neg)), pos); // rvalue overload
    EXPECT_EQ(abs(big_int{0}), 0);
}

// ----- saturating_cast: boundaries and rejected types -----

// `saturating_cast<R>` is constrained to a signed or unsigned integer type
// (detail::signed_or_unsigned in numeric.hpp): bool and character types are
// rejected without a hard error.
template <class R>
// Qualified deliberately. `big_int`'s allocator template argument drags namespace std
// into argument-dependent lookup, so an unqualified probe can pick up a std overload
// instead of ours -- and whether that overload SFINAEs out or static_asserts in its
// body differs between libc++ and libstdc++. Qualifying tests our constraint only.
concept has_saturating_cast = requires(const big_int& x) { BEMAN_BIG_INT_NAMESPACE::saturating_cast<R>(x); };

static_assert(has_saturating_cast<int>);
static_assert(has_saturating_cast<unsigned>);
static_assert(has_saturating_cast<signed char>);
static_assert(has_saturating_cast<long long>);
static_assert(!has_saturating_cast<bool>);
static_assert(!has_saturating_cast<char>);
static_assert(!has_saturating_cast<wchar_t>);

template <class R>
void check_saturating_cast_boundaries() {
    constexpr R lo = std::numeric_limits<R>::min();
    constexpr R hi = std::numeric_limits<R>::max();

    EXPECT_EQ(saturating_cast<R>(big_int{lo}), lo);
    EXPECT_EQ(saturating_cast<R>(big_int{hi}), hi);
    EXPECT_EQ(saturating_cast<R>(big_int{lo} - 1), lo); // clamps below the minimum
    EXPECT_EQ(saturating_cast<R>(big_int{hi} + 1), hi); // clamps above the maximum
}

TEST(Numeric, SaturatingCastBoundaries) {
    check_saturating_cast_boundaries<int>();
    check_saturating_cast_boundaries<unsigned>();
    check_saturating_cast_boundaries<signed char>();
    check_saturating_cast_boundaries<long long>();
}

// ----- in_range: the same boundaries -----

template <class R>
// Qualified for the same reason as has_saturating_cast above: `std::in_range` from
// <utility> is an ADL candidate here, and libstdc++ rejects it with a static_assert in
// the body rather than a constraint, which would make an unqualified probe succeed.
concept has_in_range = requires(const big_int& x) { BEMAN_BIG_INT_NAMESPACE::in_range<R>(x); };

static_assert(has_in_range<int>);
static_assert(has_in_range<unsigned>);
static_assert(has_in_range<signed char>);
static_assert(has_in_range<long long>);
static_assert(!has_in_range<bool>);
static_assert(!has_in_range<char>);

template <class R>
void check_in_range_boundaries() {
    constexpr R lo = std::numeric_limits<R>::min();
    constexpr R hi = std::numeric_limits<R>::max();

    EXPECT_TRUE(in_range<R>(big_int{lo}));
    EXPECT_TRUE(in_range<R>(big_int{hi}));
    EXPECT_FALSE(in_range<R>(big_int{lo} - 1));
    EXPECT_FALSE(in_range<R>(big_int{hi} + 1));
}

TEST(Numeric, InRangeBoundaries) {
    check_in_range_boundaries<int>();
    check_in_range_boundaries<unsigned>();
    check_in_range_boundaries<signed char>();
    check_in_range_boundaries<long long>();
}

// ----- gcd: big-big, big-builtin, builtin-big, zero, negative -----

TEST(Numeric, Gcd) {
    EXPECT_EQ(gcd(big_int{48}, big_int{18}), 6);   // big-big
    EXPECT_EQ(gcd(big_int{48}, 18), 6);            // big-builtin
    EXPECT_EQ(gcd(48, big_int{18}), 6);            // builtin-big
    EXPECT_EQ(gcd(big_int{0}, big_int{0}), 0);     // zero-zero
    EXPECT_EQ(gcd(big_int{0}, big_int{42}), 42);   // gcd(x, 0) == |x|
    EXPECT_EQ(gcd(big_int{-48}, big_int{18}), 6);  // negative operand
    EXPECT_EQ(gcd(big_int{-48}, big_int{-18}), 6); // both negative
}

// ----- lcm: big-big, big-builtin, builtin-big, zero, negative -----

TEST(Numeric, Lcm) {
    EXPECT_EQ(lcm(big_int{4}, big_int{6}), 12);   // big-big
    EXPECT_EQ(lcm(big_int{4}, 6), 12);            // big-builtin
    EXPECT_EQ(lcm(4, big_int{6}), 12);            // builtin-big
    EXPECT_EQ(lcm(big_int{0}, big_int{5}), 0);    // lcm(x, 0) == 0
    EXPECT_EQ(lcm(big_int{-4}, big_int{6}), 12);  // negative operand
    EXPECT_EQ(lcm(big_int{-4}, big_int{-6}), 12); // both negative
}

// ----- midpoint: big-big, big-builtin, builtin-big, zero, negative -----
//
// midpoint(m, n) == m + trunc_toward_zero((n - m) / 2), rounding toward `m`
// when the sum is odd (per [numeric.midpoint] in numeric.hpp).

TEST(Numeric, Midpoint) {
    EXPECT_EQ(midpoint(big_int{4}, big_int{10}), 7);                                    // big-big, even sum
    EXPECT_EQ(midpoint(big_int{4}, 10), 7);                                             // big-builtin
    EXPECT_EQ(midpoint(4, big_int{10}), 7);                                             // builtin-big
    EXPECT_EQ(midpoint(big_int{0}, big_int{0}), 0);                                     // zero-zero
    EXPECT_EQ(midpoint(big_int{-4}, big_int{4}), 0);                                    // symmetric about zero
    EXPECT_EQ(midpoint(big_int{1}, big_int{4}), 2);                                     // odd sum, rounds toward m = 1
    EXPECT_EQ(midpoint(big_int{-1}, big_int{-4}), -2);                                  // both negative, odd sum
    EXPECT_EQ(midpoint(-3, big_int{5}), 1);                                             // mixed sign, builtin-big
    EXPECT_EQ(midpoint(big_int{1} << 200, big_int{1} << 201), (big_int{1} << 199) * 3); // multi-limb
}

// ----- ADL hazard: an unqualified gcd(a, b) must select the library overload,
// not std::gcd, even with <numeric> in scope. Every basic_big_int drags
// namespace std into argument-dependent lookup through its allocator, so
// std::gcd is always a visible candidate too; ours is the more constrained
// overload and must win. -----

namespace adl_probe {
[[nodiscard]] constexpr bool resolves_to_library_gcd() { return gcd(big_int{270}, 192) == 6; }
} // namespace adl_probe

static_assert(std::is_same_v<decltype(gcd(std::declval<big_int>(), std::declval<big_int>())), big_int>);
static_assert(adl_probe::resolves_to_library_gcd());

TEST(Numeric, GcdAdlHazard) {
    const big_int a{270};
    const int     b = 192;
    EXPECT_EQ(gcd(a, b), 6); // unqualified: must not resolve to std::gcd
    EXPECT_TRUE((std::is_same_v<decltype(gcd(a, b)), big_int>));
}

} // namespace
