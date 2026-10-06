// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Differential tests for the single-pass add/sub front ends (add_in_place, add_into) against a plain
// limb-vector reference, for big_int, basic_big_int<128> and basic_big_int<256>.

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
void check_add_sub(const ref& a, const ref& b) {
    const T    ta       = make<T>(a);
    const T    tb       = make<T>(b);
    const ref  sum      = ref_add(a, b);
    const ref  diff     = ref_add(a, ref_neg(b));
    const auto describe = [&] { return "a=" + hex(a) + " b=" + hex(b); };
    SCOPED_TRACE(describe());

    // copy_copy (add_into)
    EXPECT_TRUE(matches(ta + tb, sum));
    EXPECT_TRUE(matches(ta - tb, diff));
    EXPECT_TRUE(matches(ta, a)) << "operand modified";
    EXPECT_TRUE(matches(tb, b)) << "operand modified";

    // rvalue forms (storage reuse)
    EXPECT_TRUE(matches(T(ta) + tb, sum));
    EXPECT_TRUE(matches(ta + T(tb), sum));
    EXPECT_TRUE(matches(T(ta) + T(tb), sum));
    EXPECT_TRUE(matches(T(ta) - tb, diff));
    EXPECT_TRUE(matches(ta - T(tb), diff));
    EXPECT_TRUE(matches(T(ta) - T(tb), diff));

    // compound forms
    T c = ta;
    c += tb;
    EXPECT_TRUE(matches(c, sum));
    T d = ta;
    d -= tb;
    EXPECT_TRUE(matches(d, diff));
    c -= tb;
    EXPECT_TRUE(matches(c, a)) << "(a + b) - b";
    d += tb;
    EXPECT_TRUE(matches(d, a)) << "(a - b) + b";

    // A fresh result from operands that fit inline lands inline exactly when the result fits.
    if (std::max(a.mag.size(), b.mag.size()) <= T::inplace_capacity) {
        const T r = ta + tb;
        EXPECT_EQ(is_inplace(r), sum.mag.size() <= T::inplace_capacity);
        const T r2 = ta - tb;
        EXPECT_EQ(is_inplace(r2), diff.mag.size() <= T::inplace_capacity);
    }
}

template <class T>
void check_integer_forms(const ref& a, long long v, unsigned long long u) {
    const T   ta = make<T>(a);
    const ref rv = [&] {
        ref r;
        r.neg = v < 0;
        if (v != 0) {
            r.mag = {static_cast<limb>(v < 0 ? 0 - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v))};
        }
        return r;
    }();
    const ref ru = [&] {
        ref r;
        if (u != 0) {
            r.mag = {static_cast<limb>(u)};
        }
        return r;
    }();
    SCOPED_TRACE("a=" + hex(a) + " v=" + std::to_string(v));
    EXPECT_TRUE(matches(ta + v, ref_add(a, rv)));
    EXPECT_TRUE(matches(v + ta, ref_add(rv, a)));
    EXPECT_TRUE(matches(ta - v, ref_add(a, ref_neg(rv))));
    EXPECT_TRUE(matches(v - ta, ref_add(rv, ref_neg(a))));
    EXPECT_TRUE(matches(T(ta) + v, ref_add(a, rv)));
    EXPECT_TRUE(matches(v - T(ta), ref_add(rv, ref_neg(a))));
    EXPECT_TRUE(matches(ta + u, ref_add(a, ru)));
    EXPECT_TRUE(matches(u - ta, ref_add(ru, ref_neg(a))));
    T c = ta;
    c += v;
    EXPECT_TRUE(matches(c, ref_add(a, rv)));
    c = ta;
    c -= u;
    EXPECT_TRUE(matches(c, ref_add(a, ref_neg(ru))));
}

template <class T>
void add_sub_sweep() {
    std::mt19937_64 rng(12345);
    const auto      sizes = sizes_for(T::inplace_capacity);
    for (const std::size_t na : sizes) {
        for (const std::size_t nb : sizes) {
            for (unsigned pa = 0; pa < 6; ++pa) {
                for (unsigned pb = 0; pb < 6; ++pb) {
                    for (unsigned signs = 0; signs < 4; ++signs) {
                        // Subsample the pattern product for the larger sizes to keep the run short.
                        if (na + nb > 14 && (pa + pb + signs) % 3 != 0) {
                            continue;
                        }
                        check_add_sub<T>(gen(rng, na, pa, (signs & 1U) != 0), gen(rng, nb, pb, (signs & 2U) != 0));
                    }
                }
            }
        }
    }
}

template <class T>
void add_sub_ripple() {
    // a = 2^(kW) - 1 style operands against +-1 and +-2^(jW): the carry or borrow ripples through every
    // limb, including exactly at and one past the inline capacity.
    std::mt19937_64 rng(777);
    for (std::size_t k = 1; k <= 24; ++k) {
        const ref ones = gen(rng, k, 1, false);
        const ref pow  = gen(rng, k + 1, 2, false);
        for (const bool n1 : {false, true}) {
            for (const bool n2 : {false, true}) {
                ref a = ones;
                ref b = ref{n2, {1}};
                a.neg = n1;
                check_add_sub<T>(a, b);
                check_add_sub<T>(b, a);
                ref c = pow;
                c.neg = n2;
                check_add_sub<T>(a, c);
                check_add_sub<T>(c, a);
                check_add_sub<T>(c, ref{n1, {1}});
            }
        }
    }
}

template <class T>
void integer_sweep() {
    std::mt19937_64 rng(2468);
    const long long vs[] = {0, 1, -1, 5, -5, std::numeric_limits<long long>::max(), std::numeric_limits<long long>::min()};
    for (const std::size_t n : sizes_for(T::inplace_capacity)) {
        for (unsigned p = 0; p < 6; ++p) {
            for (const bool neg : {false, true}) {
                for (const long long v : vs) {
                    check_integer_forms<T>(gen(rng, n, p, neg), v, static_cast<unsigned long long>(v));
                }
            }
        }
    }
}

// A two-limb integer operand whose high limb is zero is not canonical; the front ends must trim it.
template <class T>
void wide_integer_operands() {
#ifdef __SIZEOF_INT128__
    if constexpr (W != 64) {
        return; // the split below assumes 64-bit limbs
    }
    std::mt19937_64 rng(4242);
    for (const std::size_t n : {0U, 1U, 2U, 3U, 5U}) {
        for (const bool neg : {false, true}) {
            const ref a = gen(rng, n, 0, neg);
            const T   ta = make<T>(a);
            for (const __int128 v : {static_cast<__int128>(0), static_cast<__int128>(5), static_cast<__int128>(-5),
                                     (static_cast<__int128>(1) << 70) + 3, -((static_cast<__int128>(1) << 90) + 1)}) {
                const unsigned __int128 m = v < 0 ? 0 - static_cast<unsigned __int128>(v) : static_cast<unsigned __int128>(v);
                ref                     rv;
                rv.neg = v < 0;
                rv.mag = {static_cast<limb>(m), static_cast<limb>(m >> 64)};
                trim(rv.mag);
                SCOPED_TRACE("a=" + hex(a));
                EXPECT_TRUE(matches(ta + v, ref_add(a, rv)));
                EXPECT_TRUE(matches(v + ta, ref_add(rv, a)));
                EXPECT_TRUE(matches(ta - v, ref_add(a, ref_neg(rv))));
                EXPECT_TRUE(matches(v - ta, ref_add(rv, ref_neg(a))));
                EXPECT_TRUE(matches(T(ta) + v, ref_add(a, rv)));
                EXPECT_TRUE(matches(v - T(ta), ref_add(rv, ref_neg(a))));
                T c = ta;
                c += v;
                EXPECT_TRUE(matches(c, ref_add(a, rv)));
                c = ta;
                c -= v;
                EXPECT_TRUE(matches(c, ref_add(a, ref_neg(rv))));
            }
        }
    }
#endif
}

template <class T>
void aliasing() {
    std::mt19937_64 rng(97531);
    for (const std::size_t n : sizes_for(T::inplace_capacity)) {
        for (unsigned p = 0; p < 6; ++p) {
            for (const bool neg : {false, true}) {
                const ref a = gen(rng, n, p, neg);
                SCOPED_TRACE("a=" + hex(a));
                T x = make<T>(a);
                x += x;
                EXPECT_TRUE(matches(x, ref_add(a, a))) << "x += x";
                x = make<T>(a);
                x -= x;
                EXPECT_TRUE(matches(x, ref{})) << "x -= x";
                x = make<T>(a);
                x += -x;
                EXPECT_TRUE(matches(x, ref{})) << "x += -x";
                x = make<T>(a);
                x -= -x;
                EXPECT_TRUE(matches(x, ref_add(a, a))) << "x -= -x";
                x = make<T>(a);
                x = x + x;
                EXPECT_TRUE(matches(x, ref_add(a, a))) << "x = x + x";
                x = make<T>(a);
                x = x - x;
                EXPECT_TRUE(matches(x, ref{})) << "x = x - x";
            }
        }
    }
}

// ----- constant evaluation: multi-limb values (heap storage under consteval) -----

template <class T>
constexpr bool cx_add_sub(unsigned k) {
    const T x = cx_ones<T>(k);
    const T y = cx_ones<T>(k + 1);
    const unsigned kw = k * W;

    // carry ripples through every limb
    if (!cx_pow2<T>(x + 1, kw) || !cx_pow2<T>(1 + x, kw) || !cx_pow2<T>(T(x) + T{1}, kw)) {
        return false;
    }
    if (((x + 1) - 1) != x || (x - (x - 1)) != 1 || ((x + x) != (x << 1))) {
        return false;
    }
    // borrow ripples, and the operand-order / sign-flip paths
    const T p = x + 1;
    if (!(p - 1 == x) || !(1 - p == -x) || !(x - p == -1) || !(-x - 1 == -p) || !(-x + p == 1)) {
        return false;
    }
    if (!(y - x == (cx_ones<T>(1) << kw)) || !(x - y == -(cx_ones<T>(1) << kw)) || !(y + -x == y - x)) {
        return false;
    }
    // aliasing
    T a = x;
    a += a;
    if (a != (x << 1)) {
        return false;
    }
    a -= a;
    if (a != 0 || !tail_is_zero(a)) {
        return false;
    }
    a = x;
    a += -a;
    if (a != 0) {
        return false;
    }
    a = x;
    a = a + a;
    if (a != (x << 1)) {
        return false;
    }
    // compound on a growing value
    T g{1};
    for (unsigned i = 0; i < k; ++i) {
        g += g;
    }
    g -= 1;
    g += g + 1; // 2 * 2^k - 1
    return g == (T{1} << (k + 1)) - 1;
}

} // namespace front_end_test
BEMAN_BIG_INT_END_NAMESPACE

namespace fe = BEMAN_BIG_INT_NAMESPACE::front_end_test;

#define FRONT_END_TYPES(CASE)               \
    CASE(BigInt, bb::big_int)               \
    CASE(Inline128, bb::basic_big_int<128>) \
    CASE(Inline256, bb::basic_big_int<256>)

#define FRONT_END_TEST(SUFFIX, TYPE)                                          \
    TEST(FrontEndAddSub##SUFFIX, Sweep) { fe::add_sub_sweep<TYPE>(); }          \
    TEST(FrontEndAddSub##SUFFIX, Ripple) { fe::add_sub_ripple<TYPE>(); }        \
    TEST(FrontEndAddSub##SUFFIX, IntegerOperands) { fe::integer_sweep<TYPE>(); } \
    TEST(FrontEndAddSub##SUFFIX, WideIntegerOperands) { fe::wide_integer_operands<TYPE>(); } \
    TEST(FrontEndAddSub##SUFFIX, Aliasing) { fe::aliasing<TYPE>(); }
FRONT_END_TYPES(FRONT_END_TEST)

#define FRONT_END_STATIC(TYPE)                                            \
    static_assert(fe::cx_add_sub<TYPE>(1));                                \
    static_assert(fe::cx_add_sub<TYPE>(2));                                \
    static_assert(fe::cx_add_sub<TYPE>(TYPE::inplace_capacity));           \
    static_assert(fe::cx_add_sub<TYPE>(TYPE::inplace_capacity + 1));       \
    static_assert(fe::cx_add_sub<TYPE>(TYPE::inplace_capacity + 3))
FRONT_END_STATIC(bb::big_int);
FRONT_END_STATIC(bb::basic_big_int<128>);
FRONT_END_STATIC(bb::basic_big_int<256>);
