// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises divide_burnikel_ziegler directly at the span level: results are
// cross-checked against the schoolbook divide_unsigned and against the
// q * b + r == a identity (with 0 <= r < b). Small threshold overrides force
// deep recursion on small operands so every branch of the recursion (odd
// leaves, the single-limb leaf, the all-ones estimate, both correction
// iterations) is hit cheaply.

#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

constexpr uint_t      limb_max  = std::numeric_limits<uint_t>::max();
constexpr std::size_t limb_bits = detail::width_v<uint_t>;

std::vector<uint_t> random_limbs(const std::size_t size, std::mt19937_64& rng) {
    std::vector<uint_t> v(size);
    for (auto& limb : v) {
        limb = static_cast<uint_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// trimmed_q * b + r for building dividends with known quotient/remainder.
std::vector<uint_t>
build_dividend(const std::vector<uint_t>& q, const std::vector<uint_t>& b, const std::vector<uint_t>& r) {
    std::allocator<uint_t> alloc;
    std::vector<uint_t>    product(q.size() + b.size() + 1, 0);
    detail::multiply_dispatch(
        std::span<uint_t>{product}, std::span<const uint_t>{q}, std::span<const uint_t>{b}, alloc);
    const bool carry = detail::add_unsigned_spans(
        std::span<uint_t>{product}, std::span<const uint_t>{product}, std::span<const uint_t>{r});
    EXPECT_FALSE(carry);
    product.resize(detail::trimmed_size_span(std::span<const uint_t>{product}));
    return product;
}

// Runs divide_burnikel_ziegler on (dividend, divisor), checks against the
// schoolbook reference and the multiplication identity.
void check_division(const std::vector<uint_t>& dividend,
                    const std::vector<uint_t>& divisor,
                    const std::size_t          threshold_override) {
    ASSERT_GE(divisor.size(), 2u);
    ASSERT_NE(divisor.back(), 0u);
    ASSERT_NE(dividend.back(), 0u);
    ASSERT_GE(dividend.size(), divisor.size());

    const std::size_t m      = dividend.size();
    const std::size_t s      = divisor.size();
    const auto        a_view = std::span<const uint_t>{dividend};
    const auto        b_view = std::span<const uint_t>{divisor};

    std::allocator<uint_t> alloc;

    std::vector<uint_t> q_bz(m - s + 1, 0);
    std::vector<uint_t> r_bz(m + 1, 0);
    detail::divide_burnikel_ziegler(
        std::span<uint_t>{q_bz}, std::span<uint_t>{r_bz}, a_view, b_view, alloc, threshold_override);

    // 0 <= r < b always.
    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_bz}, b_view), std::strong_ordering::less);

    if (detail::compare_unsigned_spans(a_view, b_view) == std::strong_ordering::less) {
        // dividend < divisor: q == 0, r == dividend. divide_unsigned's
        // contract excludes this shape, so check directly.
        EXPECT_TRUE(detail::is_span_zero(std::span<const uint_t>{q_bz}));
        EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_bz}, a_view), std::strong_ordering::equal);
        return;
    }

    std::vector<uint_t> q_ref(m - s + 1, 0);
    std::vector<uint_t> r_ref(m + 1, 0);
    {
        detail::scratch_allocator<std::allocator<uint_t>> scratch(detail::divide_unsigned_storage_size(m, s), alloc);
        detail::divide_unsigned(std::span<uint_t>{q_ref}, std::span<uint_t>{r_ref}, a_view, b_view, scratch);
    }

    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{q_bz}, std::span<const uint_t>{q_ref}),
              std::strong_ordering::equal)
        << "quotient mismatch at m=" << m << " s=" << s << " thr=" << threshold_override;
    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_bz}, std::span<const uint_t>{r_ref}),
              std::strong_ordering::equal)
        << "remainder mismatch at m=" << m << " s=" << s << " thr=" << threshold_override;

    // q * b + r == a through the well-tested multiplication stack.
    std::vector<uint_t> product(q_bz.size() + s + 1, 0);
    detail::multiply_dispatch(std::span<uint_t>{product}, std::span<const uint_t>{q_bz}, b_view, alloc);
    const bool carry = detail::add_unsigned_spans(
        std::span<uint_t>{product}, std::span<const uint_t>{product}, std::span<const uint_t>{r_bz});
    EXPECT_FALSE(carry);
    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{product}, a_view), std::strong_ordering::equal)
        << "identity mismatch at m=" << m << " s=" << s << " thr=" << threshold_override;
}

constexpr std::size_t forced_thresholds[] = {2, 3, 4, 5};

TEST(DivisionDcExercise, RandomDeepRecursion) {
    std::mt19937_64 rng(0xb16d1u);
    for (const std::size_t thr : forced_thresholds) {
        for (int trial = 0; trial < 200; ++trial) {
            const std::size_t s     = 2 + static_cast<std::uint32_t>(rng()) % 30;
            const std::size_t extra = static_cast<std::uint32_t>(rng()) % 40;
            check_division(random_limbs(s + extra, rng), random_limbs(s, rng), thr);
        }
    }
}

TEST(DivisionDcExercise, AllMaxLimbs) {
    for (const std::size_t thr : forced_thresholds) {
        for (const auto& [s, m] : {std::pair<std::size_t, std::size_t>{2, 4}, {3, 9}, {5, 20}, {8, 16}, {13, 40}}) {
            check_division(std::vector<uint_t>(m, limb_max), std::vector<uint_t>(s, limb_max), thr);
        }
    }
}

TEST(DivisionDcExercise, MinimalNormalizedDivisor) {
    // b = beta^s / 2: already normalized (sigma's bit part is zero).
    std::mt19937_64 rng(0x5e7b17u);
    for (const std::size_t thr : forced_thresholds) {
        for (const std::size_t s : {std::size_t{2}, std::size_t{5}, std::size_t{12}}) {
            std::vector<uint_t> divisor(s, 0);
            divisor.back() = uint_t{1} << (limb_bits - 1);
            check_division(random_limbs(3 * s, rng), divisor, thr);
        }
    }
}

TEST(DivisionDcExercise, LowHalfZeroDivisor) {
    // Zero low limbs keep b2 == 0 through the first recursion levels.
    std::mt19937_64 rng(0xb2b2b2u);
    for (const std::size_t thr : forced_thresholds) {
        for (const std::size_t s : {std::size_t{4}, std::size_t{8}, std::size_t{16}}) {
            std::vector<uint_t> divisor(s, 0);
            const auto          top = random_limbs(s / 2, rng);
            std::copy(top.begin(), top.end(), divisor.begin() + static_cast<std::ptrdiff_t>(s - s / 2));
            check_division(random_limbs(2 * s + 3, rng), divisor, thr);
        }
    }
}

TEST(DivisionDcExercise, MaximalRemainder) {
    // a = q * b + (b - 1): every quotient digit estimate runs hot.
    std::mt19937_64 rng(0xfeedu);
    for (const std::size_t thr : forced_thresholds) {
        for (int trial = 0; trial < 25; ++trial) {
            const std::size_t   s         = 2 + static_cast<std::uint32_t>(rng()) % 12;
            const auto          b         = random_limbs(s, rng);
            const auto          q         = random_limbs(1 + static_cast<std::uint32_t>(rng()) % 20, rng);
            std::vector<uint_t> r         = b;
            const bool          underflow = detail::decrement_span(std::span<uint_t>{r});
            EXPECT_FALSE(underflow);
            check_division(build_dividend(q, b, r), b, thr);
        }
    }
}

TEST(DivisionDcExercise, ExactMultiple) {
    std::mt19937_64 rng(0xac3du);
    for (const std::size_t thr : forced_thresholds) {
        for (int trial = 0; trial < 25; ++trial) {
            const std::size_t s = 2 + static_cast<std::uint32_t>(rng()) % 12;
            const auto        b = random_limbs(s, rng);
            const auto        q = random_limbs(1 + static_cast<std::uint32_t>(rng()) % 20, rng);
            check_division(build_dividend(q, b, std::vector<uint_t>{0}), b, thr);
        }
    }
}

TEST(DivisionDcExercise, TopWindowEqualsDivisorPrefix) {
    // Dividend whose top limbs replicate the divisor, steering windows into
    // the equal-compare (all-ones estimate) branch.
    std::mt19937_64 rng(0xa11e5u);
    for (const std::size_t thr : forced_thresholds) {
        for (const std::size_t s : {std::size_t{2}, std::size_t{4}, std::size_t{6}, std::size_t{12}}) {
            const auto          divisor = random_limbs(s, rng);
            std::vector<uint_t> dividend(3 * s);
            const auto          low = random_limbs(2 * s, rng);
            std::copy(low.begin(), low.end(), dividend.begin());
            std::copy(divisor.begin(), divisor.end(), dividend.begin() + static_cast<std::ptrdiff_t>(2 * s));
            check_division(dividend, divisor, thr);
        }
    }
}

TEST(DivisionDcExercise, LongZeroRunsInDividend) {
    // Interior zero blocks produce untrimmed (mostly zero) leaf windows.
    std::mt19937_64 rng(0x0000fu);
    for (const std::size_t thr : forced_thresholds) {
        for (const std::size_t s : {std::size_t{3}, std::size_t{7}}) {
            const auto          divisor = random_limbs(s, rng);
            std::vector<uint_t> dividend(8 * s, 0);
            dividend.front() = static_cast<uint_t>(rng()) | 1u;
            dividend.back()  = static_cast<uint_t>(rng()) | 1u;
            check_division(dividend, divisor, thr);
        }
    }
}

TEST(DivisionDcExercise, DividendSmallerThanDivisor) {
    std::mt19937_64 rng(0x51a11u);
    for (const std::size_t thr : forced_thresholds) {
        // Same size, value below the divisor.
        std::vector<uint_t> divisor(6, limb_max);
        auto                dividend = random_limbs(6, rng);
        dividend.back() &= limb_max >> 1;
        if (dividend.back() == 0) {
            dividend.back() = 1;
        }
        divisor.back() = limb_max;
        check_division(dividend, divisor, thr);
    }
}

TEST(DivisionDcExercise, GoIssue42552Shape) {
    // golang/go#42552 (CVE-2020-28362) hit recursive division with divisors
    // around 3168 bits and dividends about twice that.
    std::mt19937_64   rng(0x60601u);
    const std::size_t s = (3168 / limb_bits) + 1;
    check_division(random_limbs(2 * s, rng), random_limbs(s, rng), 4);
    check_division(std::vector<uint_t>(2 * s, limb_max), std::vector<uint_t>(s, limb_max), 4);
    // Same shapes through the production threshold.
    check_division(random_limbs(2 * s, rng), random_limbs(s, rng), 0);
}

TEST(DivisionDcExercise, DefaultThreshold) {
    // Production cutoff, sizes straddling it, including a single-block tail
    // (t == 2) and a long block march.
    std::mt19937_64 rng(0xdefa17u);
    for (const auto& [s, m] :
         {std::pair<std::size_t, std::size_t>{48, 120}, {41, 62}, {64, 85}, {96, 400}, {100, 101}, {128, 1000}}) {
        check_division(random_limbs(m, rng), random_limbs(s, rng), 0);
    }
}

} // namespace
