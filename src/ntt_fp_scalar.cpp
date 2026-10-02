// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>
#include <beman/big_int/detail/simd/vec_scalar.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>
#include <beman/big_int/detail/ntt.hpp>

// The scalar (portable) FP NTT kernels, plus the shared twiddle build. Always
// compiled; the universal fallback when no SIMD kernel applies.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

void ntt_fp_forward_scalar(double* const         data,
                           const std::size_t     n,
                           const double* const   tw,
                           const ntt_fp_modulus& m) noexcept {
    ntt_fp_forward_impl<vec1d>(data, n, tw, m);
}

void ntt_fp_inverse_scalar(double* const         data,
                           const std::size_t     n,
                           const double* const   tw,
                           const ntt_fp_modulus& m) noexcept {
    ntt_fp_inverse_impl<vec1d>(data, n, tw, m);
}

void ntt_fp_pointwise_scalar(double* const         a,
                             const double* const   b,
                             const std::size_t     n,
                             const ntt_fp_modulus& m) noexcept {
    ntt_fp_pointwise_impl<vec1d>(a, b, n, m);
}

void ntt_fp_build_twiddles(const std::span<double> twiddles,
                           const std::size_t       n,
                           const ntt_fp_modulus&   m,
                           const ntt_direction     direction) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0 && (n & (n - 1)) == 0);
    BEMAN_BIG_INT_DEBUG_ASSERT((m.mod.p - 1) % static_cast<std::uint64_t>(n) == 0);
    if (n <= 1) {
        return; // length 1: no butterflies, hence no twiddles
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(twiddles.size() >= n - 1);

    // Per level (length `len`), store the contiguous powers w_len^0 .. w_len^(half-1)
    // where w_len = root^(n/len). Forward visits levels n, n/2, ..., 2; the inverse
    // visits 2, ..., n -- each in the order its transform reads the table.
    const std::uint64_t w_n  = m.mod.pow(m.mod.g, (m.mod.p - 1) / static_cast<std::uint64_t>(n));
    const std::uint64_t root = direction == ntt_direction::inverse ? m.mod.inv(w_n) : w_n;
    std::size_t         off  = 0;

    const auto fill_level = [&](const std::size_t len) noexcept {
        const std::size_t   half  = len >> 1;
        const std::uint64_t w_len = m.mod.pow(root, static_cast<std::uint64_t>(n / len));
        std::uint64_t       r     = 1; // w_len^0
        for (std::size_t j = 0; j < half; ++j) {
            twiddles[off + j] = fp_center(r, m.mod.p);
            r                 = m.mod.mul(r, w_len);
        }
        off += half;
    };

    if (direction == ntt_direction::inverse) {
        for (std::size_t len = 2; len <= n; len <<= 1) {
            fill_level(len);
        }
    } else {
        for (std::size_t len = n; len > 1; len >>= 1) {
            fill_level(len);
        }
    }
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
