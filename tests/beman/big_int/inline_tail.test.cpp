// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Regression tests for the inline-tail invariant: for an in-place value, the limbs in
// [limb_count, inplace_capacity) must be zero. They only matter when inplace_capacity > 1,
// hence basic_big_int<128> and basic_big_int<256>. Reused heap destinations of base 8/32
// from_chars are covered as well.

#include <charconv>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

namespace bb = BEMAN_BIG_INT_NAMESPACE;

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace inline_tail_test {

inline constexpr unsigned limb_width = static_cast<unsigned>(std::numeric_limits<uint_multiprecision_t>::digits);

// True when every limb of an in-place value past its limb count is zero.
template <class T>
constexpr bool tail_is_zero(const T& x) {
    if (!is_inplace(x)) {
        return true;
    }
    const auto* const limbs = x.representation().data();
    for (std::size_t i = x.representation().size(); i < T::inplace_capacity; ++i) {
        if (limbs[i] != 0) {
            return false;
        }
    }
    return true;
}

// 2^(k * limb_width) - 1, i.e. k limbs of all ones. Parsed from hex digits: shifting up to the full
// inline capacity can reserve a spare limb and spill to the heap.
template <class T>
constexpr T all_ones(unsigned k) {
    char digits[1024] = {};
    for (unsigned i = 0; i < k * limb_width / 4; ++i) {
        digits[i] = 'f';
    }
    T          x;
    const auto n = k * limb_width / 4;
    [[maybe_unused]] const auto r = from_chars(digits, digits + n, x, 16);
    return x;
}

template <class T>
constexpr T two_limbs() { // 2^limb_width + 7
    T x{1};
    x <<= limb_width;
    x += 7;
    return x;
}

template <class T>
constexpr bool sub_self() {
    T x = all_ones<T>(2);
    x -= x;
    return x == 0U && tail_is_zero(x);
}

template <class T>
constexpr bool xor_self() {
    T x = all_ones<T>(2);
    x ^= x;
    return x == 0U && tail_is_zero(x);
}

// Shift all_ones(k) (k = whole inline capacity) right by `limbs` whole limbs plus `extra` bits.
template <class T>
constexpr bool shr_case(unsigned limbs, unsigned extra, bool negative) {
    constexpr unsigned k = static_cast<unsigned>(T::inplace_capacity);
    T                  x = all_ones<T>(k);
    if (negative) {
        x = -x;
    }
    const unsigned s = limbs * limb_width + extra;
    x >>= s;
    const unsigned remaining = k * limb_width - s;
    T              expected{1};
    expected <<= remaining;
    if (negative) {
        expected = -expected; // floor(-(2^n - 1) / 2^s) == -2^(n - s)
    } else {
        expected -= 1;
    }
    return x == expected && tail_is_zero(x);
}

template <class T>
constexpr bool shr_all() {
    constexpr unsigned k = static_cast<unsigned>(T::inplace_capacity);
    for (const bool neg : {false, true}) {
        for (const unsigned extra : {0U, 3U}) {
            for (const unsigned limbs : {1U, 3U, k - 1U}) {
                if (limbs >= k || (limbs * limb_width + extra) >= k * limb_width) {
                    continue;
                }
                if (!shr_case<T>(limbs, extra, neg)) {
                    return false;
                }
            }
        }
        if (!shr_case<T>(k, 0, neg)) { // shift by every bit
            return false;
        }
    }
    return true;
}

template <class T>
constexpr bool shr_by_a_limb_matches_builtin() {
    T x = all_ones<T>(2);
    x >>= limb_width;
    return x == std::numeric_limits<uint_multiprecision_t>::max() && tail_is_zero(x);
}

// The quotient limbs written in place must not survive above the new limb count.
template <class T>
constexpr bool rem_by_small() {
    T x = all_ones<T>(2);
    x -= 1; // 2^(2W) - 2, which is 2 modulo 3
    x %= 3;
    if (!(x == 2U && tail_is_zero(x))) {
        return false;
    }
    T q = all_ones<T>(2);
    q -= 1;
    q /= 3;
    return q * 3 + 2 == all_ones<T>(2) - 1 && tail_is_zero(q);
}

template <class T>
constexpr bool div_rem_small_by_large() {
    const T a = two_limbs<T>();
    T       b{1};
    b <<= 3 * limb_width;
    auto r = div_rem_to_zero(a, b);
    if (!(r.quotient == 0U && tail_is_zero(r.quotient) && r.remainder == a && tail_is_zero(r.remainder))) {
        return false;
    }
    T a2 = two_limbs<T>();
    auto r2 = div_rem_to_zero(std::move(a2), b);
    return r2.quotient == 0U && tail_is_zero(r2.quotient) && r2.remainder == two_limbs<T>();
}

template <class T>
constexpr bool from_chars_into_two_limbs(int base, const char* digits, unsigned expected) {
    T x = two_limbs<T>();
    std::string_view s{digits};
    const auto       r = from_chars(s.data(), s.data() + s.size(), x, base);
    return r.ec == std::errc{} && x == expected && tail_is_zero(x);
}

template <class T>
constexpr bool from_chars_small_cases() {
    return from_chars_into_two_limbs<T>(16, "5", 5U) && from_chars_into_two_limbs<T>(8, "5", 5U) &&
           from_chars_into_two_limbs<T>(32, "5", 5U) && from_chars_into_two_limbs<T>(2, "101", 5U) &&
           from_chars_into_two_limbs<T>(4, "11", 5U) && from_chars_into_two_limbs<T>(10, "5", 5U);
}

// A heap destination full of ones, parsed into with a multi-block power-of-two base.
template <class T>
constexpr bool from_chars_reused_heap(int base, const std::string_view digits, const unsigned expected_shift) {
    T x = all_ones<T>(static_cast<unsigned>(T::inplace_capacity) + 10);
    if (is_inplace(x)) {
        return false;
    }
    const auto r = from_chars(digits.data(), digits.data() + digits.size(), x, base);
    T          fresh;
    const auto r2 = from_chars(digits.data(), digits.data() + digits.size(), fresh, base);
    T          expected{1};
    expected <<= expected_shift;
    return r.ec == std::errc{} && r2.ec == std::errc{} && x == fresh && x == expected;
}

template <class T>
constexpr bool from_chars_reused_heap_all() {
    // base 8: 3 bits per digit; base 32: 5 bits per digit; many blocks.
    return from_chars_reused_heap<T>(8, "1000000000000000000000000000000000000000", 39 * 3) &&
           from_chars_reused_heap<T>(32, "10000000000000000000000000000000000000000000", 43 * 5) &&
           from_chars_reused_heap<T>(8, "1000000000000000000000", 21 * 3) &&
           from_chars_reused_heap<T>(32, "1000000000000", 12 * 5);
}

template <class T>
constexpr bool moved_from_after_steal() {
    T src{1};
    src <<= static_cast<unsigned>(T::inplace_bits) + 10;
    T dst;
    dst = std::move(src);
    return !is_inplace(dst) && src == 0U && tail_is_zero(src);
}

} // namespace inline_tail_test
BEMAN_BIG_INT_END_NAMESPACE

namespace it = BEMAN_BIG_INT_NAMESPACE::inline_tail_test;

#define INLINE_TAIL_CASES(CASE)                                  \
    CASE(Sub128, bb::basic_big_int<128>, sub_self)               \
    CASE(Sub256, bb::basic_big_int<256>, sub_self)               \
    CASE(Xor128, bb::basic_big_int<128>, xor_self)               \
    CASE(Xor256, bb::basic_big_int<256>, xor_self)               \
    CASE(ShrAll128, bb::basic_big_int<128>, shr_all)             \
    CASE(ShrAll256, bb::basic_big_int<256>, shr_all)             \
    CASE(ShrLimb128, bb::basic_big_int<128>, shr_by_a_limb_matches_builtin) \
    CASE(ShrLimb256, bb::basic_big_int<256>, shr_by_a_limb_matches_builtin) \
    CASE(Rem128, bb::basic_big_int<128>, rem_by_small)           \
    CASE(Rem256, bb::basic_big_int<256>, rem_by_small)           \
    CASE(DivRem128, bb::basic_big_int<128>, div_rem_small_by_large) \
    CASE(DivRem256, bb::basic_big_int<256>, div_rem_small_by_large) \
    CASE(FromChars128, bb::basic_big_int<128>, from_chars_small_cases) \
    CASE(FromChars256, bb::basic_big_int<256>, from_chars_small_cases) \
    CASE(FromCharsHeap64, bb::big_int, from_chars_reused_heap_all) \
    CASE(FromCharsHeap128, bb::basic_big_int<128>, from_chars_reused_heap_all) \
    CASE(Steal64, bb::big_int, moved_from_after_steal)           \
    CASE(Steal128, bb::basic_big_int<128>, moved_from_after_steal) \
    CASE(Steal256, bb::basic_big_int<256>, moved_from_after_steal)

#define INLINE_TAIL_TEST(NAME, TYPE, FN) \
    TEST(InlineTail, NAME) { EXPECT_TRUE(it::FN<TYPE>()); }
INLINE_TAIL_CASES(INLINE_TAIL_TEST)

// Constant evaluation of the cases that need no heap-limb reads beyond the span.
static_assert(it::sub_self<bb::basic_big_int<128>>());
static_assert(it::xor_self<bb::basic_big_int<128>>());
static_assert(it::shr_all<bb::basic_big_int<128>>());
static_assert(it::shr_all<bb::basic_big_int<256>>());
static_assert(it::rem_by_small<bb::basic_big_int<128>>());
static_assert(it::div_rem_small_by_large<bb::basic_big_int<128>>());
static_assert(it::from_chars_small_cases<bb::basic_big_int<128>>());
static_assert(it::from_chars_reused_heap_all<bb::big_int>());
static_assert(it::moved_from_after_steal<bb::basic_big_int<128>>());
