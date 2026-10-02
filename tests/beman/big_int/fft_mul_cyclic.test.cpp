// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises the cyclic NTT kernel (a * b mod 2^(64w) - 1 at transform length
// L instead of 2L) against the obvious oracle: the full product folded once.
// Covers every coefficient width the chooser can produce (b = 26..50,
// including the sub-32 band that the linear path never uses), the spill fold
// (all-max operands maximize the top coefficients), sub-w operands, and
// semi-canonical edges. Runs against the FP kernel under
// BEMAN_BIG_INT_SIMD_MUL and the two-prime integer kernel otherwise.

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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

constexpr uint_t limb_max = std::numeric_limits<uint_t>::max();

void canonicalize(std::vector<uint_t>& v) {
    if (std::ranges::all_of(v, [](const uint_t x) { return x == limb_max; })) {
        std::ranges::fill(v, uint_t{0});
    }
}

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

void run_kernel(std::span<uint_t>               got,
                const std::vector<uint_t>&      a,
                const std::vector<uint_t>&      b,
                const detail::fft_cyclic_params params) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
    std::vector<double>        fp_ws(detail::fft_cyclic_fp_storage_size(params));
    std::vector<std::uint64_t> int_ws(detail::fft_cyclic_int_storage_size(params));
    detail::multiply_fft_cyclic(got,
                                std::span<const uint_t>{a},
                                std::span<const uint_t>{b},
                                params,
                                std::span<double>{fp_ws},
                                std::span<std::uint64_t>{int_ws});
#else
    std::vector<std::uint64_t> ws(detail::fft_cyclic_storage_size(params));
    detail::multiply_fft_cyclic(
        got, std::span<const uint_t>{a}, std::span<const uint_t>{b}, params, std::span<std::uint64_t>{ws});
#endif
}

void check_cyclic(const std::vector<uint_t>& a, const std::vector<uint_t>& b, const std::size_t min_w) {
    const detail::fft_cyclic_params params = detail::multiply_fft_cyclic_next_size(min_w);
    const std::size_t               w      = params.wrap_limbs;
    ASSERT_LE(a.size(), w);
    ASSERT_LE(b.size(), w);

    std::allocator<uint_t> alloc;
    std::vector<uint_t>    prod(a.size() + b.size(), 0);
    detail::multiply_dispatch(std::span<uint_t>{prod}, std::span<const uint_t>{a}, std::span<const uint_t>{b}, alloc);
    std::vector<uint_t> expected(w, 0);
    detail::fold_mod_bnm1(std::span<uint_t>{expected}, std::span<const uint_t>{prod});
    canonicalize(expected);

    std::vector<uint_t> got(w, 0);
    run_kernel(std::span<uint_t>{got}, a, b, params);
    canonicalize(got);

    EXPECT_TRUE(std::ranges::equal(got, expected)) << "w=" << w << " L=" << params.length << " b=" << params.coeff_bits
                                                   << " an=" << a.size() << " bn=" << b.size();
}

TEST(FftMulCyclic, EveryCoefficientWidth) {
    // min_w = b at L = 64 forces each coefficient width in [26, 50],
    // including the sub-32 band only the cyclic chooser uses.
    std::mt19937_64 rng{0xfc1u};
    for (std::size_t min_w = 26; min_w <= 50; ++min_w) {
        const auto params = detail::multiply_fft_cyclic_next_size(min_w);
        ASSERT_EQ(params.length, 64u);
        ASSERT_EQ(params.coeff_bits, min_w);
        const std::size_t w = params.wrap_limbs;
        check_cyclic(random_limbs(w, rng), random_limbs(w, rng), min_w);
        check_cyclic(std::vector<uint_t>(w, limb_max), std::vector<uint_t>(w, limb_max), min_w);
    }
}

TEST(FftMulCyclic, LargerLengthsAndShapes) {
    std::mt19937_64 rng{0xfc2u};
    for (const std::size_t min_w : {std::size_t{64}, std::size_t{100}, std::size_t{257}, std::size_t{1000}}) {
        const auto        params = detail::multiply_fft_cyclic_next_size(min_w);
        const std::size_t w      = params.wrap_limbs;

        // Full-width random (heavy wraparound).
        check_cyclic(random_limbs(w, rng), random_limbs(w, rng), min_w);
        // All-max operands: maximal coefficients and a maximal spill fold.
        check_cyclic(std::vector<uint_t>(w, limb_max), std::vector<uint_t>(w, limb_max), min_w);
        // Sub-w operands whose product does not wrap at all.
        check_cyclic(random_limbs(w / 2, rng), random_limbs(w / 3 + 1, rng), min_w);
        // One wraps, one tiny.
        check_cyclic(random_limbs(w, rng), random_limbs(1, rng), min_w);
        // Degenerate single-limb operand (zero operands never reach the
        // kernel; the mulmod caller short-circuits them).
        check_cyclic(random_limbs(w, rng), std::vector<uint_t>{1}, min_w);
        // The modulus pattern itself (== 0): the product must be == 0.
        check_cyclic(std::vector<uint_t>(w, limb_max), random_limbs(w, rng), min_w);
    }
}

} // namespace
