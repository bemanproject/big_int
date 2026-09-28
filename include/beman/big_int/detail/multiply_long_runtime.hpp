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

inline void beman_big_int_multiply_long_runtime(beman::big_int::uint_multiprecision_t*       p_result,
                                                const beman::big_int::uint_multiprecision_t* p_a,
                                                const std::size_t                            len_a,
                                                const beman::big_int::uint_multiprecision_t* p_b,
                                                const std::size_t                            len_b) noexcept {
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
