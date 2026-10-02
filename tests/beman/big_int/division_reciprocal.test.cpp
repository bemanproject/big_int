// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises reciprocal_span: the result must be the EXACT scaled reciprocal,
// i.e. X = B^n + I satisfies d * X <= B^{2n} - 1 < d * (X + 1), verified by
// reconstruction and cross-checked against the quotient of the all-ones
// dividend computed by divide_burnikel_ziegler. Small threshold overrides
// force deep Newton recursion.

#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
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

void check_reciprocal(const std::vector<uint_t>& divisor, const std::size_t threshold_override) {
    const std::size_t n = divisor.size();
    ASSERT_GE(n, 2u);
    ASSERT_NE(divisor.back() >> (limb_bits - 1), 0u);

    const auto             d_view = std::span<const uint_t>{divisor};
    std::allocator<uint_t> alloc;

    std::vector<uint_t> inverse(n);
    {
        const std::size_t thr = threshold_override != 0 ? threshold_override : detail::reciprocal_span_cutoff;
        detail::scratch_allocator<std::allocator<uint_t>> scratch(detail::reciprocal_span_storage_size(n, thr), alloc);
        detail::reciprocal_span(std::span<uint_t>{inverse}, d_view, scratch, threshold_override);
    }

    // Reconstruction: v = d * (B^n + I) must keep its top limb zero
    // (v <= B^{2n} - 1), and adding one more d must carry out (v + d >= B^{2n}).
    std::vector<uint_t> v(2 * n + 1, 0);
    detail::multiply_dispatch(std::span<uint_t>{v.data(), 2 * n}, d_view, std::span<const uint_t>{inverse}, alloc);
    detail::add_shifted(std::span<uint_t>{v}, n, d_view);
    EXPECT_EQ(v[2 * n], 0u) << "n=" << n << " thr=" << threshold_override;

    const bool carry = detail::add_unsigned_spans(
        std::span<uint_t>{v.data(), 2 * n}, std::span<const uint_t>{v.data(), 2 * n}, d_view);
    EXPECT_TRUE(carry) << "n=" << n << " thr=" << threshold_override;

    // Cross-check against the all-ones quotient from the divide-and-conquer
    // path: floor((B^{2n} - 1) / d) = B^n + I.
    std::vector<uint_t> ones(2 * n, limb_max);
    std::vector<uint_t> q(n + 1, 0);
    std::vector<uint_t> r(2 * n + 1, 0);
    detail::divide_burnikel_ziegler(
        std::span<uint_t>{q}, std::span<uint_t>{r}, std::span<const uint_t>{ones}, d_view, alloc);
    EXPECT_EQ(q[n], 1u);
    EXPECT_TRUE(std::ranges::equal(std::span<const uint_t>{q.data(), n}, std::span<const uint_t>{inverse}))
        << "n=" << n << " thr=" << threshold_override;
}

std::vector<uint_t> random_normalized(const std::size_t n, std::mt19937_64& rng) {
    std::vector<uint_t> v(n);
    for (auto& limb : v) {
        limb = static_cast<uint_t>(rng());
    }
    v.back() |= uint_t{1} << (limb_bits - 1);
    return v;
}

constexpr std::size_t sizes[]      = {2, 3, 4, 5, 7, 8, 9, 16, 17, 33, 64, 65, 100, 150};
constexpr std::size_t thresholds[] = {0, 2, 3, 5};

TEST(DivisionReciprocal, RandomNormalizedDivisors) {
    std::mt19937_64 rng{0x1ecdu};
    for (const std::size_t thr : thresholds) {
        for (const std::size_t n : sizes) {
            for (int trial = 0; trial < 5; ++trial) {
                check_reciprocal(random_normalized(n, rng), thr);
            }
        }
    }
}

TEST(DivisionReciprocal, AdversarialDivisors) {
    std::mt19937_64 rng{0xadbe1u};
    for (const std::size_t thr : thresholds) {
        for (const std::size_t n : {std::size_t{2}, std::size_t{5}, std::size_t{16}, std::size_t{65}}) {
            // All-ones divisor: X = B^n + 1.
            check_reciprocal(std::vector<uint_t>(n, limb_max), thr);

            // Minimal normalized divisor B^n / 2: X = 2 * B^n - 1 (I all-ones).
            std::vector<uint_t> minimal(n, 0);
            minimal.back() = uint_t{1} << (limb_bits - 1);
            check_reciprocal(minimal, thr);

            // Minimal plus one.
            std::vector<uint_t> minimal_plus = minimal;
            minimal_plus.front() |= 1u;
            check_reciprocal(minimal_plus, thr);

            // Top half saturated, low half zero (the Newton seed is exact for
            // the prefix, the low correction must still engage).
            std::vector<uint_t> half(n, 0);
            for (std::size_t i = (n + 1) / 2; i < n; ++i) {
                half[i] = limb_max;
            }
            check_reciprocal(half, thr);

            // Random low limb under an otherwise saturated divisor.
            std::vector<uint_t> nearly(n, limb_max);
            nearly.front() = static_cast<uint_t>(rng());
            check_reciprocal(nearly, thr);
        }
    }
}

} // namespace
