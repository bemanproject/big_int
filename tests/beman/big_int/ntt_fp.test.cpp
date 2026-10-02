// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

// The FP NTT is only built when BEMAN_BIG_INT_SIMD_MUL is defined; with it off the
// default integer NTT is used and these tests compile to an empty translation unit.
#if defined(BEMAN_BIG_INT_SIMD_MUL)

namespace {

using ::BEMAN_BIG_INT_NAMESPACE::detail::fp_center;
using ::BEMAN_BIG_INT_NAMESPACE::detail::fp_mulmod;
using ::BEMAN_BIG_INT_NAMESPACE::detail::fp_reduce_to_0n;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_direction;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_build_twiddles;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_forward;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_inverse;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_pointwise;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_primes;
using ::boost::multiprecision::cpp_int;

// Bit width of nn^2, computed exactly via cpp_int (portable; no 128-bit type).
[[nodiscard]] int n2bits_of(const std::uint64_t nn) {
    const cpp_int sq = cpp_int(nn) * nn;
    return sq == 0 ? 0 : static_cast<int>(boost::multiprecision::msb(sq)) + 1;
}

// Faithful port of FLINT's fft_small_mulmod_satisfies_bounds: a prime < 2^50 is
// only usable in the FP transform if this exact-bound predicate also holds (the
// rounding of 1/n must be benign). Guards the chosen prime set.
[[nodiscard]] bool satisfies_bounds(const std::uint64_t nn) {
    constexpr int d_bits = 53;
    const double  n      = static_cast<double>(nn);
    const double  ninv   = 1.0 / n;
    const double  t1     = std::fabs(std::fma(n, ninv, -1.0));
    const int     n1bits = std::bit_width(nn);
    const int     n2bits = n2bits_of(nn);
    int           b      = d_bits - n1bits - 1;
    if (b < 2) {
        return false;
    }
    const double limit2 = 2 * n * t1 + std::ldexp(ninv, 1 + n2bits - d_bits - 1) + 0.5 + std::ldexp(1.0, -(b + 1));
    --b;
    const double limit4 = 4 * n * t1 + std::ldexp(ninv, 2 + n2bits - d_bits - 1) + 0.5 + std::ldexp(1.0, -(b + 1));
    return limit2 < 0.99 && limit4 < 1.49;
}

TEST(NttFp, PrimesValid) {
    constexpr std::uint64_t two_pow_50 = std::uint64_t{1} << 50;
    for (const auto& m : ntt_fp_primes) {
        const std::uint64_t p = m.mod.p;
        EXPECT_GT(p, std::uint64_t{1} << 49) << "prime should be a 50-bit prime";
        EXPECT_LT(p, two_pow_50) << "prime must be < 2^50 for FP exactness";
        EXPECT_TRUE(satisfies_bounds(p)) << "prime fails FLINT mulmod bound: " << p;
        EXPECT_EQ(m.mod.pow(m.mod.g, p - 1), std::uint64_t{1}) << "g is not a unit / p not prime";
        EXPECT_EQ((p - 1) % (std::uint64_t{1} << m.mod.log2_order), std::uint64_t{0}) << "2-adicity wrong";
    }
}

// The exact FP modular multiply over the operating range used by the butterflies:
// |x| up to 2p (the u-v leg), |y| up to p/2 (a centered twiddle), so |x*y| < 2p^2.
TEST(NttFp, MulmodExactness) {
    std::mt19937_64 rng{0x123u};
    for (const auto& m : ntt_fp_primes) {
        const std::uint64_t                          p = m.mod.p;
        std::uniform_int_distribution<std::uint64_t> dx(0, 4 * p);
        std::uniform_int_distribution<std::uint64_t> dy(0, p);
        for (int iter = 0; iter < 50000; ++iter) {
            const std::int64_t ix = static_cast<std::int64_t>(dx(rng)) - static_cast<std::int64_t>(2 * p);
            const std::int64_t iy = static_cast<std::int64_t>(dy(rng)) - static_cast<std::int64_t>(p / 2);

            const double got_d =
                fp_reduce_to_0n(fp_mulmod(static_cast<double>(ix), static_cast<double>(iy), m.n, m.ninv), m.n, m.ninv);
            const std::uint64_t got = static_cast<std::uint64_t>(got_d);

            const cpp_int       prod = cpp_int(ix) * iy;
            const std::uint64_t ref  = static_cast<std::uint64_t>(((prod % p) + p) % p);
            ASSERT_EQ(got, ref) << "p=" << p << " ix=" << ix << " iy=" << iy;
        }
    }
}

// forward then inverse is the identity (the 1/n scaling cancels the transform),
// the sharpest single check on twiddle direction and the butterfly pairing.
TEST(NttFp, RoundTrip) {
    std::mt19937_64 rng{0x55u};
    for (const auto& m : ntt_fp_primes) {
        const std::uint64_t                          p = m.mod.p;
        std::uniform_int_distribution<std::uint64_t> dist(0, p - 1);
        for (std::size_t logn = 0; logn <= 15; ++logn) {
            const std::size_t          n = std::size_t{1} << logn;
            std::vector<std::uint64_t> orig(n);
            std::vector<double>        data(n);
            for (std::size_t i = 0; i < n; ++i) {
                orig[i] = dist(rng);
                data[i] = fp_center(orig[i], p);
            }
            std::vector<double> tw(n); // per-level table has n-1 entries
            ntt_fp_build_twiddles(tw, n, m, ntt_direction::forward);
            ntt_fp_forward(data, tw, m);
            ntt_fp_build_twiddles(tw, n, m, ntt_direction::inverse);
            ntt_fp_inverse(data, tw, m);
            for (std::size_t i = 0; i < n; ++i) {
                ASSERT_EQ(static_cast<std::uint64_t>(fp_reduce_to_0n(data[i], m.n, m.ninv)), orig[i])
                    << "p=" << p << " n=" << n << " i=" << i;
            }
        }
    }
}

// FP cyclic convolution must equal the O(n^2) cpp_int reference. Larger transforms
// are covered by RoundTrip (above) and the full multiply differential in
// fft_mul.test.cpp.
TEST(NttFp, ConvolutionMatchesReference) {
    std::mt19937_64 rng{0x99u};
    for (const auto& m : ntt_fp_primes) {
        const std::uint64_t                          p = m.mod.p;
        std::uniform_int_distribution<std::uint64_t> dist(0, p - 1);
        for (std::size_t logn = 1; logn <= 8; ++logn) {
            const std::size_t          n = std::size_t{1} << logn;
            std::vector<std::uint64_t> a(n);
            std::vector<std::uint64_t> b(n);
            for (auto& x : a) {
                x = dist(rng);
            }
            for (auto& x : b) {
                x = dist(rng);
            }

            // FP convolution.
            std::vector<double> fa(n);
            std::vector<double> fb(n);
            std::vector<double> ftw(n); // per-level table has n-1 entries
            for (std::size_t i = 0; i < n; ++i) {
                fa[i] = fp_center(a[i], p);
                fb[i] = fp_center(b[i], p);
            }
            ntt_fp_build_twiddles(ftw, n, m, ntt_direction::forward);
            ntt_fp_forward(fa, ftw, m);
            ntt_fp_forward(fb, ftw, m);
            ntt_fp_pointwise(fa, fb, m);
            ntt_fp_build_twiddles(ftw, n, m, ntt_direction::inverse);
            ntt_fp_inverse(fa, ftw, m);

            for (std::size_t i = 0; i < n; ++i) {
                cpp_int acc = 0;
                for (std::size_t j = 0; j < n; ++j) {
                    acc += cpp_int(a[j]) * b[(i + n - j) % n];
                }
                const std::uint64_t ref = static_cast<std::uint64_t>(acc % p);
                const std::uint64_t fpv = static_cast<std::uint64_t>(fp_reduce_to_0n(fa[i], m.n, m.ninv));
                ASSERT_EQ(fpv, ref) << "FP conv p=" << p << " n=" << n << " i=" << i;
            }
        }
    }
}

} // namespace

#endif // BEMAN_BIG_INT_SIMD_MUL
