// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mod_arith.hpp>
#include <beman/big_int/detail/ntt.hpp>

#include "boost_mp_testing.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

// The integer NTT transform is only built in the default (non-SIMD) configuration;
// when BEMAN_BIG_INT_SIMD_MUL is on, the FP NTT replaces it and this is an empty TU.
#if !defined(BEMAN_BIG_INT_SIMD_MUL)

namespace {

using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_build_twiddles;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_direction;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_forward;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_inverse;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_modulus;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_pointwise;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_primes;
using ::boost::multiprecision::cpp_int;

// Build the forward or inverse twiddle table of size n/2 for modulus m.
[[nodiscard]] std::vector<std::uint64_t>
twiddles_for(const std::size_t n, const ntt_modulus& m, const ntt_direction direction) {
    std::vector<std::uint64_t> table(n / 2);
    ntt_build_twiddles(table, n, m, direction);
    return table;
}

[[nodiscard]] std::vector<std::uint64_t>
random_vector(std::mt19937_64& rng, const std::size_t n, const std::uint64_t max_exclusive) {
    std::uniform_int_distribution<std::uint64_t> dist(0, max_exclusive - 1);
    std::vector<std::uint64_t>                   v(n);
    for (auto& x : v) {
        x = dist(rng);
    }
    return v;
}

// --------------------------------------------------------------------------
// inverse(forward(x)) == x for every prime and a range of transform lengths.
// This is the sharpest single check: it catches twiddle-direction, bit-reversal
// pairing, and 1/N-scaling mistakes at once.
// --------------------------------------------------------------------------
TEST(Ntt, RoundTripPerPrime) {
    std::mt19937_64 rng{0x177013u};
    for (const auto& m : ntt_primes) {
        for (std::size_t logn = 0; logn <= 13; ++logn) {
            const std::size_t                n    = std::size_t{1} << logn;
            const std::vector<std::uint64_t> orig = random_vector(rng, n, m.p);
            std::vector<std::uint64_t>       data = orig;
            const auto                       fwd  = twiddles_for(n, m, ntt_direction::forward);
            const auto                       inv  = twiddles_for(n, m, ntt_direction::inverse);
            ntt_forward(data, fwd, m);
            ntt_inverse(data, inv, m);
            ASSERT_EQ(data, orig) << "p=" << m.p << " n=" << n;
        }
    }
}

// --------------------------------------------------------------------------
// NTT-based cyclic convolution matches a direct O(n^2) reference (mod p),
// computed independently with cpp_int.
// --------------------------------------------------------------------------
TEST(Ntt, CyclicConvolutionSinglePrime) {
    std::mt19937_64 rng{0xc04fefu};
    for (const auto& m : ntt_primes) {
        for (std::size_t logn = 0; logn <= 8; ++logn) {
            const std::size_t                n = std::size_t{1} << logn;
            const std::vector<std::uint64_t> a = random_vector(rng, n, m.p);
            const std::vector<std::uint64_t> b = random_vector(rng, n, m.p);

            std::vector<std::uint64_t> reference(n);
            for (std::size_t k = 0; k < n; ++k) {
                cpp_int acc = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    acc += cpp_int(a[i]) * b[(k + n - i) % n];
                }
                reference[k] = static_cast<std::uint64_t>(acc % m.p);
            }

            std::vector<std::uint64_t> fa  = a;
            std::vector<std::uint64_t> fb  = b;
            const auto                 fwd = twiddles_for(n, m, ntt_direction::forward);
            const auto                 inv = twiddles_for(n, m, ntt_direction::inverse);
            ntt_forward(fa, fwd, m);
            ntt_forward(fb, fwd, m);
            ntt_pointwise(fa, fb, m);
            ntt_inverse(fa, inv, m);
            ASSERT_EQ(fa, reference) << "p=" << m.p << " n=" << n;
        }
    }
}

// --------------------------------------------------------------------------
// The headline foundation check: three-prime NTT convolution + CRT reconstructs
// the *exact* integer coefficients of a polynomial product, even when the
// coefficients overflow a single 62-bit prime. This is exactly what the
// (deferred) FFT multiplication will lean on -- only operand packing and carry
// propagation remain.
// --------------------------------------------------------------------------
TEST(Ntt, MultiPrimeCrtExactProduct) {
    std::mt19937_64 rng{0xc27e57u};

    constexpr std::size_t   na          = 200;
    constexpr std::size_t   nb          = 200;
    constexpr std::uint64_t coeff_bound = std::uint64_t{1} << 30; // coefficients in [0, 2^30)

    const std::vector<std::uint64_t> a = random_vector(rng, na, coeff_bound);
    const std::vector<std::uint64_t> b = random_vector(rng, nb, coeff_bound);

    // Exact linear convolution with cpp_int.
    const std::size_t    result_len = na + nb - 1;
    std::vector<cpp_int> exact(result_len, cpp_int{0});
    cpp_int              max_coefficient = 0;
    for (std::size_t i = 0; i < na; ++i) {
        for (std::size_t j = 0; j < nb; ++j) {
            exact[i + j] += cpp_int(a[i]) * b[j];
        }
    }
    for (const auto& c : exact) {
        if (c > max_coefficient) {
            max_coefficient = c;
        }
    }
    // Confirm the test is meaningful: at least one coefficient exceeds a single prime.
    EXPECT_GT(max_coefficient, cpp_int(ntt_primes[0].p));

    const std::size_t n = std::bit_ceil(result_len);

    // Per-prime NTT convolution; residues[t][k] == exact[k] mod ntt_primes[t].p.
    std::array<std::vector<std::uint64_t>, 3> residues;
    for (std::size_t t = 0; t < 3; ++t) {
        const ntt_modulus&         m = ntt_primes[t];
        std::vector<std::uint64_t> va(n, 0);
        std::vector<std::uint64_t> vb(n, 0);
        for (std::size_t i = 0; i < na; ++i) {
            va[i] = a[i];
        }
        for (std::size_t i = 0; i < nb; ++i) {
            vb[i] = b[i];
        }
        const auto fwd = twiddles_for(n, m, ntt_direction::forward);
        const auto inv = twiddles_for(n, m, ntt_direction::inverse);
        ntt_forward(va, fwd, m);
        ntt_forward(vb, fwd, m);
        ntt_pointwise(va, vb, m);
        ntt_inverse(va, inv, m);
        residues[t] = std::move(va);
    }

    // Garner CRT constants for the three moduli.
    const std::uint64_t p0              = ntt_primes[0].p;
    const std::uint64_t p1              = ntt_primes[1].p;
    const std::uint64_t p2              = ntt_primes[2].p;
    const std::uint64_t inv_p0_mod_p1   = ntt_primes[1].inv(p0 % p1);
    const cpp_int       p0p1            = cpp_int(p0) * p1;
    const std::uint64_t inv_p0p1_mod_p2 = ntt_primes[2].inv(static_cast<std::uint64_t>(p0p1 % p2));

    for (std::size_t k = 0; k < result_len; ++k) {
        const std::uint64_t r0 = residues[0][k];
        const std::uint64_t r1 = residues[1][k];
        const std::uint64_t r2 = residues[2][k];

        const std::uint64_t c1 = ntt_primes[1].mul(ntt_primes[1].sub(r1, r0), inv_p0_mod_p1);
        cpp_int             x  = cpp_int(r0) + cpp_int(p0) * c1;
        const std::uint64_t c2 =
            ntt_primes[2].mul(ntt_primes[2].sub(r2, static_cast<std::uint64_t>(x % p2)), inv_p0p1_mod_p2);
        x += p0p1 * c2;

        ASSERT_EQ(x, exact[k]) << "k=" << k;
    }

    // Zero-padded tail must convolve to zero in every prime.
    for (std::size_t k = result_len; k < n; ++k) {
        for (std::size_t t = 0; t < 3; ++t) {
            ASSERT_EQ(residues[t][k], std::uint64_t{0}) << "t=" << t << " k=" << k;
        }
    }
}

} // namespace

#endif // !BEMAN_BIG_INT_SIMD_MUL
