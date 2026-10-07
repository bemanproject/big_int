// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>

// Runtime kernel selection. This TU is compiled at the baseline ISA (no -mavx*),
// so it is always safe to run; it inspects the CPU once and caches the chosen
// kernel set. On AArch64 NEON is baseline (no check). On x86-64 it picks the AVX2
// kernels only after confirming AVX2+FMA (and, on MSVC, that the OS enabled YMM
// state), else the scalar kernels -- so AVX instructions never run on a CPU that
// lacks them. (AVX-512 was evaluated and dropped: it was no faster than AVX2 for
// this bandwidth-bound transform and is absent on many current CPUs.) Everything
// else uses the scalar kernels.

#include <cstdlib>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

namespace {

// Setting BEMAN_BIG_INT_NTT_FP_FORCE_SCALAR (to a non-empty, non-"0" value) pins
// the scalar kernels regardless of CPU features -- for A/B benchmarking the SIMD
// speedup and for exercising the scalar fallback. Read once (the result is cached).
[[nodiscard, maybe_unused]] bool force_scalar_requested() noexcept {
    const char* const e = std::getenv("BEMAN_BIG_INT_NTT_FP_FORCE_SCALAR");
    return e != nullptr && e[0] != '\0' && e[0] != '0';
}

#if BEMAN_BIG_INT_NTT_FP_X86
[[nodiscard]] bool cpu_has_avx2_fma() noexcept {
    #if defined(BEMAN_BIG_INT_BUILTIN_CPU_INIT) && defined(BEMAN_BIG_INT_BUILTIN_CPU_SUPPORTS)
    BEMAN_BIG_INT_BUILTIN_CPU_INIT();
    return BEMAN_BIG_INT_BUILTIN_CPU_SUPPORTS("avx2") != 0 && BEMAN_BIG_INT_BUILTIN_CPU_SUPPORTS("fma") != 0;
    #elif defined(BEMAN_BIG_INT_INTRINSIC_CPUID)
    int regs[4];
    BEMAN_BIG_INT_INTRINSIC_CPUID(regs, 1);
    const bool fma     = (regs[2] & (1 << 12)) != 0;
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    BEMAN_BIG_INT_INTRINSIC_CPUIDEX(regs, 7, 0);
    const bool avx2 = (regs[1] & (1 << 5)) != 0;
    bool       ymm  = false;
    if (osxsave) {
        const unsigned long long xcr0 = BEMAN_BIG_INT_INTRINSIC_XGETBV(0);
        ymm                           = (xcr0 & 0x6u) == 0x6u; // XMM + YMM state enabled by the OS
    }
    return fma && avx2 && ymm;
    #else
    return false;
    #endif
}
#endif

[[nodiscard]] ntt_fp_kernels select_kernels() noexcept {
    const ntt_fp_kernels scalar{ntt_fp_forward_scalar, ntt_fp_inverse_scalar, ntt_fp_pointwise_scalar};
#if BEMAN_BIG_INT_NTT_FP_ARM64
    if (force_scalar_requested()) {
        return scalar;
    }
    return ntt_fp_kernels{ntt_fp_forward_neon, ntt_fp_inverse_neon, ntt_fp_pointwise_neon};
#elif BEMAN_BIG_INT_NTT_FP_X86
    if (!force_scalar_requested() && cpu_has_avx2_fma()) {
        return ntt_fp_kernels{ntt_fp_forward_avx2, ntt_fp_inverse_avx2, ntt_fp_pointwise_avx2};
    }
    return scalar;
#else
    return scalar;
#endif
}

} // namespace

const ntt_fp_kernels& ntt_fp_dispatch() noexcept {
    static const ntt_fp_kernels kernels = select_kernels();
    return kernels;
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
