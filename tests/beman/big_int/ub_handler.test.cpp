// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Numeric errors routed through `detail::ub_handler()`: division or remainder by zero, a negative shift
// count, and the conversion of a non-finite floating-point value. With exceptions enabled the handler throws
// `std::domain_error`, and every check runs before an operand is modified, so a throwing compound operation
// leaves `*this` unchanged.

#include <climits>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::div_rem_to_zero;
using BEMAN_BIG_INT_NAMESPACE::div_result;

// A negative multi-limb value on the heap.
[[nodiscard]] big_int heap_value() { return -((big_int{1U} << 1000) + 12345U); }

// A zero that kept the heap capacity of the value it was computed from.
[[nodiscard]] big_int heap_zero() {
    big_int z = heap_value();
    z -= heap_value();
    return z;
}

// Every binary and compound division form of `x` by the big_int `zero` throws, and `x` is unchanged.
void expect_division_by_big_int_zero_throws(const big_int& x, const big_int& zero) {
    const big_int       before = x;
    big_int             result{7U};
    div_result<big_int> qr{big_int{7U}, big_int{7U}};

    EXPECT_THROW(result = x / zero, std::domain_error);
    EXPECT_THROW(result = x % zero, std::domain_error);
    EXPECT_THROW(result = big_int{x} / zero, std::domain_error);
    EXPECT_THROW(result = big_int{x} % zero, std::domain_error);
    EXPECT_THROW(result = x / big_int{zero}, std::domain_error);
    EXPECT_THROW(result = x % big_int{zero}, std::domain_error);
    EXPECT_THROW(qr = div_rem_to_zero(x, zero), std::domain_error);
    EXPECT_THROW(qr = div_rem_to_zero(big_int{x}, big_int{zero}), std::domain_error);
    EXPECT_EQ(x, before);
    EXPECT_EQ(result, 7U);
    EXPECT_EQ(qr.quotient, 7U);
    EXPECT_EQ(qr.remainder, 7U);

    big_int    y   = x;
    const auto cap = y.representation_capacity();
    EXPECT_THROW(y /= zero, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_THROW(y %= zero, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_THROW(y /= big_int{zero}, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_THROW(y %= big_int{zero}, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_EQ(y.representation_capacity(), cap);
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_normalized(y));
}

// Every division form of `x` by a zero of integer type `T`, in both operand orders, throws.
template <class T>
void expect_division_by_integer_zero_throws(const big_int& x) {
    constexpr T         zero{0};
    const big_int       before = x;
    big_int             result{7U};
    div_result<big_int> qr{big_int{7U}, big_int{7U}};

    EXPECT_THROW(result = x / zero, std::domain_error);
    EXPECT_THROW(result = x % zero, std::domain_error);
    EXPECT_THROW(result = big_int{x} / zero, std::domain_error);
    EXPECT_THROW(result = big_int{x} % zero, std::domain_error);
    EXPECT_THROW(qr = div_rem_to_zero(x, zero), std::domain_error);
    EXPECT_THROW(qr = div_rem_to_zero(big_int{x}, zero), std::domain_error);
    EXPECT_EQ(x, before);
    EXPECT_EQ(result, 7U);
    EXPECT_EQ(qr.quotient, 7U);

    big_int    y   = x;
    const auto cap = y.representation_capacity();
    EXPECT_THROW(y /= zero, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_THROW(y %= zero, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_EQ(y.representation_capacity(), cap);

    // Integer dividend, big_int zero divisor.
    const big_int big_zero{};
    EXPECT_THROW(result = T{5} / big_zero, std::domain_error);
    EXPECT_THROW(result = T{5} % big_zero, std::domain_error);
    EXPECT_THROW(result = zero / big_zero, std::domain_error);
    EXPECT_THROW(result = T{5} / big_int{}, std::domain_error);
    EXPECT_THROW(qr = div_rem_to_zero(T{5}, big_zero), std::domain_error);
    EXPECT_EQ(result, 7U);
}

// Every shift form of `x` by the negative count `s` throws, and `x` is unchanged.
template <class S>
void expect_negative_shift_throws(const big_int& x, const S s) {
    const big_int before = x;
    big_int       result{7U};

    EXPECT_THROW(result = x << s, std::domain_error);
    EXPECT_THROW(result = x >> s, std::domain_error);
    EXPECT_EQ(x, before);
    EXPECT_EQ(result, 7U);

    big_int moved = x;
    EXPECT_THROW(result = std::move(moved) << s, std::domain_error);
    EXPECT_EQ(moved, before);
    EXPECT_THROW(result = std::move(moved) >> s, std::domain_error);
    EXPECT_EQ(moved, before);
    EXPECT_EQ(result, 7U);

    big_int    y   = x;
    const auto cap = y.representation_capacity();
    EXPECT_THROW(y <<= s, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_THROW(y >>= s, std::domain_error);
    EXPECT_EQ(y, before);
    EXPECT_EQ(y.representation_capacity(), cap);
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_normalized(y));
}

template <class S>
void expect_negative_shifts_throw(const S s) {
    expect_negative_shift_throws(big_int{}, s);
    expect_negative_shift_throws(big_int{42U}, s);
    expect_negative_shift_throws(-big_int{42U}, s);
    expect_negative_shift_throws(heap_value(), s);
}

// Both floating-point constructors throw on infinity of either sign and on NaN, while the largest finite
// value, which needs a heap allocation, still converts.
template <class F>
void expect_non_finite_conversion_throws() {
    using limits = std::numeric_limits<F>;
    const big_int::allocator_type alloc{};
    big_int                       result{7U};

    // The forwarding constructor is given prvalues: its constraint does not yet accept a floating-point lvalue.
    EXPECT_THROW(result = big_int{limits::infinity()}, std::domain_error);
    EXPECT_THROW(result = big_int{-limits::infinity()}, std::domain_error);
    EXPECT_THROW(result = big_int{limits::quiet_NaN()}, std::domain_error);
    EXPECT_THROW(result = big_int{-limits::quiet_NaN()}, std::domain_error);
    for (const F v : {limits::infinity(), -limits::infinity(), limits::quiet_NaN(), -limits::quiet_NaN()}) {
        EXPECT_THROW(result = big_int(v, alloc), std::domain_error);
    }
    EXPECT_EQ(result, 7U);

    const big_int largest{limits::max()};
    EXPECT_EQ(largest.size(), static_cast<std::size_t>(limits::max_exponent));
    EXPECT_EQ(big_int{-limits::max()}, -largest);
    EXPECT_EQ(big_int{limits::denorm_min()}, 0U);
}

} // namespace

TEST(UbHandler, ThrowsDomainError) {
    EXPECT_THROW(BEMAN_BIG_INT_NAMESPACE::detail::ub_handler(), std::domain_error);
    EXPECT_THROW(BEMAN_BIG_INT_NAMESPACE::detail::ub_handler(), std::logic_error);
}

TEST(UbHandler, DivisionByBigIntZero) {
    for (const big_int& zero : {big_int{}, heap_zero()}) {
        expect_division_by_big_int_zero_throws(big_int{}, zero);
        expect_division_by_big_int_zero_throws(big_int{42U}, zero);
        expect_division_by_big_int_zero_throws(-big_int{42U}, zero);
        expect_division_by_big_int_zero_throws(heap_value(), zero);
    }
}

TEST(UbHandler, DivisionByIntegerZero) {
    for (const big_int& x : {big_int{}, big_int{42U}, -big_int{42U}, heap_value()}) {
        expect_division_by_integer_zero_throws<int>(x);
        expect_division_by_integer_zero_throws<unsigned>(x);
        expect_division_by_integer_zero_throws<long long>(x);
        expect_division_by_integer_zero_throws<unsigned long long>(x);
        expect_division_by_integer_zero_throws<signed char>(x);
    }
}

TEST(UbHandler, SelfDivisionOfZero) {
    big_int z{};
    EXPECT_THROW(z /= z, std::domain_error);
    EXPECT_EQ(z, 0U);
    EXPECT_THROW(z %= z, std::domain_error);
    EXPECT_EQ(z, 0U);

    big_int hz = heap_zero();
    EXPECT_THROW(hz /= hz, std::domain_error);
    EXPECT_EQ(hz, 0U);
    EXPECT_THROW(hz %= hz, std::domain_error);
    EXPECT_EQ(hz, 0U);
}

TEST(UbHandler, NegativeShift) {
    expect_negative_shifts_throw(-1);
    expect_negative_shifts_throw(INT_MIN);
    expect_negative_shifts_throw(-1LL);
    expect_negative_shifts_throw(LLONG_MIN);
    expect_negative_shifts_throw(static_cast<short>(-5));
    expect_negative_shifts_throw(static_cast<signed char>(-1));
}

TEST(UbHandler, NonFiniteFloatConversion) {
    expect_non_finite_conversion_throws<float>();
    expect_non_finite_conversion_throws<double>();
#ifndef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
    expect_non_finite_conversion_throws<long double>();
#endif
}

// The checks reject only the violating values: a zero shift and a nonzero divisor still work.
TEST(UbHandler, BoundaryValuesStillWork) {
    big_int x = heap_value();
    EXPECT_EQ(x << 0, x);
    EXPECT_EQ(x >> 0, x);
    EXPECT_EQ(big_int{42U} / -1, -42);
    EXPECT_EQ(big_int{42U} % -1, 0U);
    x /= x;
    EXPECT_EQ(x, 1U);
}
