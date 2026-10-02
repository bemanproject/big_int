// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>

// AArch64 NEON FP NTT kernels (width 2). NEON is mandatory baseline on AArch64,
// so this is compiled (by CMake) only on aarch64 and selected unconditionally
// there. The guard keeps the TU empty if it is ever compiled elsewhere.
#if BEMAN_BIG_INT_NTT_FP_ARM64

    #include <beman/big_int/detail/simd/vec_neon.hpp>

    #include <cstddef>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

void ntt_fp_forward_neon(double* const         data,
                         const std::size_t     n,
                         const double* const   tw,
                         const ntt_fp_modulus& m) noexcept {
    ntt_fp_forward_impl<vec2d>(data, n, tw, m);
}

void ntt_fp_inverse_neon(double* const         data,
                         const std::size_t     n,
                         const double* const   tw,
                         const ntt_fp_modulus& m) noexcept {
    ntt_fp_inverse_impl<vec2d>(data, n, tw, m);
}

void ntt_fp_pointwise_neon(double* const         a,
                           const double* const   b,
                           const std::size_t     n,
                           const ntt_fp_modulus& m) noexcept {
    ntt_fp_pointwise_impl<vec2d>(a, b, n, m);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_NTT_FP_ARM64
