// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Runtime detection of BMI2 + ADX support, used to GTEST_SKIP() the
// beman_big_int_*_bmi2_adx kernel tests when the running CPU lacks either.

#ifndef BEMAN_BIG_INT_TESTS_X86_64_BMI2_ADX_SUPPORT_HPP
#define BEMAN_BIG_INT_TESTS_X86_64_BMI2_ADX_SUPPORT_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/multiply_long_runtime.hpp>
#include <beman/big_int/detail/square_long_runtime.hpp>

#if defined(BEMAN_BIG_INT_ARCH_X86_64)

    #if defined(BEMAN_BIG_INT_MSVC)
        #include <intrin.h>
        #include <windows.h>
    #else
        #include <cpuid.h>
    #endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace tests {

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

    #if defined(BEMAN_BIG_INT_MSVC)
// Windows-on-ARM's x64 emulator has been observed to report BMI2+ADX via
// CPUID without actually executing mulx/adcx/adox: they raise SEH exception
// 0xC000001D (illegal instruction) instead. This runs the real kernels once
// on a tiny input to check, inside SEH. No C++ objects needing unwinding may
// live in a function that uses __try/__except, so this stays a small
// standalone function operating only on local arrays of plain integers.
inline bool bmi2_adx_kernels_actually_run() noexcept {
    __try {
        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t a[2]       = {1, 2};
        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t b[2]       = {3, 4};
        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t mul_out[4] = {0, 0, 0, 0};
        ::beman_big_int_multiply_long_runtime_bmi2_adx(mul_out, a, 2, b, 2);

        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t sq_out[4] = {0, 0, 0, 0};
        ::beman_big_int_square_long_runtime_bmi2_adx(sq_out, a, 2);
        return true;
    } __except (GetExceptionCode() == static_cast<DWORD>(EXCEPTION_ILLEGAL_INSTRUCTION) ? EXCEPTION_EXECUTE_HANDLER
                                                                                        : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
    #endif // defined(BEMAN_BIG_INT_MSVC)

// True when it is safe to actually call the bmi2_adx kernels: CPUID reports
// both features, and -- on MSVC, where Windows-on-ARM x64 emulation is known
// to lie about this -- a one-time probe of the real kernels also ran
// without faulting. The probe result is cached (it never changes at runtime).
inline bool bmi2_adx_kernels_are_usable() noexcept {
    if (!cpu_has_bmi2_and_adx()) {
        return false;
    }
    #if defined(BEMAN_BIG_INT_MSVC)
    static const bool probed_ok = bmi2_adx_kernels_actually_run();
    return probed_ok;
    #else
    return true;
    #endif
}

} // namespace tests
BEMAN_BIG_INT_END_NAMESPACE

#else

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace tests {

// Non-x86-64 builds never select the bmi2_adx kernel, so callers can check
// this unconditionally instead of wrapping every call site in an #if.
inline bool cpu_has_bmi2_and_adx() noexcept { return false; }
inline bool bmi2_adx_kernels_are_usable() noexcept { return false; }

} // namespace tests
BEMAN_BIG_INT_END_NAMESPACE

#endif // defined(BEMAN_BIG_INT_ARCH_X86_64)

#endif // BEMAN_BIG_INT_TESTS_X86_64_BMI2_ADX_SUPPORT_HPP
