// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

// The SIMD safety net: the kernel the dispatcher selects on this CPU (NEON on
// AArch64, AVX2 on x86-64 with AVX2+FMA, else scalar) must produce results
// BIT-IDENTICAL to the scalar kernel. The FP ops are all correctly rounded, so a
// matching operation sequence is bit-exact lane for lane. Run under the Docker
// images this also confirms the AVX2 path on x86 and the NEON path on ARM64.
// Only built when the FP/SIMD path is enabled; otherwise an empty TU.
#if defined(BEMAN_BIG_INT_SIMD_MUL)

namespace {

using ::BEMAN_BIG_INT_NAMESPACE::detail::fp_center;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_direction;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_build_twiddles;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_dispatch;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_forward_scalar;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_inverse_scalar;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_pointwise_scalar;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_fp_primes;

[[nodiscard]] bool bit_equal(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size()) {
        return false;
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (std::bit_cast<std::uint64_t>(x[i]) != std::bit_cast<std::uint64_t>(y[i])) {
            return false;
        }
    }
    return true;
}

TEST(NttFpSimd, DispatchedKernelMatchesScalarBitwise) {
    const auto&     k = ntt_fp_dispatch();
    std::mt19937_64 rng{0x5114u};
    for (const auto& m : ntt_fp_primes) {
        const std::uint64_t                          p = m.mod.p;
        std::uniform_int_distribution<std::uint64_t> dist(0, p - 1);
        for (std::size_t logn = 1; logn <= 14; ++logn) {
            const std::size_t   n = std::size_t{1} << logn;
            std::vector<double> base(n);
            for (auto& x : base) {
                x = fp_center(dist(rng), p);
            }
            std::vector<double> tw_fwd(n);
            std::vector<double> tw_inv(n);
            ntt_fp_build_twiddles(tw_fwd, n, m, ntt_direction::forward);
            ntt_fp_build_twiddles(tw_inv, n, m, ntt_direction::inverse);

            {
                std::vector<double> s(base);
                std::vector<double> d(base);
                ntt_fp_forward_scalar(s.data(), n, tw_fwd.data(), m);
                k.forward(d.data(), n, tw_fwd.data(), m);
                ASSERT_TRUE(bit_equal(s, d)) << "forward p=" << p << " n=" << n;
            }
            {
                std::vector<double> s(base);
                std::vector<double> d(base);
                ntt_fp_inverse_scalar(s.data(), n, tw_inv.data(), m);
                k.inverse(d.data(), n, tw_inv.data(), m);
                ASSERT_TRUE(bit_equal(s, d)) << "inverse p=" << p << " n=" << n;
            }
            {
                std::vector<double> b2(n);
                for (auto& x : b2) {
                    x = fp_center(dist(rng), p);
                }
                std::vector<double> s(base);
                std::vector<double> d(base);
                ntt_fp_pointwise_scalar(s.data(), b2.data(), n, m);
                k.pointwise(d.data(), b2.data(), n, m);
                ASSERT_TRUE(bit_equal(s, d)) << "pointwise p=" << p << " n=" << n;
            }
        }
    }
}

} // namespace

#endif // BEMAN_BIG_INT_SIMD_MUL
