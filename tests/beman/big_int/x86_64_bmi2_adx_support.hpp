// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Runtime detection of BMI2 + ADX support, used to GTEST_SKIP() the
// beman_big_int_*_bmi2_adx kernel tests when the running CPU lacks either.

#ifndef BEMAN_BIG_INT_TESTS_X86_64_BMI2_ADX_SUPPORT_HPP
#define BEMAN_BIG_INT_TESTS_X86_64_BMI2_ADX_SUPPORT_HPP

#include <beman/big_int/detail/config.hpp>

#if defined(BEMAN_BIG_INT_ARCH_X86_64)

    #if defined(BEMAN_BIG_INT_MSVC)
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif

namespace beman::big_int::tests {

// CPUID leaf 7, subleaf 0: EBX bit 8 is BMI2, bit 19 is ADX.
inline bool cpu_has_bmi2_and_adx() noexcept {
    constexpr unsigned int bmi2_bit = 1U << 8;
    constexpr unsigned int adx_bit  = 1U << 19;

    #if defined(BEMAN_BIG_INT_MSVC)
    int leaf0[4] = {0, 0, 0, 0};
    __cpuidex(leaf0, 0, 0);
    if (leaf0[0] < 7) {
        return false;
    }
    int leaf7[4] = {0, 0, 0, 0};
    __cpuidex(leaf7, 7, 0);
    const unsigned int ebx = static_cast<unsigned int>(leaf7[1]);
    #else
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) == 0) {
        return false;
    }
    #endif

    return (ebx & bmi2_bit) != 0 && (ebx & adx_bit) != 0;
}

} // namespace beman::big_int::tests

#else

namespace beman::big_int::tests {

// Non-x86-64 builds never select the bmi2_adx kernel, so callers can check
// this unconditionally instead of wrapping every call site in an #if.
inline bool cpu_has_bmi2_and_adx() noexcept { return false; }

} // namespace beman::big_int::tests

#endif // defined(BEMAN_BIG_INT_ARCH_X86_64)

#endif // BEMAN_BIG_INT_TESTS_X86_64_BMI2_ADX_SUPPORT_HPP
