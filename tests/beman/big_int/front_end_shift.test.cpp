// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Differential tests for the single-pass shift front ends (shift_left / shift_right in place and the
// assign_shifted_* fresh-result paths) against a plain limb-vector reference, for big_int,
// basic_big_int<128> and basic_big_int<256>.

#include <cstddef>
#include <limits>
#include <random>
#include <utility>
#include <vector>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "front_end_reference.hpp"
#include "testing.hpp"

namespace bb = BEMAN_BIG_INT_NAMESPACE;

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace front_end_test {

template <class T>
void check_shifts(const ref& a, const std::size_t s) {
    const T    ta  = make<T>(a);
    const ref  shl = ref_shl(a, s);
    const ref  shr = ref_shr(a, s);
    SCOPED_TRACE("a=" + hex(a) + " s=" + std::to_string(s));

    EXPECT_TRUE(matches(ta << s, shl));
    EXPECT_TRUE(matches(ta >> s, shr));
    EXPECT_TRUE(matches(ta, a)) << "operand modified";
    EXPECT_TRUE(matches(T(ta) << s, shl));
    EXPECT_TRUE(matches(T(ta) >> s, shr));
    T c = ta;
    c <<= s;
    EXPECT_TRUE(matches(c, shl));
    T d = ta;
    d >>= s;
    EXPECT_TRUE(matches(d, shr));

    // A fresh result lands inline exactly when it fits, even from a heap source.
    const T l = ta << s;
    EXPECT_EQ(is_inplace(l), shl.mag.size() <= T::inplace_capacity);
    const T r = ta >> s;
    EXPECT_EQ(is_inplace(r), shr.mag.size() <= T::inplace_capacity);
    // An inline value shifted in place stays inline when the result fits.
    if (is_inplace(ta) && shl.mag.size() <= T::inplace_capacity) {
        EXPECT_TRUE(is_inplace(c));
    }
}

template <class T>
void shift_sweep() {
    std::mt19937_64 rng(1357);
    const std::size_t cap = T::inplace_capacity;
    for (const std::size_t n : sizes_for(cap)) {
        if (n > 8 && n != cap && n != cap + 1) {
            continue;
        }
        for (unsigned p = 0; p < 6; ++p) {
            for (const bool neg : {false, true}) {
                const ref a = gen(rng, n, p, neg);
                std::vector<std::size_t> shifts = {0,         1,         W - 1,     W,         W + 1,     2 * W,
                                                   2 * W + 13, 3 * W - 1, n * W - 1, n * W,     n * W + 1, n * W + 5,
                                                   cap * W,    cap * W + 13, (cap + 1) * W, (cap + 2) * W + 1};
                for (const std::size_t s : shifts) {
                    if (s > 100000) {
                        continue; // n == 0 underflow of n * W - 1
                    }
                    check_shifts<T>(a, s);
                }
            }
        }
    }
}

// Every small value against every small shift, checked against the builtin (arithmetic shift = floor).
template <class T>
void small_exhaustive() {
    for (long long x = -70; x <= 70; ++x) {
        for (unsigned s = 0; s <= 130; ++s) {
            const T tx{x};
            long long expect_r = x >> std::min(s, 62U);
            EXPECT_EQ(tx >> s, expect_r) << x << " >> " << s;
            T c = tx;
            c >>= s;
            EXPECT_EQ(c, expect_r) << x << " >>= " << s;
            EXPECT_TRUE(tail_is_zero(c));
            if (s <= 40) {
                EXPECT_EQ(tx << s, x * (1LL << s)) << x << " << " << s;
                T e = tx;
                e <<= s;
                EXPECT_EQ(e, x * (1LL << s)) << x << " <<= " << s;
                EXPECT_TRUE(tail_is_zero(e));
            }
        }
    }
}

// Shifting a value that exactly fits the inline capacity must not spill unless the result needs it.
template <class T>
void left_shift_exact_sizing() {
    constexpr std::size_t cap_bits = T::inplace_capacity * W;
    for (const bool neg : {false, true}) {
        // 1 << (cap_bits - 1) is the top bit of the inline buffer.
        T one{neg ? -1 : 1};
        T top = one << (cap_bits - 1);
        EXPECT_TRUE(is_inplace(top)) << "copy form spilled";
        T inplace = one;
        inplace <<= (cap_bits - 1);
        EXPECT_TRUE(is_inplace(inplace)) << "in-place form spilled";
        EXPECT_EQ(top, inplace);
        T next = one << cap_bits;
        EXPECT_FALSE(is_inplace(next));
        T moved = std::move(one) << (cap_bits - 1);
        EXPECT_TRUE(is_inplace(moved)) << "rvalue form spilled";
        EXPECT_TRUE(tail_is_zero(top) && tail_is_zero(inplace) && tail_is_zero(moved));
    }
}

// Aliasing and identity shifts: the operand and the destination are the same object.
template <class T>
void shift_aliasing() {
    std::mt19937_64 rng(97531);
    for (const std::size_t n : sizes_for(T::inplace_capacity)) {
        for (unsigned p = 0; p < 6; ++p) {
            for (const bool neg : {false, true}) {
                const ref a = gen(rng, n, p, neg);
                SCOPED_TRACE("a=" + hex(a));
                T x = make<T>(a);
                x = x << 0;
                EXPECT_TRUE(matches(x, a)) << "x = x << 0";
                x = make<T>(a);
                x = x >> 0;
                EXPECT_TRUE(matches(x, a)) << "x = x >> 0";
                x = make<T>(a);
                x <<= 0;
                EXPECT_TRUE(matches(x, a)) << "x <<= 0";
                x = make<T>(a);
                x <<= W;
                EXPECT_TRUE(matches(x, ref_shl(a, W))) << "x <<= W";
                x = make<T>(a);
                x >>= W;
                EXPECT_TRUE(matches(x, ref_shr(a, W))) << "x >>= W";
                x = make<T>(a);
                x = x << W;
                EXPECT_TRUE(matches(x, ref_shl(a, W))) << "x = x << W";
                x = make<T>(a);
                x = x >> W;
                EXPECT_TRUE(matches(x, ref_shr(a, W))) << "x = x >> W";
            }
        }
    }
}

// ----- constant evaluation: multi-limb values (heap storage under consteval) -----

// 2^(k * W) - 1, parsed from hex so no operation under test builds it.
// ----- constant evaluation: multi-limb values (heap storage under consteval) -----

template <class T>
constexpr bool cx_shift(unsigned k) {
    const T        x  = cx_ones<T>(k);
    const unsigned kw = k * W;
    for (const unsigned s : {1U, W - 1, W, W + 1, 2 * W + 13}) {
        if (((x << s) >> s) != x || !((x << s) == (x * (T{1} << s)))) {
            return false;
        }
        T c = x;
        c <<= s;
        const T cc = c;
        c >>= s;
        if (c != x || !tail_is_zero(c) || (cc >> s) != x) {
            return false;
        }
        // negative floor rounding: low bits of an all-ones value are always discarded when s < kw
        if (s < kw) {
            const T q = x >> s;
            if (((-x) >> s) != -(q + 1) || !tail_is_zero(-x >> s)) {
                return false;
            }
            T n = -x;
            n >>= s;
            if (n != -(q + 1)) {
                return false;
            }
        }
    }
    if ((x >> kw) != 0 || ((-x) >> kw) != -1 || ((-x) >> (kw + 5)) != -1) {
        return false;
    }
    T z = x;
    z >>= kw;
    T nz = -x;
    nz >>= kw;
    return z == 0 && tail_is_zero(z) && nz == -1 && tail_is_zero(nz);
}

} // namespace front_end_test
BEMAN_BIG_INT_END_NAMESPACE

namespace fe = BEMAN_BIG_INT_NAMESPACE::front_end_test;

#define FRONT_END_TYPES(CASE)               \
    CASE(BigInt, bb::big_int)               \
    CASE(Inline128, bb::basic_big_int<128>) \
    CASE(Inline256, bb::basic_big_int<256>)

#define FRONT_END_TEST(SUFFIX, TYPE)                                                \
    TEST(FrontEndShift##SUFFIX, Sweep) { fe::shift_sweep<TYPE>(); }                  \
    TEST(FrontEndShift##SUFFIX, Aliasing) { fe::shift_aliasing<TYPE>(); }                \
    TEST(FrontEndShift##SUFFIX, SmallExhaustive) { fe::small_exhaustive<TYPE>(); }   \
    TEST(FrontEndShift##SUFFIX, LeftShiftExactSizing) { fe::left_shift_exact_sizing<TYPE>(); }
FRONT_END_TYPES(FRONT_END_TEST)

#define FRONT_END_STATIC(TYPE)                                       \
    static_assert(fe::cx_shift<TYPE>(1));                             \
    static_assert(fe::cx_shift<TYPE>(2));                             \
    static_assert(fe::cx_shift<TYPE>(TYPE::inplace_capacity));        \
    static_assert(fe::cx_shift<TYPE>(TYPE::inplace_capacity + 1));    \
    static_assert(fe::cx_shift<TYPE>(TYPE::inplace_capacity + 3))
FRONT_END_STATIC(bb::big_int);
FRONT_END_STATIC(bb::basic_big_int<128>);
FRONT_END_STATIC(bb::basic_big_int<256>);
