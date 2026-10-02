// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_NTT_HPP
#define BEMAN_BIG_INT_NTT_HPP

#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Three NTT-friendly primes p = c * 2^k + 1, all < 2^62 (the sub-62-bit
// headroom leaves room for later lazy reduction). Their product ~ 2^186 bounds
// the largest exact integer convolution coefficient that can be recovered by
// CRT; the smallest 2-adicity (52) caps the transform length at 2^52. Primes,
// primitive roots, and 2-adicities were verified independently.
inline constexpr ntt_modulus ntt_primes[3] = {
    ntt_modulus::make(4179340454199820289ull, 3ull, 57ull), // 29 * 2^57 + 1
    ntt_modulus::make(4512606826625236993ull, 7ull, 53ull), // 501 * 2^53 + 1
    ntt_modulus::make(4472074429978902529ull, 7ull, 52ull), // 993 * 2^52 + 1
};

// Selects which twiddle table ntt_build_twiddles produces. An enum rather than a
// bare bool, so call sites read `ntt_direction::inverse` instead of `true`.
enum class ntt_direction { forward, inverse };

// Fill `twiddles[0 .. n/2)` with the Montgomery-form powers of the n-th root of
// unity (or its inverse, per `direction`), for use by ntt_forward / ntt_inverse
// on a transform of length `n`. `n` must be a power of two and at most
// 2^mod.log2_order. The same table serves both operands of a convolution.
void ntt_build_twiddles(std::span<std::uint64_t> twiddles,
                        std::size_t              n,
                        const ntt_modulus&       mod,
                        ntt_direction            direction) noexcept;

// In-place forward transform: maps `data` (length a power of two, ordinary
// residues mod mod.p) to its NTT in bit-reversed order (Gentleman-Sande
// decimation-in-frequency, no explicit bit-reversal). `data.size()` must be a
// power of two and at most 2^mod.log2_order. `twiddles` is a forward table from
// ntt_build_twiddles sized for `data.size()`.
void ntt_forward(std::span<std::uint64_t>       data,
                 std::span<const std::uint64_t> twiddles,
                 const ntt_modulus&             mod) noexcept;

// In-place inverse transform: consumes the bit-reversed output of ntt_forward
// (Cooley-Tukey decimation-in-time) and restores natural order, including the
// final 1/N scaling. Pairs with ntt_forward so the bit-reversal cancels.
// `twiddles` is an inverse table from ntt_build_twiddles sized for `data.size()`.
void ntt_inverse(std::span<std::uint64_t>       data,
                 std::span<const std::uint64_t> twiddles,
                 const ntt_modulus&             mod) noexcept;

// Elementwise modular product a[i] <- a[i] * b[i] mod mod.p. Order-agnostic, so
// it composes with the bit-reversed layout between forward and inverse.
void ntt_pointwise(std::span<std::uint64_t> a, std::span<const std::uint64_t> b, const ntt_modulus& mod) noexcept;

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_NTT_HPP
