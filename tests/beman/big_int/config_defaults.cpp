// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// detail/config.hpp's defaults for a build that does not use CMake, checked
// against the compiler's own feature macros. The targets built from this file do
// not link the library, so config_generated.hpp is not on their include path
// (see CMakeLists.txt). BEMAN_BIG_INT_TEST_FORCED_IFMA marks the variant that
// defines BEMAN_BIG_INT_X86_64_AVX512_IFMA=1 itself.

#include <beman/big_int/detail/config.hpp>

#include <gtest/gtest.h>

#ifdef BEMAN_BIG_INT_CONFIG_GENERATED
    #error "detail/config_generated.hpp must not be reachable without CMake"
#endif

// Exact only under floating-point flags that cannot be queried, so never on by default.
#ifdef BEMAN_BIG_INT_SIMD_MUL
    #error "BEMAN_BIG_INT_SIMD_MUL must never be enabled by default"
#endif

namespace {

#if defined(__BMI2__) && defined(__ADX__)
constexpr bool compiler_bmi2_adx = true;
#else
constexpr bool compiler_bmi2_adx = false;
#endif

#if defined(__AVX512IFMA__) && defined(__AVX512VL__) && defined(__AVX512BW__) && defined(__AVX512VBMI__)
constexpr bool compiler_avx512_ifma = true;
#else
constexpr bool compiler_avx512_ifma = false;
#endif

#ifdef BEMAN_BIG_INT_TEST_FORCED_IFMA
constexpr bool forced_ifma = true;
#else
constexpr bool forced_ifma = false;
#endif

#ifdef BEMAN_BIG_INT_ARCH_X86_64
constexpr bool x86_64_kernels = true;
#else
constexpr bool x86_64_kernels = false;
#endif

TEST(ConfigDefaults, Bmi2Adx) {
    // Forcing the IFMA kernels on also forces their small-operand fallback, as CMake does.
    const bool expected = x86_64_kernels && (compiler_bmi2_adx || forced_ifma);
    EXPECT_EQ(BEMAN_BIG_INT_X86_64_BMI2_ADX, expected ? 1 : 0);
}

TEST(ConfigDefaults, Avx512Ifma) {
    const bool expected = x86_64_kernels && (forced_ifma || (compiler_avx512_ifma && compiler_bmi2_adx));
    EXPECT_EQ(BEMAN_BIG_INT_X86_64_AVX512_IFMA, expected ? 1 : 0);
}

} // namespace
