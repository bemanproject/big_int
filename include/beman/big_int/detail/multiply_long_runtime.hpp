// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
#define BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP

#include <beman/big_int/detail/wide_ops.hpp>

#if defined(BEMAN_BIG_INT_ARCH_X86_64)

// Compile-time selection between the two x86-64 kernels (config.hpp resolves
// BEMAN_BIG_INT_X86_64_BMI2_ADX), so there is no runtime dispatch cost.
extern "C" void beman_big_int_multiply_long_runtime_generic(beman::big_int::uint_multiprecision_t*       p_result,
                                                            const beman::big_int::uint_multiprecision_t* p_a,
                                                            const std::size_t                            len_a,
                                                            const beman::big_int::uint_multiprecision_t* p_b,
                                                            const std::size_t len_b) noexcept;
extern "C" void beman_big_int_multiply_long_runtime_bmi2_adx(beman::big_int::uint_multiprecision_t*       p_result,
                                                             const beman::big_int::uint_multiprecision_t* p_a,
                                                             const std::size_t                            len_a,
                                                             const beman::big_int::uint_multiprecision_t* p_b,
                                                             const std::size_t len_b) noexcept;
// Always declared on x86-64 (the .s/.asm stub or real kernel is always
// assembled), even when BEMAN_BIG_INT_X86_64_AVX512_IFMA resolves to 0.
extern "C" void beman_big_int_multiply_long_runtime_avx512_ifma(beman::big_int::uint_multiprecision_t*       p_result,
                                                                const beman::big_int::uint_multiprecision_t* p_a,
                                                                const std::size_t                            len_a,
                                                                const beman::big_int::uint_multiprecision_t* p_b,
                                                                const std::size_t len_b) noexcept;

namespace beman::big_int::detail {

// The AVX-512 IFMA kernel has a fixed set-up cost, so it pays off from a size that shrinks as the operands get more
// lopsided (its cost follows the shorter operand in 8-digit blocks, the BMI2/ADX kernel's the whole product). Tuned
// on the i9-11900K, 2026-09-29, at the crossover of the two kernels timed with the buffers away from 4 KiB
// aliasing: about 18 limbs balanced, 16x20 at 5:4, 13x26 at 2:1, 12x36 at 3:1, 8x64 at 8:1, 7x112 at 16:1 and
// 6x192 at 32:1. Below ifma_multiply_min_limbs the BMI2/ADX kernel always wins.
inline constexpr std::size_t ifma_multiply_min_limbs        = 6;
inline constexpr std::size_t ifma_multiply_mid_limbs        = 8;
inline constexpr std::size_t ifma_multiply_high_limbs       = 13;
inline constexpr std::size_t ifma_multiply_always_limbs     = 20;
inline constexpr std::size_t ifma_multiply_min_product_low  = 900;
inline constexpr std::size_t ifma_multiply_min_product_mid  = 450;
inline constexpr std::size_t ifma_multiply_min_product_high = 330;

static_assert(ifma_multiply_min_limbs <= ifma_multiply_mid_limbs);
static_assert(ifma_multiply_mid_limbs <= ifma_multiply_high_limbs);
static_assert(ifma_multiply_high_limbs <= ifma_multiply_always_limbs);

constexpr bool ifma_multiply_worthwhile(const std::size_t len_a, const std::size_t len_b) noexcept {
    const std::size_t lo = len_a < len_b ? len_a : len_b;
    if (lo < ifma_multiply_min_limbs) {
        return false;
    }
    if (lo >= ifma_multiply_always_limbs) {
        return true;
    }
    const std::size_t product = len_a * len_b;
    if (lo >= ifma_multiply_high_limbs) {
        return product >= ifma_multiply_min_product_high;
    }
    if (lo >= ifma_multiply_mid_limbs) {
        return product >= ifma_multiply_min_product_mid;
    }
    return product >= ifma_multiply_min_product_low;
}

} // namespace beman::big_int::detail

    // Feature macro for tools that print the gate constants above.
    #define BEMAN_BIG_INT_HAS_IFMA_MULTIPLY_GATE 1

inline void beman_big_int_multiply_long_runtime(beman::big_int::uint_multiprecision_t*       p_result,
                                                const beman::big_int::uint_multiprecision_t* p_a,
                                                const std::size_t                            len_a,
                                                const beman::big_int::uint_multiprecision_t* p_b,
                                                const std::size_t                            len_b) noexcept {
    #if BEMAN_BIG_INT_X86_64_AVX512_IFMA
    if (beman::big_int::detail::ifma_multiply_worthwhile(len_a, len_b)) {
        beman_big_int_multiply_long_runtime_avx512_ifma(p_result, p_a, len_a, p_b, len_b);
        return;
    }
    #endif
    #if BEMAN_BIG_INT_X86_64_BMI2_ADX
    beman_big_int_multiply_long_runtime_bmi2_adx(p_result, p_a, len_a, p_b, len_b);
    #else
    beman_big_int_multiply_long_runtime_generic(p_result, p_a, len_a, p_b, len_b);
    #endif
}

#else

BEMAN_BIG_INT_ASM_LINKAGE void beman_big_int_multiply_long_runtime(beman::big_int::uint_multiprecision_t* p_result,
                                                                   const beman::big_int::uint_multiprecision_t* p_a,
                                                                   const std::size_t                            len_a,
                                                                   const beman::big_int::uint_multiprecision_t* p_b,
                                                                   const std::size_t len_b) noexcept
    #if defined(BEMAN_BIG_INT_HAS_ASM_KERNELS)
    ;
    #else
{
    if (len_a == 0 || len_b == 0) {
        return;
    }

    {
        beman::big_int::uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < len_b; ++j) {
            const auto [lo, hi] = beman::big_int::detail::widening_mul(*p_a, *(p_b + j));
            const auto [s, c]   = beman::big_int::detail::carrying_add(lo, carry);
            *(p_result + j)     = s;
            carry               = hi + static_cast<beman::big_int::uint_multiprecision_t>(c);
        }
        *(p_result + len_b) = carry;
    }

    // Subsequent rows: accumulate onto values written by previous rows.
    for (std::size_t i = 1; i < len_a; ++i) {
        beman::big_int::uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < len_b; ++j) {
            const auto [lo, hi]   = beman::big_int::detail::widening_mul(*(p_a + i), *(p_b + j));
            const auto [s1, c1]   = beman::big_int::detail::carrying_add(lo, *(p_result + (i + j)));
            const auto [s2, c2]   = beman::big_int::detail::carrying_add(s1, carry);
            *(p_result + (i + j)) = s2;
            carry                 = hi + static_cast<beman::big_int::uint_multiprecision_t>(c1) +
                                    static_cast<beman::big_int::uint_multiprecision_t>(c2);
        }
        *(p_result + (i + len_b)) = carry;
    }
}

    #endif // !defined(BEMAN_BIG_INT_HAS_ASM_KERNELS)

#endif // defined(BEMAN_BIG_INT_ARCH_X86_64)

#endif // BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
