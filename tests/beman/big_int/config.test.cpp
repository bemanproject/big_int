// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Built through CMake, so detail/config.hpp must have picked up the generated
// config_generated.hpp, and the headers must see the options CMake resolved
// (passed in as BEMAN_BIG_INT_TEST_EXPECTED_* by CMakeLists.txt).

#include <beman/big_int.hpp>

#include <gtest/gtest.h>

#include <string_view>

#ifndef BEMAN_BIG_INT_CONFIG_GENERATED
    #error "detail/config_generated.hpp is not on the include path of a CMake build"
#endif

#define BEMAN_BIG_INT_TEST_STRINGIZE_IMPL(...) #__VA_ARGS__
#define BEMAN_BIG_INT_TEST_STRINGIZE(...) BEMAN_BIG_INT_TEST_STRINGIZE_IMPL(__VA_ARGS__)

namespace {

TEST(Config, Namespace) {
    EXPECT_EQ(std::string_view{BEMAN_BIG_INT_TEST_STRINGIZE(BEMAN_BIG_INT_NAMESPACE)},
              std::string_view{BEMAN_BIG_INT_TEST_EXPECTED_NAMESPACE});
}

TEST(Config, X86KernelChoices) {
    // The choices only apply where the x86-64 kernels are called at all.
#ifdef BEMAN_BIG_INT_ARCH_X86_64
    constexpr int bmi2_adx    = BEMAN_BIG_INT_TEST_EXPECTED_BMI2_ADX;
    constexpr int avx512_ifma = BEMAN_BIG_INT_TEST_EXPECTED_AVX512_IFMA;
#else
    constexpr int bmi2_adx    = 0;
    constexpr int avx512_ifma = 0;
#endif
    EXPECT_EQ(BEMAN_BIG_INT_X86_64_BMI2_ADX, bmi2_adx);
    EXPECT_EQ(BEMAN_BIG_INT_X86_64_AVX512_IFMA, avx512_ifma);
}

TEST(Config, SimdMul) {
#ifdef BEMAN_BIG_INT_SIMD_MUL
    constexpr bool simd_mul = true;
#else
    constexpr bool simd_mul = false;
#endif
    EXPECT_EQ(simd_mul, BEMAN_BIG_INT_TEST_EXPECTED_SIMD_MUL != 0);
}

} // namespace
