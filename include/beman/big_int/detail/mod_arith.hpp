// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MOD_ARITH_HPP
#define BEMAN_BIG_INT_MOD_ARITH_HPP

// This header (and its src-only siblings: ntt.hpp, ntt_fp.hpp,
// multiply_long_runtime.hpp, and simd/vec_*.hpp) is included only by src/*.cpp,
// which are compiled without BEMAN_BIG_INT_BUILD_MODULE. Its std-library
// includes are therefore left unguarded on purpose: guarding them here would
// silently drop these includes from the compiled library.
#include <cstdint>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Modular arithmetic over a single word-size prime, the foundation for the NTT
// that backs FFT multiplication. Values are 64-bit regardless of the library
// limb width: the chosen primes are 62-bit, so the transform always works in
// 64-bit arithmetic and the packing layer (src/fft_mul.cpp) bridges to limbs.
//
// Multiplication uses Montgomery reduction (REDC), which needs only a
// 64x64->128 widening multiply (widening_mul) plus 64-bit add -- no 128-bit
// division -- so it is portable and usable in constant expressions. Transform
// data is kept in ordinary residue form [0, p); twiddles are stored in
// Montgomery form so a single mont_mul() maps an ordinary residue to the
// ordinary-form product: mont_mul(a, to_mont(w)) == a*w mod p. (A Shoup
// precomputed-quotient multiply was tried for the butterfly but measured slower
// here -- see the note in src/ntt.cpp.)
struct ntt_modulus {
    std::uint64_t p;          // the prime modulus (< 2^62)
    std::uint64_t n_prime;    // -p^-1 mod 2^64
    std::uint64_t r_squared;  // 2^128 mod p (Montgomery conversion constant)
    std::uint64_t g;          // a primitive root modulo p
    std::uint64_t log2_order; // 2-adicity of p-1: the maximum log2 transform length

    // -p^-1 mod 2^64 via Newton's iteration, which doubles the number of
    // correct low bits each step (p must be odd; p itself is the 3-bit seed).
    [[nodiscard]] static constexpr std::uint64_t make_n_prime(const std::uint64_t prime) noexcept {
        std::uint64_t inv = prime;             // correct modulo 2^3 for odd prime
        inv *= std::uint64_t{2} - prime * inv; // 2^6
        inv *= std::uint64_t{2} - prime * inv; // 2^12
        inv *= std::uint64_t{2} - prime * inv; // 2^24
        inv *= std::uint64_t{2} - prime * inv; // 2^48
        inv *= std::uint64_t{2} - prime * inv; // >= 2^64, so exact mod 2^64
        return std::uint64_t{0} - inv;
    }

    // 2^128 mod p by 128 modular doublings; needs no division, and p < 2^63
    // keeps the intermediate r << 1 within 64 bits.
    [[nodiscard]] static constexpr std::uint64_t make_r_squared(const std::uint64_t prime) noexcept {
        std::uint64_t r = 1;
        for (int i = 0; i < 128; ++i) {
            r <<= 1;
            if (r >= prime) {
                r -= prime;
            }
        }
        return r;
    }

    [[nodiscard]] static constexpr ntt_modulus
    make(const std::uint64_t prime, const std::uint64_t prim_root, const std::uint64_t adicity) noexcept {
        return ntt_modulus{prime, make_n_prime(prime), make_r_squared(prime), prim_root, adicity};
    }

    // Montgomery reduction of the 128-bit product t into [0, p): returns
    // t * 2^-64 mod p. Used as the kernel of every modular multiply.
    [[nodiscard]] constexpr std::uint64_t reduce(const wide<std::uint64_t> t) const noexcept {
        const std::uint64_t       m        = t.low_bits * n_prime; // chosen so the low 64 bits cancel
        const wide<std::uint64_t> mp       = widening_mul(m, p);
        const auto                low_sum  = carrying_add(t.low_bits, mp.low_bits); // low_sum.value == 0
        const auto                high_sum = carrying_add(t.high_bits, mp.high_bits, low_sum.carry);
        const std::uint64_t       res      = high_sum.value; // < 2p; high carry is provably zero
        return res >= p ? res - p : res;
    }

    [[nodiscard]] constexpr std::uint64_t mont_mul(const std::uint64_t a, const std::uint64_t b) const noexcept {
        return reduce(widening_mul(a, b));
    }

    // Ordinary residue -> Montgomery form (a * 2^64 mod p) and back.
    [[nodiscard]] constexpr std::uint64_t to_mont(const std::uint64_t a) const noexcept {
        return mont_mul(a, r_squared);
    }
    [[nodiscard]] constexpr std::uint64_t from_mont(const std::uint64_t a) const noexcept {
        return reduce(wide<std::uint64_t>{a, 0});
    }

    [[nodiscard]] constexpr std::uint64_t add(const std::uint64_t a, const std::uint64_t b) const noexcept {
        const std::uint64_t s = a + b; // < 2p < 2^63
        return s >= p ? s - p : s;
    }
    [[nodiscard]] constexpr std::uint64_t sub(const std::uint64_t a, const std::uint64_t b) const noexcept {
        return a >= b ? a - b : a + (p - b);
    }

    // Ordinary (non-Montgomery) product a*b mod p, for setup and reconstruction
    // paths: mont_mul(to_mont(a), b) == a*b*2^64*2^-64 == a*b mod p.
    [[nodiscard]] constexpr std::uint64_t mul(const std::uint64_t a, const std::uint64_t b) const noexcept {
        return mont_mul(to_mont(a), b);
    }

    // a^e mod p (ordinary form in and out) via Montgomery square-and-multiply.
    [[nodiscard]] constexpr std::uint64_t pow(const std::uint64_t a, std::uint64_t e) const noexcept {
        std::uint64_t base   = to_mont(a);
        std::uint64_t result = to_mont(std::uint64_t{1});
        while (e != 0) {
            if ((e & 1u) != 0u) {
                result = mont_mul(result, base);
            }
            base = mont_mul(base, base);
            e >>= 1;
        }
        return from_mont(result);
    }

    // Modular inverse via Fermat: a^(p-2) mod p (p is prime).
    [[nodiscard]] constexpr std::uint64_t inv(const std::uint64_t a) const noexcept { return pow(a, p - 2); }

    // A primitive 2^j-th root of unity (ordinary form), valid for j <= log2_order.
    [[nodiscard]] constexpr std::uint64_t root(const std::uint64_t j) const noexcept { return pow(g, (p - 1) >> j); }
};

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_MOD_ARITH_HPP
