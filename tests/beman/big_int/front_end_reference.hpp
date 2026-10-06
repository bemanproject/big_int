// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Shared reference model for the front_end_add_sub and front_end_shift tests: a sign plus trimmed
// limb-vector magnitude with naive add/sub/shift, and helpers to build operands and compare results.

#ifndef BEMAN_BIG_INT_TESTS_FRONT_END_REFERENCE_HPP
#define BEMAN_BIG_INT_TESTS_FRONT_END_REFERENCE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace front_end_test {


using limb                = uint_multiprecision_t;
inline constexpr unsigned W = static_cast<unsigned>(std::numeric_limits<limb>::digits);

// Sign and trimmed little-endian magnitude (empty for zero).
struct ref {
    bool              neg = false;
    std::vector<limb> mag;
};

inline void trim(std::vector<limb>& v) {
    while (!v.empty() && v.back() == 0) {
        v.pop_back();
    }
}

inline int cmp_mag(const std::vector<limb>& a, const std::vector<limb>& b) {
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

inline std::vector<limb> add_mag(const std::vector<limb>& a, const std::vector<limb>& b) {
    std::vector<limb> r;
    limb              carry = 0;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const limb x = i < a.size() ? a[i] : 0;
        const limb y = i < b.size() ? b[i] : 0;
        const limb s = static_cast<limb>(x + y);
        const limb t = static_cast<limb>(s + carry);
        carry        = static_cast<limb>((s < x ? 1 : 0) + (t < s ? 1 : 0));
        r.push_back(t);
    }
    if (carry != 0) {
        r.push_back(carry);
    }
    trim(r);
    return r;
}

// Requires a >= b.
inline std::vector<limb> sub_mag(const std::vector<limb>& a, const std::vector<limb>& b) {
    std::vector<limb> r;
    limb              borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const limb y = i < b.size() ? b[i] : 0;
        const limb d = static_cast<limb>(a[i] - y);
        const limb e = static_cast<limb>(d - borrow);
        borrow       = static_cast<limb>((a[i] < y ? 1 : 0) + (d < borrow ? 1 : 0));
        r.push_back(e);
    }
    trim(r);
    return r;
}

inline ref ref_add(const ref& a, const ref& b) {
    if (a.neg == b.neg) {
        ref r{a.neg, add_mag(a.mag, b.mag)};
        r.neg = r.neg && !r.mag.empty();
        return r;
    }
    const int c = cmp_mag(a.mag, b.mag);
    if (c == 0) {
        return {};
    }
    return c > 0 ? ref{a.neg, sub_mag(a.mag, b.mag)} : ref{b.neg, sub_mag(b.mag, a.mag)};
}

inline ref ref_neg(ref a) {
    a.neg = !a.neg && !a.mag.empty();
    return a;
}

inline bool bit_at(const std::vector<limb>& v, std::size_t i) {
    const std::size_t l = i / W;
    return l < v.size() && ((v[l] >> (i % W)) & 1U) != 0;
}

inline ref ref_shl(const ref& a, std::size_t s) {
    if (a.mag.empty()) {
        return {};
    }
    ref r;
    r.neg = a.neg;
    r.mag.assign(a.mag.size() + s / W + 2, 0);
    for (std::size_t i = 0; i < a.mag.size() * W; ++i) {
        if (bit_at(a.mag, i)) {
            const std::size_t j = i + s;
            r.mag[j / W] |= static_cast<limb>(limb{1} << (j % W));
        }
    }
    trim(r.mag);
    return r;
}

// Floor division by 2^s.
inline ref ref_shr(const ref& a, std::size_t s) {
    ref r;
    r.neg = a.neg;
    r.mag.assign(a.mag.size(), 0);
    bool inexact = false;
    for (std::size_t i = 0; i < a.mag.size() * W; ++i) {
        if (!bit_at(a.mag, i)) {
            continue;
        }
        if (i < s) {
            inexact = true;
        } else {
            const std::size_t j = i - s;
            r.mag[j / W] |= static_cast<limb>(limb{1} << (j % W));
        }
    }
    trim(r.mag);
    if (a.neg && inexact) {
        r.mag = add_mag(r.mag, {1});
    }
    r.neg = a.neg && !r.mag.empty();
    return r;
}

inline std::string hex(const ref& r) {
    if (r.mag.empty()) {
        return "0";
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string           s;
    for (std::size_t i = r.mag.size(); i-- > 0;) {
        for (unsigned d = W / 4; d-- > 0;) {
            const char c = digits[(r.mag[i] >> (4 * d)) & 0xFU];
            if (!s.empty() || c != '0') {
                s.push_back(c);
            }
        }
    }
    return (r.neg ? "-" : "") + s;
}

template <class T>
T make(const ref& r) {
    T           x;
    const auto  s   = hex(r);
    const auto  res = from_chars(s.data(), s.data() + s.size(), x, 16);
    EXPECT_TRUE(res.ec == std::errc{});
    return x;
}

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

template <class T>
::testing::AssertionResult matches(const T& x, const ref& expect) {
    const auto rep = x.representation();
    std::vector<limb> got(rep.begin(), rep.end());
    trim(got);
    const bool neg = x < 0;
    if (got != expect.mag || neg != expect.neg) {
        return ::testing::AssertionFailure() << "got " << (neg ? "-" : "") << "[" << got.size() << " limbs] expected "
                                             << hex(expect);
    }
    if (!is_normalized(x)) {
        return ::testing::AssertionFailure() << "not normalized, expected " << hex(expect);
    }
    if (!tail_is_zero(x)) {
        return ::testing::AssertionFailure() << "inline tail not zero, expected " << hex(expect);
    }
    return ::testing::AssertionSuccess();
}

// Operand generator: a magnitude of `n` limbs (0 means zero) in one of several carry-stressing shapes.
inline ref gen(std::mt19937_64& rng, std::size_t n, unsigned pattern, bool neg) {
    ref r;
    if (n == 0) {
        return r;
    }
    r.mag.assign(n, 0);
    const limb ones = std::numeric_limits<limb>::max();
    switch (pattern % 6) {
    case 0: // random
        for (auto& l : r.mag) {
            l = static_cast<limb>(rng());
        }
        break;
    case 1: // all ones
        std::fill(r.mag.begin(), r.mag.end(), ones);
        break;
    case 2: // 2^(W(n-1)): zeros below a single top bit
        r.mag.back() = 1;
        break;
    case 3: // top limb 1, ones below
        std::fill(r.mag.begin(), r.mag.end(), ones);
        r.mag.back() = 1;
        break;
    case 4: // random with zero low limbs
        for (std::size_t i = n / 2; i < n; ++i) {
            r.mag[i] = static_cast<limb>(rng());
        }
        break;
    default: // single high bit set
        r.mag.back() = static_cast<limb>(limb{1} << (W - 1));
        break;
    }
    if (r.mag.back() == 0) {
        r.mag.back() = 1;
    }
    r.neg = neg;
    return r;
}

inline std::vector<std::size_t> sizes_for(std::size_t cap) {
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i <= 20; ++i) {
        v.push_back(i);
    }
    for (const std::size_t extra : {cap - 1, cap, cap + 1, cap + 2}) {
        if (extra > 20) {
            v.push_back(extra);
        }
    }
    return v;
}

template <class T>
constexpr T cx_ones(unsigned k) {
    char digits[2048] = {};
    const unsigned n  = k * W / 4;
    for (unsigned i = 0; i < n; ++i) {
        digits[i] = 'f';
    }
    T x;
    [[maybe_unused]] const auto r = from_chars(digits, digits + n, x, 16);
    return x;
}

template <class T>
constexpr bool cx_pow2(const T& x, unsigned e) { // x == 2^e
    T p{1};
    p <<= e;
    return x == p;
}


} // namespace front_end_test
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_TESTS_FRONT_END_REFERENCE_HPP
