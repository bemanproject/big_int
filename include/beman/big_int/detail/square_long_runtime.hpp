// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SQUARE_LONG_RUNTIME_HPP
#define BEMAN_BIG_INT_SQUARE_LONG_RUNTIME_HPP

#include <beman/big_int/detail/wide_ops.hpp>

// Schoolbook squaring: p_result[0 .. 2 * len_a) = a^2. Every limb of the
// product is written, so p_result need not be pre-zeroed; it must not alias p_a.
// Builds the off-diagonal triangle, then doubles it and adds the diagonal
// squares in a single pass (the x86_64/AArch64 assembly follows the same plan).
BEMAN_BIG_INT_ASM_LINKAGE void beman_big_int_square_long_runtime(beman::big_int::uint_multiprecision_t*       p_result,
                                                                 const beman::big_int::uint_multiprecision_t* p_a,
                                                                 const std::size_t len_a) noexcept
#if defined(BEMAN_BIG_INT_HAS_ASM_KERNELS)
    ;
#else
{
    using limb = beman::big_int::uint_multiprecision_t;

    if (len_a == 0) {
        return;
    }

    // Triangle, one row per a[i] against a[i + 1 ..]. Row 0 stores and writes
    // p_result[1 .. len_a]; each later row accumulates one limb further on.
    {
        limb carry = 0;
        for (std::size_t j = 1; j < len_a; ++j) {
            const auto [lo, hi] = beman::big_int::detail::widening_mul(*p_a, *(p_a + j));
            const auto [s, c]   = beman::big_int::detail::carrying_add(lo, carry);
            *(p_result + j)     = s;
            carry               = hi + static_cast<limb>(c);
        }
        *(p_result + len_a) = carry;
    }

    for (std::size_t i = 1; i + 1 < len_a; ++i) {
        limb carry = 0;
        for (std::size_t j = i + 1; j < len_a; ++j) {
            const auto [lo, hi]   = beman::big_int::detail::widening_mul(*(p_a + i), *(p_a + j));
            const auto [s1, c1]   = beman::big_int::detail::carrying_add(lo, *(p_result + (i + j)));
            const auto [s2, c2]   = beman::big_int::detail::carrying_add(s1, carry);
            *(p_result + (i + j)) = s2;
            carry                 = hi + static_cast<limb>(c1) + static_cast<limb>(c2);
        }
        *(p_result + (i + len_a)) = carry;
    }

    // The triangle's two end limbs were never written.
    *p_result                     = 0;
    *(p_result + (2 * len_a - 1)) = 0;

    // Double the triangle and add the diagonal squares, pair by pair. `extra`
    // (at most 2) carries the bit doubling shifts out of the pair's high limb
    // plus the carry out of the pair's sum.
    constexpr std::size_t top_shift = beman::big_int::detail::width_v<limb> - 1;

    limb extra = 0;
    for (std::size_t i = 0; i < len_a; ++i) {
        const limb t_lo = *(p_result + 2 * i);
        const limb t_hi = *(p_result + (2 * i + 1));

        const limb d_lo = t_lo << 1u;
        const limb d_hi = (t_hi << 1u) | (t_lo >> top_shift);

        const auto [sq_lo, sq_hi] = beman::big_int::detail::widening_mul(*(p_a + i), *(p_a + i));
        const auto [s0, c0]       = beman::big_int::detail::carrying_add(sq_lo, d_lo);
        const auto [s1, c1]       = beman::big_int::detail::carrying_add(sq_hi, d_hi, c0);
        const auto [s2, c2]       = beman::big_int::detail::carrying_add(s0, extra);
        const auto [s3, c3]       = beman::big_int::detail::carrying_add(s1, limb{0}, c2);

        *(p_result + 2 * i)       = s2;
        *(p_result + (2 * i + 1)) = s3;
        extra                     = (t_hi >> top_shift) + static_cast<limb>(c1) + static_cast<limb>(c3);
    }
}

#endif // !defined(BEMAN_BIG_INT_HAS_ASM_KERNELS)

#endif // BEMAN_BIG_INT_SQUARE_LONG_RUNTIME_HPP
