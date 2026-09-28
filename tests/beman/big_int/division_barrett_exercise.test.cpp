// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises divide_barrett directly at the span level: results are
// cross-checked against divide_burnikel_ziegler and the q * b + r == a
// identity (with 0 <= r < b). The driver works at any divisor size >= 2, so
// small operands plus reciprocal-threshold overrides cover deep Newton
// recursion and every correction path cheaply.

#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = beman::big_int::detail;
using uint_t     = beman::big_int::uint_multiprecision_t;

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

void check_division(const std::vector<uint_t>& dividend,
                    const std::vector<uint_t>& divisor,
                    const std::size_t          invert_override) {
    ASSERT_GE(divisor.size(), 2u);
    ASSERT_NE(divisor.back(), 0u);
    ASSERT_NE(dividend.back(), 0u);
    ASSERT_GE(dividend.size(), divisor.size());

    const std::size_t m      = dividend.size();
    const std::size_t s      = divisor.size();
    const auto        a_view = std::span<const uint_t>{dividend};
    const auto        b_view = std::span<const uint_t>{divisor};

    std::allocator<uint_t> alloc;

    std::vector<uint_t> q_mu(m - s + 1, 0);
    std::vector<uint_t> r_mu(m + 1, 0);
    detail::divide_barrett(std::span<uint_t>{q_mu}, std::span<uint_t>{r_mu}, a_view, b_view, alloc, invert_override);

    // 0 <= r < b always.
    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_mu}, b_view), std::strong_ordering::less);

    if (detail::compare_unsigned_spans(a_view, b_view) == std::strong_ordering::less) {
        EXPECT_TRUE(detail::is_span_zero(std::span<const uint_t>{q_mu}));
        EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_mu}, a_view), std::strong_ordering::equal);
        return;
    }

    std::vector<uint_t> q_ref(m - s + 1, 0);
    std::vector<uint_t> r_ref(m + 1, 0);
    detail::divide_burnikel_ziegler(std::span<uint_t>{q_ref}, std::span<uint_t>{r_ref}, a_view, b_view, alloc);

    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{q_mu}, std::span<const uint_t>{q_ref}),
              std::strong_ordering::equal)
        << "quotient mismatch at m=" << m << " s=" << s << " inv_thr=" << invert_override;
    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_mu}, std::span<const uint_t>{r_ref}),
              std::strong_ordering::equal)
        << "remainder mismatch at m=" << m << " s=" << s << " inv_thr=" << invert_override;

    // q * b + r == a through the multiplication stack.
    std::vector<uint_t> product(q_mu.size() + s + 1, 0);
    detail::multiply_dispatch(std::span<uint_t>{product}, std::span<const uint_t>{q_mu}, b_view, alloc);
    const bool carry = detail::add_unsigned_spans(
        std::span<uint_t>{product}, std::span<const uint_t>{product}, std::span<const uint_t>{r_mu});
    EXPECT_FALSE(carry);
    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{product}, a_view), std::strong_ordering::equal)
        << "identity mismatch at m=" << m << " s=" << s << " inv_thr=" << invert_override;
}

constexpr std::size_t forced_thresholds[] = {2, 3, 5};

TEST(DivisionBarrettExercise, RandomDeepRecursion) {
    std::mt19937_64 rng{0xba99e7u};
    for (const std::size_t thr : forced_thresholds) {
        for (int trial = 0; trial < 150; ++trial) {
            const std::size_t s     = 2 + static_cast<std::uint32_t>(rng()) % 30;
            const std::size_t extra = static_cast<std::uint32_t>(rng()) % 60;
            check_division(random_limbs(s + extra, rng), random_limbs(s, rng), thr);
        }
    }
}

TEST(DivisionBarrettExercise, AllMaxLimbs) {
    for (const std::size_t thr : forced_thresholds) {
        for (const auto& [s, m] : {std::pair<std::size_t, std::size_t>{2, 4}, {3, 9}, {5, 20}, {8, 16}, {13, 40}}) {
            check_division(std::vector<uint_t>(m, limb_max), std::vector<uint_t>(s, limb_max), thr);
        }
    }
}

TEST(DivisionBarrettExercise, MinimalNormalizedDivisor) {
    std::mt19937_64 rng{0x5e7b18u};
    for (const std::size_t thr : forced_thresholds) {
        for (const std::size_t s : {std::size_t{2}, std::size_t{5}, std::size_t{12}}) {
            std::vector<uint_t> divisor(s, 0);
            divisor.back() = uint_t{1} << (limb_bits - 1);
            check_division(random_limbs(3 * s, rng), divisor, thr);
        }
    }
}

TEST(DivisionBarrettExercise, LowHalfZeroDivisor) {
    std::mt19937_64 rng{0xb2b2b3u};
    for (const std::size_t thr : forced_thresholds) {
        for (const std::size_t s : {std::size_t{4}, std::size_t{8}, std::size_t{16}}) {
            std::vector<uint_t> divisor(s, 0);
            const auto          top = random_limbs(s / 2, rng);
            std::copy(top.begin(), top.end(), divisor.begin() + static_cast<std::ptrdiff_t>(s - s / 2));
            check_division(random_limbs(2 * s + 3, rng), divisor, thr);
        }
    }
}

TEST(DivisionBarrettExercise, MaximalRemainder) {
    std::mt19937_64 rng{0xfeee1u};
    for (const std::size_t thr : forced_thresholds) {
        for (int trial = 0; trial < 20; ++trial) {
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

TEST(DivisionBarrettExercise, ExactMultiple) {
    std::mt19937_64 rng{0xac3eu};
    for (const std::size_t thr : forced_thresholds) {
        for (int trial = 0; trial < 20; ++trial) {
            const std::size_t s = 2 + static_cast<std::uint32_t>(rng()) % 12;
            const auto        b = random_limbs(s, rng);
            const auto        q = random_limbs(1 + static_cast<std::uint32_t>(rng()) % 20, rng);
            check_division(build_dividend(q, b, std::vector<uint_t>{0}), b, thr);
        }
    }
}

TEST(DivisionBarrettExercise, TopWindowEqualsDivisorPrefix) {
    std::mt19937_64 rng{0xa11e6u};
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

TEST(DivisionBarrettExercise, LongZeroRunsInDividend) {
    std::mt19937_64 rng{0x00010u};
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

TEST(DivisionBarrettExercise, DispatchGateBoundaries) {
    // Shapes straddling the barrett_march gate route through divide_dispatch;
    // both sides of the boundary must agree with the direct
    // divide_burnikel_ziegler reference.
    constexpr std::size_t m8 = detail::barrett_march8_cutoff;
    std::mt19937_64       rng{0x9a7e5u};
    for (const auto& [s, m] : {std::pair<std::size_t, std::size_t>{512, 16 * 512}, // at the march gate
                               {512, 16 * 512 - 1},                                // one below
                               {520, 16 * 520 + 7},                                // above, unaligned
                               {511, 16 * 511},                                    // divisor below cutoff
                               {m8, 8 * m8},                                       // at the march8 gate
                               {m8, 8 * m8 - 1},                                   // one below the m/8 line
                               {m8 - 1, 8 * (m8 - 1)}}) {                          // divisor below march8
        const auto dividend = random_limbs(m, rng);
        const auto divisor  = random_limbs(s, rng);
        const auto a_view   = std::span<const uint_t>{dividend};
        const auto b_view   = std::span<const uint_t>{divisor};

        std::allocator<uint_t> alloc;
        std::vector<uint_t>    q_disp(m - s + 1, 0);
        std::vector<uint_t>    r_disp(m + 1, 0);
        {
            detail::scratch_allocator<std::allocator<uint_t>> scratch(detail::divide_unsigned_storage_size(m, s),
                                                                      alloc);
            detail::divide_dispatch(
                std::span<uint_t>{q_disp}, std::span<uint_t>{r_disp}, a_view, b_view, scratch, alloc);
        }

        std::vector<uint_t> q_ref(m - s + 1, 0);
        std::vector<uint_t> r_ref(m + 1, 0);
        detail::divide_burnikel_ziegler(std::span<uint_t>{q_ref}, std::span<uint_t>{r_ref}, a_view, b_view, alloc);

        EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{q_disp}, std::span<const uint_t>{q_ref}),
                  std::strong_ordering::equal)
            << "m=" << m << " s=" << s;
        EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{r_disp}, std::span<const uint_t>{r_ref}),
                  std::strong_ordering::equal)
            << "m=" << m << " s=" << s;
    }
}

TEST(DivisionBarrettExercise, CyclicMulmodTier) {
    if constexpr (detail::width_v<uint_t> != 64) {
        GTEST_SKIP() << "the cyclic mulmod tier is gated to 64-bit limbs";
    }
    // Divisor sized so the block wrap w = multiply_mod_bnm1_next_size(s + 1)
    // comes from the cyclic NTT chooser (s + 1 >= fft_cyclic_cutoff). The
    // chooser pads w past s + 1, exercising the residual fold and undershoot
    // bounds under a padded wrap on both the reciprocal and the block march.
    std::mt19937_64   rng{0xba99eau};
    const std::size_t s = detail::fft_cyclic_cutoff + 40;
    check_division(random_limbs(2 * s + 173, rng), random_limbs(s, rng), 0);
    check_division(std::vector<uint_t>(3 * s + 7, limb_max), std::vector<uint_t>(s, limb_max), 0);
}

TEST(DivisionBarrettExercise, DefaultReciprocalThreshold) {
    // Default Newton threshold, sizes that take one or two real levels, plus
    // long block marches and a single-block tail.
    std::mt19937_64 rng{0xdefa18u};
    for (const auto& [s, m] :
         {std::pair<std::size_t, std::size_t>{100, 400}, {128, 1000}, {200, 280}, {256, 520}, {300, 301}}) {
        check_division(random_limbs(m, rng), random_limbs(s, rng), 0);
    }
}

} // namespace
