// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Runtime detection of AVX-512 IFMA (+ VL/BW/VBMI/BMI2/ADX) support, used to
// GTEST_SKIP() the beman_big_int_*_avx512_ifma kernel tests when the running
// CPU lacks any of them.

#ifndef BEMAN_BIG_INT_TESTS_X86_64_AVX512_IFMA_SUPPORT_HPP
#define BEMAN_BIG_INT_TESTS_X86_64_AVX512_IFMA_SUPPORT_HPP

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

// XCR0 bits the OS must enable for AVX-512: SSE (1), AVX (2), opmask (5),
// ZMM_Hi256 (6) and Hi16_ZMM (7), i.e. 0xE6.
inline unsigned long long xgetbv0() noexcept {
    #if defined(BEMAN_BIG_INT_MSVC)
    return _xgetbv(0);
    #else
    unsigned int eax = 0;
    unsigned int edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<unsigned long long>(edx) << 32) | eax;
    #endif
}

// CPUID leaf 1 ECX bit 27 (OSXSAVE); leaf 7 subleaf 0 EBX bits 16 (AVX512F),
// 21 (AVX512_IFMA), 30 (AVX512BW), 31 (AVX512VL), 8 (BMI2), 19 (ADX), and ECX
// bit 1 (AVX512_VBMI).
inline bool cpu_has_avx512_ifma() noexcept {
    constexpr unsigned int osxsave_bit    = 1U << 27;
    constexpr unsigned int avx512f_bit    = 1U << 16;
    constexpr unsigned int avx512ifma_bit = 1U << 21;
    constexpr unsigned int avx512bw_bit   = 1U << 30;
    constexpr unsigned int avx512vl_bit   = 1U << 31;
    constexpr unsigned int bmi2_bit       = 1U << 8;
    constexpr unsigned int adx_bit        = 1U << 19;
    constexpr unsigned int avx512vbmi_bit = 1U << 1;

    #if defined(BEMAN_BIG_INT_MSVC)
    int leaf0[4] = {0, 0, 0, 0};
    __cpuidex(leaf0, 0, 0);
    if (leaf0[0] < 7) {
        return false;
    }
    int leaf1[4] = {0, 0, 0, 0};
    __cpuidex(leaf1, 1, 0);
    const unsigned int leaf1_ecx = static_cast<unsigned int>(leaf1[2]);

    int leaf7[4] = {0, 0, 0, 0};
    __cpuidex(leaf7, 7, 0);
    const unsigned int leaf7_ebx = static_cast<unsigned int>(leaf7[1]);
    const unsigned int leaf7_ecx = static_cast<unsigned int>(leaf7[2]);
    #else
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) {
        return false;
    }
    const unsigned int leaf1_ecx = ecx;

    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) == 0) {
        return false;
    }
    const unsigned int leaf7_ebx = ebx;
    const unsigned int leaf7_ecx = ecx;
    #endif

    if ((leaf1_ecx & osxsave_bit) == 0) {
        return false;
    }
    if ((xgetbv0() & 0xE6ULL) != 0xE6ULL) {
        return false;
    }

    return (leaf7_ebx & avx512f_bit) != 0 && (leaf7_ebx & avx512ifma_bit) != 0 && (leaf7_ebx & avx512bw_bit) != 0 &&
           (leaf7_ebx & avx512vl_bit) != 0 && (leaf7_ebx & bmi2_bit) != 0 && (leaf7_ebx & adx_bit) != 0 &&
           (leaf7_ecx & avx512vbmi_bit) != 0;
}

    #if defined(BEMAN_BIG_INT_MSVC)
// Windows-on-ARM's x64 emulator has been observed to report feature bits via
// CPUID without actually executing the corresponding instructions, raising
// SEH exception 0xC000001D (illegal instruction) instead. This runs the real
// kernels once on a tiny input to check, inside SEH. No C++ objects needing
// unwinding may live in a function that uses __try/__except, so this stays a
// small standalone function operating only on local arrays of plain integers.
inline bool avx512_ifma_kernels_actually_run() noexcept {
    __try {
        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t a[2]       = {1, 2};
        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t b[2]       = {3, 4};
        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t mul_out[4] = {0, 0, 0, 0};
        ::beman_big_int_multiply_long_runtime_avx512_ifma(mul_out, a, 2, b, 2);

        BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t sq_out[4] = {0, 0, 0, 0};
        ::beman_big_int_square_long_runtime_avx512_ifma(sq_out, a, 2);
        return true;
    } __except (GetExceptionCode() == static_cast<DWORD>(EXCEPTION_ILLEGAL_INSTRUCTION) ? EXCEPTION_EXECUTE_HANDLER
                                                                                        : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
    #endif // defined(BEMAN_BIG_INT_MSVC)

// True when it is safe to actually call the avx512_ifma kernels: CPUID
// reports the full feature set, and -- on MSVC, where Windows-on-ARM x64
// emulation is known to lie about this -- a one-time probe of the real
// kernels also ran without faulting. The probe result is cached (it never
// changes at runtime).
inline bool avx512_ifma_kernels_are_usable() noexcept {
    if (!cpu_has_avx512_ifma()) {
        return false;
    }
    #if defined(BEMAN_BIG_INT_MSVC)
    static const bool probed_ok = avx512_ifma_kernels_actually_run();
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

// Non-x86-64 builds never select the avx512_ifma kernel, so callers can check
// this unconditionally instead of wrapping every call site in an #if.
inline bool cpu_has_avx512_ifma() noexcept { return false; }
inline bool avx512_ifma_kernels_are_usable() noexcept { return false; }

} // namespace tests
BEMAN_BIG_INT_END_NAMESPACE

#endif // defined(BEMAN_BIG_INT_ARCH_X86_64)

#endif // BEMAN_BIG_INT_TESTS_X86_64_AVX512_IFMA_SUPPORT_HPP
