// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// [big.int.require]: during constant evaluation, a value whose effective width is at most
// inplace_bits holds no allocation after any operation (issue #410). Each test starts from a
// heap value and checks that the result of the operation is back in place, with the right value,
// canonical, and with a zeroed in-place tail. basic_big_int<128> and <192> cover multi-limb
// in-place storage.

#include <array>
#include <charconv>
#include <cstddef>
#include <utility>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

namespace bb = BEMAN_BIG_INT_NAMESPACE;

// The reproducer from the issue.
namespace issue_410 {
using bb::big_int;
constexpr big_int a = (big_int(1) << 200) >> 190;
constexpr big_int b = [] {
    big_int x = big_int(1) << 200;
    x         = 5;
    return x;
}();
constexpr big_int ok1 = [] {
    big_int x = big_int(1) << 200;
    return x >> 190;
}();
constexpr big_int ok2 = [] {
    big_int x = big_int(1) << 200;
    x         = 5;
    x.shrink_to_fit();
    return x;
}();
static_assert(a == 1024 && b == 5 && ok1 == 1024 && ok2 == 5);
} // namespace issue_410

namespace {

// True when `x` holds `expected` in place, canonically, with every in-place limb past the
// limb count zero.
template <class T>
constexpr bool holds_in_place(const T& x, const T& expected) {
    if (!bb::is_inplace(x) || !bb::is_normalized(x) || x != expected) {
        return false;
    }
    const auto* const limbs = x.representation().data();
    for (std::size_t i = x.representation().size(); i < T::inplace_capacity; ++i) {
        if (limbs[i] != 0) {
            return false;
        }
    }
    return true;
}

// A value that needs the heap for every width tested here.
template <class T>
constexpr T heap_value() {
    return T(1) << 300;
}

template <class T>
consteval bool test_assignment() {
    T x = heap_value<T>();
    x   = 5;
    if (!holds_in_place(x, T(5))) {
        return false;
    }
    x             = heap_value<T>();
    const T small = -7;
    x             = small;
    if (!holds_in_place(x, T(-7))) {
        return false;
    }
    x       = heap_value<T>();
    T other = 9;
    x       = std::move(other);
    if (!holds_in_place(x, T(9))) {
        return false;
    }
    x = heap_value<T>();
    x = bb::basic_big_int<256>(11);
    return holds_in_place(x, T(11));
}

template <class T>
consteval bool test_construction() {
    using limb                    = bb::uint_multiprecision_t;
    const std::array<limb, 6> src = {5, 0, 0, 0, 0, 0};
    const T                   x(src.begin(), src.end());
    if (!holds_in_place(x, T(5))) {
        return false;
    }
#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_CONTAINERS_RANGES
    const T y(std::from_range, src);
    if (!holds_in_place(y, T(5))) {
        return false;
    }
#endif
    return true;
}

template <class T>
consteval bool test_shift() {
    T x = heap_value<T>();
    x >>= 290;
    if (!holds_in_place(x, T(1024))) {
        return false;
    }
    // Fills every in-place limb.
    x = heap_value<T>();
    x >>= 300 - (T::inplace_bits - 1);
    if (!holds_in_place(x, T(1) << (T::inplace_bits - 1))) {
        return false;
    }
    if (!holds_in_place(heap_value<T>() >> 299, T(2))) {
        return false;
    }
    return holds_in_place(-heap_value<T>() >> 400, T(-1));
}

template <class T>
consteval bool test_increment_decrement() {
    const T one_past = T(1) << T::inplace_bits;
    const T all_ones = one_past - 1;

    T x = one_past;
    --x;
    if (!holds_in_place(x, all_ones)) {
        return false;
    }
    x = one_past;
    x--;
    if (!holds_in_place(x, all_ones)) {
        return false;
    }
    x = -one_past;
    ++x;
    if (!holds_in_place(x, -all_ones)) {
        return false;
    }
    return holds_in_place(~(-one_past), all_ones);
}

template <class T>
consteval bool test_add_sub() {
    const T h  = heap_value<T>();
    const T h1 = h - 1;

    T x = h;
    x -= h1;
    if (!holds_in_place(x, T(1))) {
        return false;
    }
    x = h;
    x -= x;
    if (!holds_in_place(x, T(0))) {
        return false;
    }
    x = -h;
    x += h;
    if (!holds_in_place(x, T(0))) {
        return false;
    }
    if (!holds_in_place(h - h1, T(1)) || !holds_in_place(h + -h1, T(1))) {
        return false;
    }
    if (!holds_in_place(T(h) - h1, T(1)) || !holds_in_place(h - T(h1), T(1))) {
        return false;
    }
    if (!holds_in_place(T(h) - T(h1), T(1)) || !holds_in_place(T(h1) - T(h), T(-1))) {
        return false;
    }
    if (!holds_in_place(T(h) - h, T(0)) || !holds_in_place(5 - (T(h) - h), T(5))) {
        return false;
    }
    return holds_in_place(T(-h) + h, T(0));
}

template <class T>
consteval bool test_multiply() {
    T x = heap_value<T>();
    x *= 0;
    if (!holds_in_place(x, T(0))) {
        return false;
    }
    if (!holds_in_place(heap_value<T>() * 0, T(0))) {
        return false;
    }
    // Operands whose limb counts add up past the in-place capacity, with a product that fits.
    const T half = T(1) << (T::inplace_bits / 2 - 1);
    return holds_in_place(half * half, T(1) << (T::inplace_bits - 2));
}

template <class T>
consteval bool test_divide() {
    const T h = heap_value<T>();

    T x = h;
    x /= x;
    if (!holds_in_place(x, T(1))) {
        return false;
    }
    x = h;
    x %= x;
    if (!holds_in_place(x, T(0))) {
        return false;
    }
    x = h;
    x /= h;
    if (!holds_in_place(x, T(1))) {
        return false;
    }
    x = h + 12;
    x %= 7;
    if (!holds_in_place(x, T(6))) { // 2^300 % 7 == 1
        return false;
    }
    x = h + 5;
    x %= h;
    if (!holds_in_place(x, T(5))) {
        return false;
    }
    if (!holds_in_place(T(h) / h, T(1)) || !holds_in_place((h + 5) % h, T(5))) {
        return false;
    }
    if (!holds_in_place(T(h + 12) % 7, T(6))) {
        return false;
    }
    const auto [q, r] = bb::div_rem_to_zero(h + 5, h);
    return holds_in_place(q, T(1)) && holds_in_place(r, T(5));
}

template <class T>
consteval bool test_bitwise() {
    const T h = heap_value<T>();

    T x = h + 3;
    x &= 0xFF;
    if (!holds_in_place(x, T(3))) {
        return false;
    }
    x = h + 3;
    x ^= h;
    if (!holds_in_place(x, T(3))) {
        return false;
    }
    x = h;
    x ^= x;
    if (!holds_in_place(x, T(0))) {
        return false;
    }
    if (!holds_in_place((h + 3) ^ h, T(3)) || !holds_in_place(T(h + 3) & 0xFF, T(3))) {
        return false;
    }
    return holds_in_place(T(-5) | 3, T(-5)) && holds_in_place(T(-5) & -3, T(-7));
}

template <class T>
consteval bool test_numeric() {
    const T h = heap_value<T>();
    if (!holds_in_place(bb::gcd(T(h), h + 2), T(2)) || !holds_in_place(bb::gcd(h, h + 2), T(2))) {
        return false;
    }
    if (!holds_in_place(bb::midpoint(T(h), -h), T(0)) || !holds_in_place(bb::midpoint(h, -h), T(0))) {
        return false;
    }
    return holds_in_place(bb::lcm(T(h) - h, 5), T(0));
}

template <class T>
consteval bool test_from_chars() {
    // Base 10, base 16 (whole digits per limb), and base 8 (digits straddle limbs), each with
    // more leading zeros than the in-place storage holds.
    constexpr char dec[] = "0000000000000000000000000000000000000000000000000000000000000000012";
    constexpr char hex[] = "0000000000000000000000000000000000000000000000000000000000000000012";
    constexpr char oct[] = "0000000000000000000000000000000000000000000000000000000000000000012";

    T x = heap_value<T>();
    if (bb::from_chars(dec, dec + sizeof(dec) - 1, x).ec != std::errc{} || !holds_in_place(x, T(12))) {
        return false;
    }
    T y;
    if (bb::from_chars(hex, hex + sizeof(hex) - 1, y, 16).ec != std::errc{} || !holds_in_place(y, T(18))) {
        return false;
    }
    T z = heap_value<T>();
    return bb::from_chars(oct, oct + sizeof(oct) - 1, z, 8).ec == std::errc{} && holds_in_place(z, T(10));
}

#define STATIC_ASSERT_ALL_WIDTHS(test)             \
    static_assert(test<bb::big_int>());            \
    static_assert(test<bb::basic_big_int<128>>()); \
    static_assert(test<bb::basic_big_int<192>>())

STATIC_ASSERT_ALL_WIDTHS(test_assignment);
STATIC_ASSERT_ALL_WIDTHS(test_construction);
STATIC_ASSERT_ALL_WIDTHS(test_shift);
STATIC_ASSERT_ALL_WIDTHS(test_increment_decrement);
STATIC_ASSERT_ALL_WIDTHS(test_add_sub);
STATIC_ASSERT_ALL_WIDTHS(test_multiply);
STATIC_ASSERT_ALL_WIDTHS(test_divide);
STATIC_ASSERT_ALL_WIDTHS(test_bitwise);
STATIC_ASSERT_ALL_WIDTHS(test_numeric);
STATIC_ASSERT_ALL_WIDTHS(test_from_chars);

#undef STATIC_ASSERT_ALL_WIDTHS

} // namespace

// The run-time results agree with the constants built during constant evaluation.
TEST(ConstevalShrink, RuntimeMatchesConstantEvaluation) {
    bb::big_int x = bb::big_int(1) << 200;
    EXPECT_EQ(x >> 190, issue_410::a);
    EXPECT_EQ(std::move(x) >> 190, issue_410::a);
    bb::big_int y = bb::big_int(1) << 200;
    y             = 5;
    EXPECT_EQ(y, issue_410::b);
}

// The shrink is limited to constant evaluation: at run time a held buffer is kept for reuse.
TEST(ConstevalShrink, RuntimeKeepsBuffer) {
    bb::big_int x = bb::big_int(1) << 200;
    x             = 5;
    EXPECT_EQ(x, 5);
    EXPECT_FALSE(bb::is_inplace(x));
}
