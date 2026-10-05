// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include <beman/big_int.hpp>
#include <beman/big_int/detail/mul_impl.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace bmp = ::BEMAN_BIG_INT_NAMESPACE::boost_mp_testing;

namespace {

using uint_t        = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using std_allocator = std::allocator<uint_t>;
using scratch_t     = ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<std_allocator>;

constexpr std::size_t limb_bits =
    static_cast<std::size_t>(std::numeric_limits<::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);

std::vector<uint_t> make_random(const std::size_t limbs, const std::uint64_t seed) {
    std::mt19937_64                       rng{seed};
    std::uniform_int_distribution<uint_t> dist;
    std::vector<uint_t>                   v(limbs);
    for (auto& x : v) {
        x = dist(rng);
    }
    if (!v.empty() && v.back() == 0) {
        v.back() = 1; // keep the operand at full limb count
    }
    return v;
}

// Forces Toom-8.5 at the top level via cutoff_override=1 and compares against an
// independent reference (schoolbook for small sizes, Toom-6.5 for large ones).
void expect_mul_matches(const std::size_t na, const std::size_t nb, const bool big_ref = false) {
    SCOPED_TRACE("multiply na=" + std::to_string(na) + " nb=" + std::to_string(nb));
    const auto a = make_random(na, 0x9E3779B97F4A7C15ULL ^ (na * 1315423911u));
    const auto b = make_random(nb, 0xC2B2AE3D27D4EB4FULL ^ (nb * 2654435761u));

    const auto a_view = std::span<const uint_t>{a};
    const auto b_view = std::span<const uint_t>{b};

    std::vector<uint_t> got(na + nb, uint_t{0});
    std::vector<uint_t> ref(na + nb, uint_t{0});

    std_allocator alloc;
    scratch_t     scratch(16 * std::max(na, nb) + 4096, alloc);
    ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_8_5(
        std::span<uint_t>{got}, a_view, b_view, scratch, std::size_t{1});

    if (big_ref) {
        scratch_t ref_scratch(16 * std::max(na, nb) + 4096, alloc);
        ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_toom_cook_6_5(
            std::span<uint_t>{ref}, a_view, b_view, ref_scratch, std::size_t{1});
    } else {
        ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_long(std::span<uint_t>{ref}, a_view, b_view);
    }

    EXPECT_EQ(got, ref);
}

void expect_sqr_matches(const std::size_t n, const bool big_ref = false) {
    SCOPED_TRACE("square n=" + std::to_string(n));
    const auto a      = make_random(n, 0xD1B54A32D192ED03ULL ^ (n * 40503u));
    const auto a_view = std::span<const uint_t>{a};

    std::vector<uint_t> got(2 * n, uint_t{0});
    std::vector<uint_t> ref(2 * n, uint_t{0});

    std_allocator alloc;
    scratch_t     scratch(16 * n + 4096, alloc);
    ::BEMAN_BIG_INT_NAMESPACE::detail::square_toom_cook_8_5(std::span<uint_t>{got}, a_view, scratch, std::size_t{1});

    if (big_ref) {
        scratch_t ref_scratch(16 * n + 4096, alloc);
        ::BEMAN_BIG_INT_NAMESPACE::detail::square_toom_cook_6_5(
            std::span<uint_t>{ref}, a_view, ref_scratch, std::size_t{1});
    } else {
        ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_long(std::span<uint_t>{ref}, a_view, a_view);
    }

    EXPECT_EQ(got, ref);
}

} // namespace

// ---- Balanced multiplication (b8 empty, c15 = 0), full pieces. ----
TEST(ToomCook8_5, BalancedFullPieces) {
    for (const std::size_t m : {1u, 2u, 3u, 5u, 8u, 16u, 32u, 64u, 100u, 125u, 250u}) {
        expect_mul_matches(8 * m, 8 * m); // k = m, all eight a-pieces full, a7 full
    }
}

// ---- Balanced with partial top pieces (a7 / b7 partial but non-empty). ----
TEST(ToomCook8_5, BalancedPartialPieces) {
    for (const std::size_t n : {15u, 22u, 23u, 31u, 65u, 79u, 80u, 127u, 129u, 200u, 255u, 257u, 999u, 1001u}) {
        expect_mul_matches(n, n);
    }
}

// ---- Asymmetric within the 9:8 gate, b8 empty (nb <= 8k -> c15 = 0). ----
TEST(ToomCook8_5, AsymmetricC15Zero) {
    expect_mul_matches(799, 800);   // k=100, 8k=800 -> b8 empty; a7 partial, b7 full
    expect_mul_matches(1599, 1600); // k=200, 8k=1600 -> b8 empty
}

// ---- Asymmetric with b8 non-empty (8k < nb <= 9k -> c15 = a7*b8 != 0). ----
TEST(ToomCook8_5, AsymmetricC15NonZero) {
    expect_mul_matches(800, 810); // k=100, 8k=800 < 810 <= 900=9k
    expect_mul_matches(800, 850);
    expect_mul_matches(800, 900); // nb == 9k exactly
    expect_mul_matches(640, 720); // k=80, 9k=720
    expect_mul_matches(1600, 1700);
    expect_mul_matches(1600, 1800); // 9k exactly
}

// ---- Sizes that must fall back (max > 9k) still produce correct products. ----
TEST(ToomCook8_5, RatioFallback) {
    expect_mul_matches(800, 901);  // 901 > 9k=900 -> fall back to Toom-6.5
    expect_mul_matches(800, 2000); // far beyond ratio
    expect_mul_matches(700, 1500); // k=88, 9k=792 < 1500 -> fallback
}

// ---- Squaring (always balanced, c15 = 0). ----
TEST(ToomCook8_5, Squaring) {
    for (const std::size_t n : {8u, 16u, 23u, 64u, 79u, 128u, 200u, 256u, 800u, 999u, 1600u}) {
        expect_sqr_matches(n);
    }
}

// ---- Two-level recursion: from 8 * toom_cook_8_5_cutoff limbs the sub-products clear the cutoff and run
// Toom-8.5 again. Reference is the independent Toom-6.5 kernel.
// (One case only; 2-level Toom needs many limbs and is slow under MaxSan.) ----
TEST(ToomCook8_5, DeepRecursionVsToom65) {
    const std::size_t n = 8 * ::BEMAN_BIG_INT_NAMESPACE::detail::toom_cook_8_5_cutoff + 1000;
    expect_mul_matches(n, n, /*big_ref=*/true);
}

// ---- Public-API integration: operator* must agree with Boost.Multiprecision at and above the Toom-8.5 cutoff
// (the FFT gate may take the product first, depending on the configuration). ----
TEST(ToomCook8_5, DispatchAtCutoff) {
    const std::size_t cutoff = ::BEMAN_BIG_INT_NAMESPACE::detail::toom_cook_8_5_cutoff;
    const std::string a      = bmp::random_big_int(cutoff * limb_bits);
    const std::string b      = bmp::random_big_int(cutoff * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}

TEST(ToomCook8_5, DispatchAboveCutoffAndSquare) {
    const std::size_t cutoff = ::BEMAN_BIG_INT_NAMESPACE::detail::toom_cook_8_5_cutoff;
    const std::string a      = bmp::random_big_int((cutoff + 1000) * limb_bits);
    const std::string b      = bmp::random_big_int((cutoff + 2000) * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));

    // x * x at >= square_toom_cook_8_5_cutoff routes through square_dispatch ->
    // square_toom_cook_8_5 (or the square FFT).
    const std::string c =
        bmp::random_big_int(::BEMAN_BIG_INT_NAMESPACE::detail::square_toom_cook_8_5_cutoff * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, c, c));
}

TEST(ToomCook8_5, DispatchSignedOperands) {
    const std::size_t n = ::BEMAN_BIG_INT_NAMESPACE::detail::toom_cook_8_5_cutoff + 1000;
    const std::string a = bmp::random_big_int(n * limb_bits, /*negative=*/true);
    const std::string b = bmp::random_big_int(n * limb_bits, /*negative=*/false);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}
