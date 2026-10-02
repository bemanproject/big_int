// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises multiply_mod_bnm1 against the obvious oracle (full product, then
// one fold mod B^w - 1), across odd/even/rounded wrap sizes, operand shapes,
// and deep forced recursion; also asserts the scratch high-water mark stays
// within multiply_mod_bnm1_storage_size.

#define BEMAN_BIG_INT_INSTRUMENT

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

using scratch_for_test = detail::scratch_allocator<std::allocator<uint_t>>;

// Map the semi-canonical all-ones representation (== the modulus) to zero so
// results compare as values mod B^w - 1.
void canonicalize(std::vector<uint_t>& v) {
    if (std::ranges::all_of(v, [](const uint_t x) { return x == limb_max; })) {
        std::ranges::fill(v, uint_t{0});
    }
}

void check_mulmod(const std::vector<uint_t>& a,
                  const std::vector<uint_t>& b,
                  const std::size_t          w,
                  const std::size_t          cutoff_override) {
    std::allocator<uint_t> alloc;

    // Oracle: full product folded once.
    std::vector<uint_t> prod(a.size() + b.size(), 0);
    detail::multiply_dispatch(std::span<uint_t>{prod}, std::span<const uint_t>{a}, std::span<const uint_t>{b}, alloc);
    std::vector<uint_t> expected(w, 0);
    detail::fold_mod_bnm1(std::span<uint_t>{expected}, std::span<const uint_t>{prod});
    canonicalize(expected);

    std::vector<uint_t> got(w, 0);
    const std::size_t   budget = detail::multiply_mod_bnm1_storage_size(w);
    scratch_for_test    scratch(3 * budget + 64, alloc);
    detail::multiply_mod_bnm1(
        std::span<uint_t>{got}, std::span<const uint_t>{a}, std::span<const uint_t>{b}, scratch, cutoff_override);
    EXPECT_LE(scratch.peak(), budget) << "w=" << w << " thr=" << cutoff_override;
    canonicalize(got);

    EXPECT_TRUE(std::ranges::equal(got, expected))
        << "w=" << w << " an=" << a.size() << " bn=" << b.size() << " thr=" << cutoff_override;
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

TEST(MulmodBnm1, SweepSmallWrapSizes) {
    std::mt19937_64 rng{0xb31u};
    for (std::size_t w = 1; w <= 40; ++w) {
        for (const std::size_t thr : {std::size_t{0}, std::size_t{2}, std::size_t{4}}) {
            for (int trial = 0; trial < 6; ++trial) {
                const std::size_t an = 1 + static_cast<std::uint32_t>(rng()) % w;
                const std::size_t bn = 1 + static_cast<std::uint32_t>(rng()) % w;
                check_mulmod(random_limbs(an, rng), random_limbs(bn, rng), w, thr);
            }
            // Full-width operands maximize the wraparound.
            check_mulmod(random_limbs(w, rng), random_limbs(w, rng), w, thr);
            check_mulmod(std::vector<uint_t>(w, limb_max), std::vector<uint_t>(w, limb_max), w, thr);
        }
    }
}

TEST(MulmodBnm1, LargerAndRoundedSizes) {
    std::mt19937_64 rng{0xb32u};
    for (const std::size_t n : {std::size_t{41},
                                std::size_t{48},
                                std::size_t{64},
                                std::size_t{100},
                                std::size_t{129},
                                std::size_t{256},
                                std::size_t{1000}}) {
        const std::size_t w = detail::multiply_mod_bnm1_next_size(n, detail::multiply_mod_bnm1_cutoff);
        ASSERT_GE(w, n);
        for (int trial = 0; trial < 3; ++trial) {
            check_mulmod(random_limbs(w, rng), random_limbs(w, rng), w, 0);
            check_mulmod(random_limbs(n, rng), random_limbs(1 + static_cast<std::uint32_t>(rng()) % n, rng), w, 0);
        }
        check_mulmod(std::vector<uint_t>(w, limb_max), std::vector<uint_t>(w, limb_max), w, 0);
    }
}

TEST(MulmodBnm1, DegenerateOperands) {
    std::mt19937_64 rng{0xb33u};
    for (const std::size_t w : {std::size_t{6}, std::size_t{16}, std::size_t{32}}) {
        const auto a = random_limbs(w, rng);
        // Zero operand.
        check_mulmod(a, std::vector<uint_t>{0}, w, 2);
        // One.
        check_mulmod(a, std::vector<uint_t>{1}, w, 2);
        // Operand congruent to zero mod B^(w/2) - 1 (all-ones half), driving
        // the recursive half to its semi-canonical edge.
        check_mulmod(a, std::vector<uint_t>(w / 2, limb_max), w, 2);
        // No-wrap case: an + bn <= w.
        check_mulmod(random_limbs(w / 2, rng), random_limbs(w / 2, rng), w, 2);
    }
}

TEST(MulmodBnm1, NextSizeProperties) {
    constexpr std::size_t thr = detail::multiply_mod_bnm1_cutoff;
    for (std::size_t n = 1; n <= detail::fft_cyclic_cutoff + 4000; n = n < 64 ? n + 1 : n + 37) {
        const std::size_t w = detail::multiply_mod_bnm1_next_size(n, thr);
        EXPECT_GE(w, n);
        if (detail::width_v<uint_t> == 64 && n >= detail::fft_cyclic_cutoff) {
            // Cyclic-tier sizes come from the NTT chooser: idempotent, with
            // padding below min_w / 25 (b >= 26 needs 64 * min_w / b <= L
            // coefficients; the bound is the chooser's static_assert).
            EXPECT_EQ(detail::multiply_fft_cyclic_next_size(w).wrap_limbs, w) << "n=" << n;
            EXPECT_LT(w, n + n / 25 + 1) << "n=" << n;
            continue;
        }
        // The recursion must reach the threshold by halving even sizes only.
        std::size_t x = w;
        while (x > thr) {
            EXPECT_EQ(x % 2, 0u) << "n=" << n << " w=" << w;
            x /= 2;
        }
        // And the padding stays modest: the chunk is the smallest power of
        // two with ceil(n/chunk) <= thr, so chunk < 2n/(thr-1) and the
        // round-up adds less than that.
        EXPECT_LT(w, n + 2 * n / (thr - 1) + 2);
    }
}

TEST(MulmodBnm1, CyclicTierDifferential) {
    if constexpr (detail::width_v<uint_t> != 64) {
        GTEST_SKIP() << "the cyclic tier is gated to 64-bit limbs";
    }
    std::mt19937_64 rng{0xb34u};

    // A chooser size at the production cutoff: the cyclic kernel runs.
    const std::size_t w =
        detail::multiply_mod_bnm1_next_size(detail::fft_cyclic_cutoff + 17, detail::multiply_mod_bnm1_cutoff);
    check_mulmod(random_limbs(w, rng), random_limbs(w, rng), w, 0);
    check_mulmod(std::vector<uint_t>(w, limb_max), std::vector<uint_t>(w, limb_max), w, 0);
    // Zero operand exercises the tier's short-circuit (the kernel itself
    // requires nonzero operands).
    check_mulmod(std::vector<uint_t>{0}, random_limbs(w, rng), w, 0);

    // An even non-chooser size above the cutoff falls back to the CRT split.
    const std::size_t w_split = w + 2;
    ASSERT_NE(detail::multiply_fft_cyclic_next_size(w_split).wrap_limbs, w_split);
    check_mulmod(random_limbs(w_split, rng), random_limbs(w_split, rng), w_split, 0);
}

} // namespace
