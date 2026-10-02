// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt.hpp>

#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>

// Iterative radix-2 number-theoretic transform. Data is kept in ordinary
// residue form throughout; twiddles are precomputed once per (prime, length) by
// ntt_build_twiddles into a Montgomery-form table, so each butterfly multiply is
// a single mont_mul plus a table lookup. Radix-4 and lazy reduction remain
// possible future optimizations. A Shoup precomputed-quotient multiply was tried
// and measured ~15% slower here: its second per-twiddle table doubles twiddle
// memory traffic, which outweighs its shorter dependency chain on these
// bandwidth-bound transforms (it may pay off later in a compute-bound, e.g.
// SIMD, regime).

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

void ntt_build_twiddles(const std::span<std::uint64_t> twiddles,
                        const std::size_t              n,
                        const ntt_modulus&             mod,
                        const ntt_direction            direction) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0 && (n & (n - 1)) == 0);
    BEMAN_BIG_INT_DEBUG_ASSERT((mod.p - 1) % static_cast<std::uint64_t>(n) == 0);
    const std::size_t half = n >> 1;
    if (half == 0) {
        return; // length 1: no butterflies, hence no twiddles
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(twiddles.size() >= half);

    // twiddles[m] = montgomery(root^m), root = the n-th (inverse) root of unity.
    const std::uint64_t w_n       = mod.pow(mod.g, (mod.p - 1) / static_cast<std::uint64_t>(n));
    const std::uint64_t root      = direction == ntt_direction::inverse ? mod.inv(w_n) : w_n;
    const std::uint64_t root_mont = mod.to_mont(root);
    twiddles[0]                   = mod.to_mont(std::uint64_t{1});
    for (std::size_t m = 1; m < half; ++m) {
        twiddles[m] = mod.mont_mul(twiddles[m - 1], root_mont);
    }
}

void ntt_forward(const std::span<std::uint64_t>       data,
                 const std::span<const std::uint64_t> twiddles,
                 const ntt_modulus&                   mod) noexcept {
    const std::size_t n = data.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0 && (n & (n - 1)) == 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(n == 1 || twiddles.size() >= (n >> 1));

    // Gentleman-Sande decimation-in-frequency: output ends up in bit-reversed
    // order. The level-`len` twiddle for index j is twiddles[j * (n / len)].
    for (std::size_t len = n; len > 1; len >>= 1) {
        const std::size_t half   = len >> 1;
        const std::size_t stride = n / len;
        for (std::size_t start = 0; start < n; start += len) {
            for (std::size_t j = 0; j < half; ++j) {
                const std::uint64_t w  = twiddles[j * stride];
                const std::uint64_t u  = data[start + j];
                const std::uint64_t v  = data[start + j + half];
                data[start + j]        = mod.add(u, v);
                data[start + j + half] = mod.mont_mul(mod.sub(u, v), w);
            }
        }
    }
}

void ntt_inverse(const std::span<std::uint64_t>       data,
                 const std::span<const std::uint64_t> twiddles,
                 const ntt_modulus&                   mod) noexcept {
    const std::size_t n = data.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0 && (n & (n - 1)) == 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(n == 1 || twiddles.size() >= (n >> 1));

    // Cooley-Tukey decimation-in-time with inverse twiddles (the table built with
    // ntt_direction::inverse): consumes the bit-reversed layout left by
    // ntt_forward and restores natural order.
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half   = len >> 1;
        const std::size_t stride = n / len;
        for (std::size_t start = 0; start < n; start += len) {
            for (std::size_t j = 0; j < half; ++j) {
                const std::uint64_t w  = twiddles[j * stride];
                const std::uint64_t u  = data[start + j];
                const std::uint64_t v  = mod.mont_mul(data[start + j + half], w);
                data[start + j]        = mod.add(u, v);
                data[start + j + half] = mod.sub(u, v);
            }
        }
    }

    // Scale by 1/n.
    const std::uint64_t n_inv_mont = mod.to_mont(mod.inv(static_cast<std::uint64_t>(n) % mod.p));
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = mod.mont_mul(data[i], n_inv_mont);
    }
}

void ntt_pointwise(const std::span<std::uint64_t>       a,
                   const std::span<const std::uint64_t> b,
                   const ntt_modulus&                   mod) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == b.size());
    const std::size_t n = a.size();
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = mod.mul(a[i], b[i]);
    }
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
