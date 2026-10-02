// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "mul_kernel_testing.hpp"
#include "testing.hpp"
#include <beman/big_int/detail/mul_impl.hpp>
#include <gtest/gtest.h>
#include <cstddef>

namespace bmp = ::BEMAN_BIG_INT_NAMESPACE::boost_mp_testing;
namespace kt  = ::BEMAN_BIG_INT_NAMESPACE::kernel_testing;

constexpr std::size_t limb_bits =
    static_cast<std::size_t>(std::numeric_limits<::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);

// Toom-Cook 6.5 routing starts at detail::toom_cook_6_5_cutoff (`cutoff` below). Tests around the boundary verify
// that the dispatcher produces correct results on both sides of it and for varying input sizes. Shapes are derived
// from the constant, so they follow retuning.
constexpr std::size_t cutoff = ::BEMAN_BIG_INT_NAMESPACE::detail::toom_cook_6_5_cutoff;

void check_balanced(const std::size_t limbs_a, const std::size_t limbs_b) {
    const std::string a = bmp::random_big_int(limbs_a * limb_bits);
    const std::string b = bmp::random_big_int(limbs_b * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}

// n scaled by num/den.
constexpr std::size_t scaled(const std::size_t n, const std::size_t num, const std::size_t den) {
    return n * num / den;
}

TEST(Multiplication, ToomCook6_5AtCutoff) {
    // Both operands exactly at the cutoff.
    check_balanced(cutoff, cutoff);
}

TEST(Multiplication, ToomCook6_5JustAboveCutoff) {
    check_balanced(cutoff + 1, cutoff + 1);
    check_balanced(cutoff + cutoff / 16, cutoff + cutoff / 16);
}

TEST(Multiplication, ToomCook6_5JustBelowCutoff) {
    // One limb below the cutoff: the previous tier runs. Sanity check that the gate works as expected.
    check_balanced(cutoff - 1, cutoff - 1);
}

TEST(Multiplication, ToomCook6_5DeepRecursion) {
    // Large enough to drive several recursion levels before the cascade drops to a lower tier.
    check_balanced(4 * cutoff, 4 * cutoff);
}

TEST(Multiplication, ToomCook6_5DeepRecursionKernel) {
    // Force the top level (cutoff_override = 1) at a size whose sub-products clear the tier's own cutoff, so the
    // kernel recurses into itself, and compare with the next lower kernel.
    const auto kernel = [](auto r, auto a, auto b, auto& s, const std::size_t c) {
        ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_6_5(r, a, b, s, c);
    };
    const auto reference = [](auto r, auto a, auto b, auto& s, const std::size_t c) {
        ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_4(r, a, b, s, c);
    };
    kt::expect_kernels_match(kernel, reference, 7 * cutoff + 7, 7 * cutoff + 7);
}

TEST(Multiplication, ToomCook6_5AsymmetricBalanced) {
    // Asymmetric but within the kernel's ratio gate (long/short up to about 1.1:1).
    check_balanced(cutoff + cutoff / 16, scaled(cutoff + cutoff / 16, 11, 10));
    check_balanced(scaled(cutoff, 5, 4), scaled(scaled(cutoff, 5, 4), 11, 10));
    check_balanced(2 * cutoff, scaled(2 * cutoff, 11, 10));
}

TEST(Multiplication, ToomCook6_5AsymmetricFallback) {
    // Beyond the kernel's ratio gate the kernel falls back to a lower tier or the dispatcher slices, depending on the
    // slicing ratio of the zone. The first shape (1.3:1) is below the 3:2 slicing ratio of the Toom zones and runs the
    // kernel's own fallback; the second slices. End-to-end correctness only (the kernel fallback itself is covered by
    // ToomCook6_5KernelRatioFallback).
    check_balanced(cutoff, scaled(cutoff, 13, 10));
    check_balanced(cutoff + cutoff / 8, scaled(cutoff, 27, 10));
}

TEST(Multiplication, ToomCook6_5KernelRatioFallback) {
    // Drive the kernel directly with cutoff_override = 1: shapes inside the ratio gate run Toom, the rest fall
    // back inside the kernel; all must match schoolbook.
    const auto kernel = [](auto r, auto a, auto b, auto& s, const std::size_t c) {
        ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_6_5(r, a, b, s, c);
    };
    for (const auto& [na, nb] : {std::pair<std::size_t, std::size_t>{100, 130},
                                 {100, 300},
                                 {120, 130},
                                 {120, 140},
                                 {200, 220},
                                 {600, 700},
                                 {60, 400}}) {
        kt::expect_kernel_matches_long(kernel, na, nb);
        kt::expect_kernel_matches_long(kernel, nb, na);
    }
}

TEST(Multiplication, ToomCook6_5Squaring) {
    // a * a (same operand) at sizes that exercise the tier.
    const std::string a = bmp::random_big_int(scaled(cutoff, 5, 4) * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, a));

    const std::string b = bmp::random_big_int(scaled(cutoff, 5, 2) * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, b, b));
}

TEST(Multiplication, ToomCook6_5SignedOperands) {
    // Negative operand handling is independent of the algorithm choice, but confirm the sign propagation works.
    const std::size_t n = scaled(cutoff, 5, 4);
    const std::string a = bmp::random_big_int(n * limb_bits, /*negative=*/true);
    const std::string b = bmp::random_big_int(n * limb_bits, /*negative=*/false);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));

    const std::string c = bmp::random_big_int(n * limb_bits, /*negative=*/true);
    const std::string d = bmp::random_big_int(n * limb_bits, /*negative=*/true);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, c, d));
}
