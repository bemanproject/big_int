// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

#include "boost_mp_testing.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

using ::beman::big_int::uint_multiprecision_t;
using ::beman::big_int::detail::fft_choose_coeff_bits;
using ::beman::big_int::detail::multiply_fft;
using ::beman::big_int::detail::square_fft;
#if defined(BEMAN_BIG_INT_SIMD_MUL)
using ::beman::big_int::detail::fft_mul_fp_storage_size;
using ::beman::big_int::detail::fft_mul_int_storage_size;
using ::beman::big_int::detail::square_fft_fp_storage_size;
using ::beman::big_int::detail::square_fft_int_storage_size;
#else
using ::beman::big_int::detail::fft_mul_storage_size;
using ::beman::big_int::detail::square_fft_storage_size;
#endif
using ::boost::multiprecision::cpp_int;

inline constexpr unsigned limb_bits = std::numeric_limits<uint_multiprecision_t>::digits;

// Adaptive coefficient-bit selection: the common range picks b = 50 (the max); b
// drops only for very large operands, and sooner for the tighter two-prime integer
// CRT (~2^124) than the three-prime FP CRT (~2^149). (Validated on 64-bit limbs.)
#if BEMAN_BIG_INT_LIMB_WIDTH == 64
static_assert(fft_choose_coeff_bits(1000, 1000) == 50);
static_assert(fft_choose_coeff_bits(100000, 100000) == 50);
static_assert(fft_choose_coeff_bits(1, 1000000) == 50);
    #if defined(BEMAN_BIG_INT_SIMD_MUL)
static_assert(fft_choose_coeff_bits(50000000, 50000000) == 50); // three ~50-bit primes
    #else
static_assert(fft_choose_coeff_bits(50000000, 50000000) == 48); // two ~62-bit primes
    #endif
#endif

// Random limb vector with a non-zero top limb (so the trimmed size is exactly n).
[[nodiscard]] std::vector<uint_multiprecision_t> random_limbs(std::mt19937_64& rng, const std::size_t n) {
    std::uniform_int_distribution<uint_multiprecision_t> dist;
    std::vector<uint_multiprecision_t>                   v(n);
    for (auto& x : v) {
        x = dist(rng);
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

[[nodiscard]] cpp_int from_limbs(const std::span<const uint_multiprecision_t> limbs) {
    cpp_int v = 0;
    for (std::size_t i = limbs.size(); i-- > 0;) {
        v <<= limb_bits;
        v += limbs[i];
    }
    return v;
}

[[nodiscard]] ::testing::AssertionResult
check_multiply(std::mt19937_64& rng, const std::size_t na, const std::size_t nb) {
    const std::vector<uint_multiprecision_t> a = random_limbs(rng, na);
    const std::vector<uint_multiprecision_t> b = random_limbs(rng, nb);
    std::vector<uint_multiprecision_t>       result(na + nb, uint_multiprecision_t{0});
#if defined(BEMAN_BIG_INT_SIMD_MUL)
    std::vector<double>        fp_ws(fft_mul_fp_storage_size(na, nb));
    std::vector<std::uint64_t> int_ws(fft_mul_int_storage_size(na, nb));
    multiply_fft(result, a, b, fp_ws, int_ws);
#else
    std::vector<std::uint64_t> ws(fft_mul_storage_size(na, nb));
    multiply_fft(result, a, b, ws);
#endif

    const cpp_int expected = from_limbs(a) * from_limbs(b);
    const cpp_int got      = from_limbs(result);
    if (got != expected) {
        return ::testing::AssertionFailure() << "multiply mismatch at na=" << na << " nb=" << nb;
    }
    return ::testing::AssertionSuccess();
}

[[nodiscard]] ::testing::AssertionResult check_square(std::mt19937_64& rng, const std::size_t na) {
    const std::vector<uint_multiprecision_t> a = random_limbs(rng, na);
    std::vector<uint_multiprecision_t>       result(2 * na, uint_multiprecision_t{0});
#if defined(BEMAN_BIG_INT_SIMD_MUL)
    std::vector<double>        fp_ws(square_fft_fp_storage_size(na));
    std::vector<std::uint64_t> int_ws(square_fft_int_storage_size(na));
    square_fft(result, a, fp_ws, int_ws);
#else
    std::vector<std::uint64_t> ws(square_fft_storage_size(na));
    square_fft(result, a, ws);
#endif

    const cpp_int base     = from_limbs(a);
    const cpp_int expected = base * base;
    const cpp_int got      = from_limbs(result);
    if (got != expected) {
        return ::testing::AssertionFailure() << "square mismatch at na=" << na;
    }
    return ::testing::AssertionSuccess();
}

// Sizes spanning tiny, around powers of two, odd, and large enough that
// convolution coefficients use the high limb of the CRT value and grow the
// recomposition accumulator.
constexpr std::size_t sizes[] = {1,  2,   3,   4,   5,   7,   8,   9,   15,  16,   17,   31,   32,   33,   63,   64,
                                 65, 100, 127, 128, 129, 256, 511, 512, 513, 1000, 1024, 2000, 2048, 3000, 4096, 5000};

TEST(FftMul, MultiplyEqualSizesDifferential) {
    std::mt19937_64 rng{0xf17u};
    for (const std::size_t n : sizes) {
        ASSERT_TRUE(check_multiply(rng, n, n));
    }
}

TEST(FftMul, MultiplyUnequalSizesDifferential) {
    std::mt19937_64 rng{0xf18u};
    for (const std::size_t n : sizes) {
        // A handful of unequal pairings, including very lopsided ones.
        ASSERT_TRUE(check_multiply(rng, n, n + 1));
        ASSERT_TRUE(check_multiply(rng, n + 1, n));
        ASSERT_TRUE(check_multiply(rng, n, (n / 2) + 1));
        ASSERT_TRUE(check_multiply(rng, 1, n));
    }
}

TEST(FftMul, SquareDifferential) {
    std::mt19937_64 rng{0xf19u};
    for (const std::size_t n : sizes) {
        ASSERT_TRUE(check_square(rng, n));
    }
}

// End-to-end through the public API: operands sized just above the FFT floor
// (and, when the cost model is on, at the first size where the model picks the
// FFT) route through the FFT branch of multiply_dispatch / square_dispatch.
// Tied to the gate helpers so they keep exercising FFT after the gates are tuned.
namespace bmp = ::beman::big_int::boost_mp_testing;

namespace {

// Balanced sizes above this are skipped: the cost model takes the FFT only that late on some configurations, and the
// quadratic Boost reference would dominate the run (the FFT dispatch is still covered by multiplication_unbalanced).
constexpr std::size_t max_reference_limbs = 50'000;

// Smallest n above `floor` (stepping about 1/8 at a time) for which the gate accepts an n x n product.
template <class Gate>
std::size_t first_size_taking_fft(const std::size_t floor, Gate&& gate) {
    std::size_t n = floor + 1;
    while (!gate(n)) {
        n += n / 8 + 1;
        if (n > 64 * floor) {
            ADD_FAILURE() << "the FFT gate accepts no size up to 64 * floor";
            break;
        }
    }
    return n;
}

} // namespace

TEST(FftMul, DispatchIntegrationMultiply) {
    const std::size_t n = first_size_taking_fft(::beman::big_int::detail::fft_mul_min_limbs, [](const std::size_t m) {
        return ::beman::big_int::detail::fft_mul_worthwhile(m, m);
    });
    if (n > max_reference_limbs) {
        GTEST_SKIP() << "the gate takes the FFT only from " << n << " limbs here: too large for the Boost reference";
    }
    const std::size_t bits = n * limb_bits;
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, bmp::random_big_int(bits), bmp::random_big_int(bits)));
}

TEST(FftMul, DispatchIntegrationSquare) {
    const std::size_t n =
        first_size_taking_fft(::beman::big_int::detail::square_fft_min_limbs,
                              [](const std::size_t m) { return ::beman::big_int::detail::square_fft_worthwhile(m); });
    if (n > max_reference_limbs) {
        GTEST_SKIP() << "the gate takes the FFT only from " << n << " limbs here: too large for the Boost reference";
    }
    const std::size_t bits = n * limb_bits;
    // A single object squared (v * v) takes the square_dispatch path.
    EXPECT_TRUE(bmp::check_cpp_int_equal_unary([](const auto& v) { return v * v; }, bmp::random_big_int(bits)));
}

} // namespace
