// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Front ends of multiplication and division (operator*, *=, /, %, /=, %=, div_rem_to_zero) against a
// limb-level reference: aliasing, results at and one past the inline capacity, every operand size from
// 0 to 20 limbs, shapes straddling the schoolbook / divide-and-conquer / Barrett gates, and constant
// evaluation of multi-limb products and divisions.

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <beman/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace muldiv_frontend_test {

using limb_t  = uint_multiprecision_t;
using limbs_t = std::vector<limb_t>;

inline constexpr std::size_t limb_bits = static_cast<std::size_t>(std::numeric_limits<limb_t>::digits);

// Random magnitude of exactly `n` limbs with a nonzero top limb; n == 0 gives the empty (zero) magnitude.
inline limbs_t random_mag(const std::size_t n, std::mt19937_64& rng) {
    limbs_t v(n);
    for (auto& x : v) {
        x = static_cast<limb_t>(rng());
    }
    if (n != 0 && v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

template <class T>
T make(const limbs_t& mag, const bool neg) {
    if (mag.empty()) {
        return T{};
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string           s;
    if (neg) {
        s.push_back('-');
    }
    for (std::size_t i = mag.size(); i-- > 0;) {
        for (std::size_t d = limb_bits / 4; d-- > 0;) {
            s.push_back(digits[(mag[i] >> (4 * d)) & 0xFU]);
        }
    }
    T x;
    const auto [p, ec] = from_chars(s.data(), s.data() + s.size(), x, 16);
    EXPECT_TRUE(ec == std::errc{} && p == s.data() + s.size());
    return x;
}

template <class T>
limbs_t mag_of(const T& x) {
    const auto rep = x.representation();
    limbs_t    v(rep.begin(), rep.end());
    while (!v.empty() && v.back() == 0) {
        v.pop_back();
    }
    return v;
}

inline limbs_t ref_mul(const limbs_t& a, const limbs_t& b) {
    if (a.empty() || b.empty()) {
        return {};
    }
    limbs_t r(a.size() + b.size(), limb_t{0});
    detail::multiply_long(std::span<limb_t>{r}, std::span<const limb_t>{a}, std::span<const limb_t>{b});
    while (!r.empty() && r.back() == 0) {
        r.pop_back();
    }
    return r;
}

inline limbs_t ref_add(const limbs_t& a, const limbs_t& b) {
    limbs_t r(std::max(a.size(), b.size()) + 1, limb_t{0});
    bool    carry = false;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const limb_t x        = i < a.size() ? a[i] : limb_t{0};
        const limb_t y        = i < b.size() ? b[i] : limb_t{0};
        const auto [sum, out] = detail::carrying_add(x, y, carry);
        r[i]                  = sum;
        carry                 = out;
    }
    while (!r.empty() && r.back() == 0) {
        r.pop_back();
    }
    return r;
}

// -1, 0, 1 for |a| <=> |b|.
inline int ref_cmp(const limbs_t& a, const limbs_t& b) {
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

// True when every in-place limb past the limb count is zero (the invariant behind inplace_to_bit_uint).
template <class T>
bool tail_is_zero(const T& x) {
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

template <class T>
::testing::AssertionResult is_value(const T& x, const limbs_t& mag, const bool neg) {
    if (mag_of(x) != mag) {
        return ::testing::AssertionFailure()
               << "magnitude differs (got " << mag_of(x).size() << " limbs, want " << mag.size() << ")";
    }
    if ((x < 0) != (neg && !mag.empty())) {
        return ::testing::AssertionFailure() << "sign differs";
    }
    if (!is_normalized(x)) {
        return ::testing::AssertionFailure() << "not normalized";
    }
    if (!tail_is_zero(x)) {
        return ::testing::AssertionFailure() << "stale inline tail";
    }
    if (x != make<T>(mag, neg)) {
        return ::testing::AssertionFailure() << "operator== disagrees with the limbs";
    }
    return ::testing::AssertionSuccess();
}

// Checks every multiplication form of a * b against the reference.
template <class T>
void check_multiply(const limbs_t& am, const bool an, const limbs_t& bm, const bool bn) {
    const limbs_t pm  = ref_mul(am, bm);
    const bool    neg = an != bn;
    const T       a   = make<T>(am, an);
    const T       b   = make<T>(bm, bn);
    const T       a0  = a;
    const T       b0  = b;

    EXPECT_TRUE(is_value(a * b, pm, neg)) << "a*b la=" << am.size() << " lb=" << bm.size();
    EXPECT_TRUE(is_value(T{a} * b, pm, neg)) << "rvalue a*b la=" << am.size() << " lb=" << bm.size();
    T c = a;
    c *= b;
    EXPECT_TRUE(is_value(c, pm, neg)) << "a*=b la=" << am.size() << " lb=" << bm.size();
    // Reuse a destination that already holds a different, possibly longer, value.
    std::mt19937_64 rng{1};
    T               d = make<T>(random_mag(24, rng), true);
    d                 = a;
    d *= b;
    EXPECT_TRUE(is_value(d, pm, neg)) << "reused a*=b la=" << am.size() << " lb=" << bm.size();
    EXPECT_EQ(a, a0);
    EXPECT_EQ(b, b0);
}

// Checks every division form of a / b, a % b and div_rem_to_zero against the reference identities.
template <class T>
void check_divide(const limbs_t& am, const bool an, const limbs_t& bm, const bool bn) {
    ASSERT_FALSE(bm.empty());
    const T a  = make<T>(am, an);
    const T b  = make<T>(bm, bn);
    const T a0 = a;
    const T b0 = b;

    const T q = a / b;
    const T r = a % b;

    // |r| < |b|, sign(r) follows a, q * b + r == a.
    const limbs_t qm = mag_of(q);
    const limbs_t rm = mag_of(r);
    EXPECT_LT(ref_cmp(rm, bm), 0) << "remainder not below divisor, la=" << am.size() << " lb=" << bm.size();
    EXPECT_EQ(ref_add(ref_mul(qm, bm), rm), am) << "q*b+r != a, la=" << am.size() << " lb=" << bm.size();
    EXPECT_TRUE(is_value(q, qm, an != bn));
    EXPECT_TRUE(is_value(r, rm, an));

    const auto both = div_rem_to_zero(a, b);
    EXPECT_EQ(both.quotient, q) << "div_rem_to_zero quotient, la=" << am.size() << " lb=" << bm.size();
    EXPECT_EQ(both.remainder, r) << "div_rem_to_zero remainder, la=" << am.size() << " lb=" << bm.size();
    EXPECT_TRUE(is_value(both.quotient, qm, an != bn));
    EXPECT_TRUE(is_value(both.remainder, rm, an));

    T c = a;
    c /= b;
    EXPECT_TRUE(is_value(c, qm, an != bn)) << "a/=b la=" << am.size() << " lb=" << bm.size();
    T d = a;
    d %= b;
    EXPECT_TRUE(is_value(d, rm, an)) << "a%=b la=" << am.size() << " lb=" << bm.size();
    EXPECT_EQ(a, a0);
    EXPECT_EQ(b, b0);
}

template <class T>
void differential_all_sizes() {
    std::mt19937_64 rng{0x6d756c64u};
    for (std::size_t la = 0; la <= 20; ++la) {
        for (std::size_t lb = 0; lb <= 20; ++lb) {
            const limbs_t am = random_mag(la, rng);
            const limbs_t bm = random_mag(lb, rng);
            for (const bool an : {false, true}) {
                for (const bool bn : {false, true}) {
                    check_multiply<T>(am, an, bm, bn);
                    if (lb != 0) {
                        check_divide<T>(am, an, bm, bn);
                    }
                }
            }
        }
    }
}

TEST(MulDivFrontend, DifferentialBigInt) { differential_all_sizes<big_int>(); }
TEST(MulDivFrontend, DifferentialInline128) { differential_all_sizes<basic_big_int<128>>(); }
TEST(MulDivFrontend, DifferentialInline256) { differential_all_sizes<basic_big_int<256>>(); }

// Divisor and dividend of related sizes: below, at, and above each gate of the dispatcher.
template <class T>
void gate_shapes() {
    std::mt19937_64                                  rng{0x6761746u};
    std::vector<std::pair<std::size_t, std::size_t>> shapes;
    const std::size_t                                bz  = detail::burnikel_ziegler_cutoff;
    const std::size_t                                off = detail::burnikel_ziegler_offset;
    for (const std::size_t s : {std::size_t{2}, std::size_t{3}, bz - 1, bz, bz + 1}) {
        for (const std::size_t m : {s, s + 1, s + off - 1, s + off, s + off + 1, 3 * s + 1}) {
            shapes.emplace_back(m, s);
        }
    }
    const std::size_t c = detail::barrett_march_cutoff;
    for (const std::size_t s : {c - 1, c, c + 1}) {
        for (const std::size_t m : {16 * s - 1, 16 * s, 16 * s + 5}) {
            shapes.emplace_back(m, s);
        }
    }
    for (const auto& [m, s] : shapes) {
        const limbs_t am = random_mag(m, rng);
        const limbs_t bm = random_mag(s, rng);
        check_divide<T>(am, false, bm, false);
        check_divide<T>(am, true, bm, false);
        check_divide<T>(am, false, bm, true);
        check_multiply<T>(am, false, bm, true);
    }
}

TEST(MulDivFrontend, GateShapesBigInt) { gate_shapes<big_int>(); }
TEST(MulDivFrontend, GateShapesInline128) { gate_shapes<basic_big_int<128>>(); }

// Products and quotients landing exactly at, and one limb past, the inline capacity.
template <class T>
void inline_capacity_edges() {
    std::mt19937_64       rng{0xed6eu};
    constexpr std::size_t cap = T::inplace_capacity;
    for (const std::size_t result_limbs : {cap - 1, cap, cap + 1, cap + 2}) {
        if (result_limbs < 2) {
            continue;
        }
        for (const bool full : {false, true}) {
            // a (result_limbs - 1 limbs) * b (1 limb): all-ones forces a carry limb, a small b does not.
            limbs_t am = random_mag(result_limbs - 1, rng);
            limbs_t bm = {full ? ~limb_t{0} : limb_t{3}};
            if (full) {
                std::ranges::fill(am, ~limb_t{0});
            }
            check_multiply<T>(am, false, bm, false);
            check_multiply<T>(bm, true, am, false);
            // And the matching division (multi-limb divisor too).
            const limbs_t big = random_mag(result_limbs + 1, rng);
            check_divide<T>(big, false, bm, false);
            check_divide<T>(big, true, am.size() >= 2 ? am : random_mag(2, rng), false);
        }
    }
}

TEST(MulDivFrontend, InlineCapacityEdgesBigInt) { inline_capacity_edges<big_int>(); }
TEST(MulDivFrontend, InlineCapacityEdges128) { inline_capacity_edges<basic_big_int<128>>(); }
TEST(MulDivFrontend, InlineCapacityEdges256) { inline_capacity_edges<basic_big_int<256>>(); }

// Aliasing: the destination is also an operand, and divisors that are copies of the dividend.
template <class T>
void aliasing() {
    std::mt19937_64 rng{0xa11a5u};
    for (const std::size_t n : {std::size_t{1},
                                std::size_t{2},
                                std::size_t{3},
                                std::size_t{9},
                                std::size_t{20},
                                std::size_t{80},
                                std::size_t{300}}) {
        for (const bool neg : {false, true}) {
            const limbs_t am  = random_mag(n, rng);
            const limbs_t bm  = random_mag(n / 2 + 1, rng);
            const limbs_t sqm = ref_mul(am, am);
            const limbs_t pm  = ref_mul(am, bm);

            T       a = make<T>(am, neg);
            const T b = make<T>(bm, false);
            a         = a * b;
            EXPECT_TRUE(is_value(a, pm, neg)) << "a = a * b, n=" << n;

            a = make<T>(am, neg);
            a = b * a;
            EXPECT_TRUE(is_value(a, pm, neg)) << "a = b * a, n=" << n;

            a = make<T>(am, neg);
            a *= a;
            EXPECT_TRUE(is_value(a, sqm, false)) << "a *= a, n=" << n;

            a = make<T>(am, neg);
            a = a * a;
            EXPECT_TRUE(is_value(a, sqm, false)) << "a = a * a, n=" << n;

            a = make<T>(am, neg);
            a *= b;
            EXPECT_TRUE(is_value(a, pm, neg)) << "a *= b, n=" << n;

            a = make<T>(am, neg);
            a /= a;
            EXPECT_TRUE(is_value(a, limbs_t{1}, false)) << "a /= a, n=" << n;

            a = make<T>(am, neg);
            a %= a;
            EXPECT_TRUE(is_value(a, limbs_t{}, false)) << "a %= a, n=" << n;

            a            = make<T>(am, neg);
            const T copy = a;
            a /= copy;
            EXPECT_TRUE(is_value(a, limbs_t{1}, false)) << "a /= copy of a, n=" << n;

            a = make<T>(am, neg);
            a %= copy;
            EXPECT_TRUE(is_value(a, limbs_t{}, false)) << "a %= copy of a, n=" << n;

            a = make<T>(am, neg);
            a = a / a;
            EXPECT_TRUE(is_value(a, limbs_t{1}, false)) << "a = a / a, n=" << n;

            a = make<T>(am, neg);
            a = a % a;
            EXPECT_TRUE(is_value(a, limbs_t{}, false)) << "a = a % a, n=" << n;

            // a = a / b and a = a % b through the destination.
            const auto both = div_rem_to_zero(make<T>(am, neg), b);
            a               = make<T>(am, neg);
            a               = a / b;
            EXPECT_EQ(a, both.quotient) << "a = a / b, n=" << n;
            a = make<T>(am, neg);
            a = a % b;
            EXPECT_EQ(a, both.remainder) << "a = a % b, n=" << n;
            a = make<T>(am, neg);
            a /= b;
            EXPECT_EQ(a, both.quotient) << "a /= b, n=" << n;
            a = make<T>(am, neg);
            a %= b;
            EXPECT_EQ(a, both.remainder) << "a %= b, n=" << n;
        }
    }
}

TEST(MulDivFrontend, AliasingBigInt) { aliasing<big_int>(); }
TEST(MulDivFrontend, AliasingInline128) { aliasing<basic_big_int<128>>(); }

// Compound ops against built-in integers, one-limb and two-limb.
TEST(MulDivFrontend, CompoundWithIntegers) {
    std::mt19937_64 rng{0x1417u};
    for (const std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{7}, std::size_t{40}}) {
        const limbs_t am = random_mag(n, rng);
        for (const std::int64_t v :
             {std::int64_t{3}, std::int64_t{-7}, std::int64_t{1} << 40, std::numeric_limits<std::int64_t>::max()}) {
            const limbs_t vm = {
                static_cast<limb_t>(v < 0 ? -static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v))};
            big_int a = make<big_int>(am, false);
            a *= v;
            EXPECT_TRUE(is_value(a, ref_mul(am, vm), v < 0)) << "a *= int, n=" << n;
            a = make<big_int>(am, false);
            a /= v;
            EXPECT_EQ(a, make<big_int>(am, false) / make<big_int>(vm, v < 0));
            a = make<big_int>(am, false);
            a %= v;
            EXPECT_EQ(a, make<big_int>(am, false) % make<big_int>(vm, v < 0));
        }
    }
}

// Constant evaluation of multi-limb products and divisions.
consteval bool constexpr_multi_limb() {
    big_int a = (big_int{1} << 200) + 12345;
    big_int b = (big_int{1} << 70) + 3;

    big_int q = a / b;
    big_int r = a % b;
    if (q * b + r != a || r >= b || r < 0) {
        return false;
    }
    const auto both = div_rem_to_zero(a, b);
    if (both.quotient != q || both.remainder != r) {
        return false;
    }

    big_int c = a;
    c *= b;
    if (c != a * b || c / b != a || c % b != 0) {
        return false;
    }
    c = a;
    c /= b;
    if (c != q) {
        return false;
    }
    c = a;
    c %= b;
    if (c != r) {
        return false;
    }
    c = a;
    c *= c;
    if (c != a * a) {
        return false;
    }
    c = a;
    c /= c;
    if (c != 1) {
        return false;
    }
    c = a;
    c %= c;
    if (c != 0) {
        return false;
    }
    c = -a;
    c *= 3;
    if (c != -(a * 3)) {
        return false;
    }
    c = a * 5;
    c /= 5;
    c %= (big_int{1} << 100);
    return c == a % (big_int{1} << 100);
}

static_assert(constexpr_multi_limb());

consteval bool constexpr_inline_variants() {
    basic_big_int<256> a = (basic_big_int<256>{1} << 300) + 77;
    basic_big_int<256> b = (basic_big_int<256>{1} << 130) + 5;
    auto               c = a;
    c *= b;
    if (c != a * b || c / b != a || c % b != 0) {
        return false;
    }
    c = a;
    c /= b;
    return c == a / b;
}

static_assert(constexpr_inline_variants());

} // namespace muldiv_frontend_test
BEMAN_BIG_INT_END_NAMESPACE
