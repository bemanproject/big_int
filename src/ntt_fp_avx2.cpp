// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>

// x86-64 AVX2 + FMA FP NTT kernels (width 4). Compiled (by CMake) only on x86-64
// and ONLY this TU is built with -mavx2 -mfma, so AVX/FMA instructions are
// confined here; the dispatcher selects these only after a runtime AVX2+FMA check,
// so they never execute on a CPU that lacks them. The guard keeps the TU empty if
// it is ever compiled on another architecture.
#if BEMAN_BIG_INT_NTT_FP_X86

    #include <beman/big_int/detail/simd/vec_avx2.hpp>

    #include <cstddef>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

void ntt_fp_forward_avx2(double* const         data,
                         const std::size_t     n,
                         const double* const   tw,
                         const ntt_fp_modulus& m) noexcept {
    ntt_fp_forward_impl<vec4d>(data, n, tw, m);
}

void ntt_fp_inverse_avx2(double* const         data,
                         const std::size_t     n,
                         const double* const   tw,
                         const ntt_fp_modulus& m) noexcept {
    ntt_fp_inverse_impl<vec4d>(data, n, tw, m);
}

void ntt_fp_pointwise_avx2(double* const         a,
                           const double* const   b,
                           const std::size_t     n,
                           const ntt_fp_modulus& m) noexcept {
    ntt_fp_pointwise_impl<vec4d>(a, b, n, m);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_NTT_FP_X86
