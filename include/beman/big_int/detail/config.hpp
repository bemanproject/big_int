// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_HPP
#define BEMAN_BIG_INT_CONFIG_HPP

// Guarding these includes is safe only because the .cppm supplies them in its
// global module fragment before the purview include of this header.
#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cfloat>  // for LDBL_MANT_DIG, LDBL_MAX_EXP
    #include <climits> // for BITINT_MAXWIDTH
    #include <cstdint> // for INTPTR_MAX
    #include <version> // for __cpp_lib_*
#endif

// A CMake build generates config_generated.hpp at configure time
#if __has_include(<beman/big_int/detail/config_generated.hpp>)

    #include <beman/big_int/detail/config_generated.hpp>

#else

    // Feature detection ===========================================================

    // Every feature-test, compiler-extension, and target-architecture macro the
    // library branches on is tested here and nowhere else; the rest of the library
    // checks only the BEMAN_BIG_INT_HAS_*, BEMAN_BIG_INT_TARGET_* and
    // BEMAN_BIG_INT_LONG_DOUBLE_* spellings below, and the BEMAN_BIG_INT_BUILTIN_*
    // and BEMAN_BIG_INT_INTRINSIC_* wrappers defined from them after this branch.
    // Each is defined to 1 when the feature is available and left undefined otherwise.

    // Language features

    #if defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
        #define BEMAN_BIG_INT_HAS_CPP_IF_CONSTEVAL 1
    #endif

    // Library features

    #if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
        #define BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST 1
    #endif

    #if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
        #define BEMAN_BIG_INT_HAS_CPP_LIB_CONSTEXPR_CMATH 1
    #endif

    #if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
        #define BEMAN_BIG_INT_HAS_CPP_LIB_CONTAINERS_RANGES 1
    #endif

    #if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
        #define BEMAN_BIG_INT_HAS_CPP_LIB_FORMAT 1
    #endif

    // 202411L is the revision that makes the uninitialized memory algorithms constexpr.
    #if defined(__cpp_lib_raw_memory_algorithms) && __cpp_lib_raw_memory_algorithms >= 202411L
        #define BEMAN_BIG_INT_HAS_CPP_LIB_RAW_MEMORY_ALGORITHMS 1
    #endif

    #if defined(__cpp_lib_string_resize_and_overwrite) && __cpp_lib_string_resize_and_overwrite >= 202110L
        #define BEMAN_BIG_INT_HAS_CPP_LIB_STRING_RESIZE_AND_OVERWRITE 1
    #endif

    // Extended floating-point types

    #if __has_include(<stdfloat>)
        #define BEMAN_BIG_INT_HAS_STDFLOAT 1
    #endif

    #ifdef __STDCPP_FLOAT16_T__
        #define BEMAN_BIG_INT_HAS_STDCPP_FLOAT16_T 1
    #endif

    #ifdef __STDCPP_BFLOAT16_T__
        #define BEMAN_BIG_INT_HAS_STDCPP_BFLOAT16_T 1
    #endif

    #ifdef __STDCPP_FLOAT128_T__
        #define BEMAN_BIG_INT_HAS_STDCPP_FLOAT128_T 1
    #endif

    // long double format

    #if !defined(LDBL_MANT_DIG) || !defined(LDBL_MAX_EXP)
        #error Cannot determine the format of long double without LDBL_MANT_DIG and LDBL_MAX_EXP.
    #elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
        #define BEMAN_BIG_INT_LONG_DOUBLE_X87_EXTENDED 1
    #elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
        #define BEMAN_BIG_INT_LONG_DOUBLE_BINARY128 1
    #elif LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
        #define BEMAN_BIG_INT_LONG_DOUBLE_BINARY64 1
    #endif

    // Bit-precise integers (_BitInt)

    #ifdef BITINT_MAXWIDTH
        // Once _BitInt is a standard feature and available on all compilers,
        // this case should be selected for all compilers.
        #define BEMAN_BIG_INT_HAS_BITINT 1
        #define BEMAN_BIG_INT_BITINT_MAXWIDTH BITINT_MAXWIDTH
    #elif defined(__BITINT_MAXWIDTH__)
        // This case is for Clang when it provides _BitInt as an extension.
        #define BEMAN_BIG_INT_HAS_BITINT 1
        #define BEMAN_BIG_INT_HAS_BITINT_EXTENSION 1
        #define BEMAN_BIG_INT_BITINT_MAXWIDTH __BITINT_MAXWIDTH__
    #else
        // Prevent warnings for use of undefined macros.
        #define BEMAN_BIG_INT_BITINT_MAXWIDTH 0
    #endif // BITINT_MAXWIDTH

    // Workaround for Clang-19 ICE past 128 bits, even though it reports far more than that
    // Crashes in EmitAutoVarInit; fixed in Clang 20 by https://github.com/llvm/llvm-project/pull/112218
    #if defined(__clang__) && __clang_major__ == 19 && BEMAN_BIG_INT_BITINT_MAXWIDTH > 128
        #undef BEMAN_BIG_INT_BITINT_MAXWIDTH
        #define BEMAN_BIG_INT_BITINT_MAXWIDTH 128
    #endif

    // GNU __int128

    #ifdef __SIZEOF_INT128__
        #define BEMAN_BIG_INT_HAS_INT128_EXTENSION 1
    #endif

    // Compiler builtins

    // BEMAN_BIG_INT_HAS_BUILTIN_<NAME> means __builtin_<name> (or the type trait
    // __<name>) exists; BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_<NAME> means it is also
    // usable during constant evaluation. Only Clang can test the latter exactly, so
    // on GCC the floating-point builtins, which it folds, count whenever they exist.
    // The library calls them through the BEMAN_BIG_INT_BUILTIN_* wrappers defined
    // from these below. The DETECT helpers are function-like and #undef'd below, so
    // the CMake probe never records them.

    #ifdef __has_builtin
        #define BEMAN_BIG_INT_DETECT_BUILTIN(...) __has_builtin(__VA_ARGS__)
    #else
        #define BEMAN_BIG_INT_DETECT_BUILTIN(...) 0
    #endif

    #ifdef __has_constexpr_builtin
        #define BEMAN_BIG_INT_DETECT_CONSTEXPR_BUILTIN(...) __has_constexpr_builtin(__VA_ARGS__)
        #define BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(...) __has_constexpr_builtin(__VA_ARGS__)
    #else
        #define BEMAN_BIG_INT_DETECT_CONSTEXPR_BUILTIN(...) 0
        #define BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(...) BEMAN_BIG_INT_DETECT_BUILTIN(__VA_ARGS__)
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__is_integral)
        #define BEMAN_BIG_INT_HAS_BUILTIN_IS_INTEGRAL 1
    #endif

    // MSVC provides this one without __has_builtin.
    #if defined(_MSC_VER) || BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_is_constant_evaluated)
        #define BEMAN_BIG_INT_HAS_BUILTIN_IS_CONSTANT_EVALUATED 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_constant_p)
        #define BEMAN_BIG_INT_HAS_BUILTIN_CONSTANT_P 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_trap)
        #define BEMAN_BIG_INT_HAS_BUILTIN_TRAP 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_add_overflow)
        #define BEMAN_BIG_INT_HAS_BUILTIN_ADD_OVERFLOW 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_sub_overflow)
        #define BEMAN_BIG_INT_HAS_BUILTIN_SUB_OVERFLOW 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_mul_overflow)
        #define BEMAN_BIG_INT_HAS_BUILTIN_MUL_OVERFLOW 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_addc)
        #define BEMAN_BIG_INT_HAS_BUILTIN_ADDC 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_subc)
        #define BEMAN_BIG_INT_HAS_BUILTIN_SUBC 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_BUILTIN(__builtin_addc)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_ADDC 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_BUILTIN(__builtin_subc)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_SUBC 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_elementwise_fshl)
        #define BEMAN_BIG_INT_HAS_BUILTIN_ELEMENTWISE_FSHL 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_elementwise_fshr)
        #define BEMAN_BIG_INT_HAS_BUILTIN_ELEMENTWISE_FSHR 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_signbit)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_SIGNBIT 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_isfinite)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_ISFINITE 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_copysign)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_COPYSIGN 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_copysignf)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_COPYSIGNF 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_copysignl)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_COPYSIGNL 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_ldexp)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_LDEXP 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_ldexpf)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_LDEXPF 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_ldexpl)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_LDEXPL 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_fabs)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_FABS 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_fabsf)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_FABSF 1
    #endif

    #if BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN(__builtin_fabsl)
        #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_FABSL 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_cpu_init)
        #define BEMAN_BIG_INT_HAS_BUILTIN_CPU_INIT 1
    #endif

    #if BEMAN_BIG_INT_DETECT_BUILTIN(__builtin_cpu_supports)
        #define BEMAN_BIG_INT_HAS_BUILTIN_CPU_SUPPORTS 1
    #endif

    #undef BEMAN_BIG_INT_DETECT_BUILTIN
    #undef BEMAN_BIG_INT_DETECT_CONSTEXPR_BUILTIN
    #undef BEMAN_BIG_INT_DETECT_CONSTEXPR_MATH_BUILTIN

    // Exceptions

    #if (defined(_MSC_VER) && defined(_CPPUNWIND)) || defined(__EXCEPTIONS)
        #define BEMAN_BIG_INT_ALLOW_EXCEPTIONS
    #else
        #define BEMAN_BIG_INT_NO_EXCEPTIONS
    #endif

    // Target architecture

    #if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
        #define BEMAN_BIG_INT_TARGET_X86_64 1
    #endif

    #if defined(__i386__) || defined(_M_IX86)
        #define BEMAN_BIG_INT_TARGET_X86_32 1
    #endif

    #if defined(__aarch64__) || defined(_M_ARM64)
        #define BEMAN_BIG_INT_TARGET_AARCH64 1
    #endif

    #if defined(__wasm__) || defined(__EMSCRIPTEN__)
        #define BEMAN_BIG_INT_TARGET_WASM 1
    #endif

    // x86 instruction set extensions enabled at compile time (e.g. -mbmi2 -madx).
    // BEMAN_BIG_INT_HAS_AVX512_IFMA is the full AVX-512 set the IFMA kernels need.

    #if defined(__BMI2__) && defined(__ADX__)
        #define BEMAN_BIG_INT_HAS_BMI2_ADX 1
    #endif

    #if defined(__AVX512IFMA__) && defined(__AVX512VL__) && defined(__AVX512BW__) && defined(__AVX512VBMI__)
        #define BEMAN_BIG_INT_HAS_AVX512_IFMA 1
    #endif

    // MSVC intrinsics (<intrin.h>) and GNU x86 inline assembly. Clang-cl also
    // defines _MSC_VER, but takes the __builtin_* paths wherever both exist.

    // __umulh and __mulh
    #if defined(_MSC_VER) && (defined(BEMAN_BIG_INT_TARGET_X86_64) || defined(BEMAN_BIG_INT_TARGET_AARCH64))
        #define BEMAN_BIG_INT_HAS_INTRINSIC_UMULH 1
    #endif

    // _umul128 and _mul128, _udiv128, _addcarry_u64 and _subborrow_u64
    #if defined(_MSC_VER) && defined(BEMAN_BIG_INT_TARGET_X86_64)
        #define BEMAN_BIG_INT_HAS_INTRINSIC_UMUL128 1
        #define BEMAN_BIG_INT_HAS_INTRINSIC_UDIV128 1
        #define BEMAN_BIG_INT_HAS_INTRINSIC_ADDCARRY_U64 1
    #endif

    // _addcarry_u8/16/32 and _subborrow_u8/16/32; __cpuid, __cpuidex and _xgetbv
    #if defined(_MSC_VER) && (defined(BEMAN_BIG_INT_TARGET_X86_64) || defined(BEMAN_BIG_INT_TARGET_X86_32))
        #define BEMAN_BIG_INT_HAS_INTRINSIC_ADDCARRY 1
        #define BEMAN_BIG_INT_HAS_INTRINSIC_CPUID 1
    #endif

    #if defined(__GNUC__) && (defined(BEMAN_BIG_INT_TARGET_X86_64) || defined(BEMAN_BIG_INT_TARGET_X86_32))
        #define BEMAN_BIG_INT_HAS_X86_GNU_ASM 1
    #endif

    // Word size

    #if INTPTR_MAX == INT64_MAX || defined(BEMAN_BIG_INT_TARGET_WASM)
        #define BEMAN_BIG_INT_WORD_BITS 64
    #elif INTPTR_MAX == INT32_MAX
        #define BEMAN_BIG_INT_WORD_BITS 32
    #else
        #error Unknown pointer size or missing size macros!
    #endif

    // Build configuration defaults ================================================

    // CMake sets these from its options. Without it they follow the compiler's own
    // feature macros, and each can still be set explicitly with -D. The FP SIMD
    // multiplication (BEMAN_BIG_INT_SIMD_MUL) is never turned on here: it is exact
    // only under floating-point flags that cannot be queried, so it must be
    // defined explicitly.

    // As in CMake, forcing the IFMA kernels on also forces their small-operand
    // fallback, the BMI2/ADX kernels, unless that is set explicitly.
    #ifndef BEMAN_BIG_INT_X86_64_BMI2_ADX
        #if defined(BEMAN_BIG_INT_HAS_BMI2_ADX) || \
            (defined(BEMAN_BIG_INT_X86_64_AVX512_IFMA) && BEMAN_BIG_INT_X86_64_AVX512_IFMA)
            #define BEMAN_BIG_INT_X86_64_BMI2_ADX 1
        #else
            #define BEMAN_BIG_INT_X86_64_BMI2_ADX 0
        #endif
    #endif

    #ifndef BEMAN_BIG_INT_X86_64_AVX512_IFMA
        #if defined(BEMAN_BIG_INT_HAS_AVX512_IFMA) && defined(BEMAN_BIG_INT_HAS_BMI2_ADX)
            #define BEMAN_BIG_INT_X86_64_AVX512_IFMA 1
        #else
            #define BEMAN_BIG_INT_X86_64_AVX512_IFMA 0
        #endif
    #endif

#endif // Config Generated

// Module support ==============================================================

// `BEMAN_BIG_INT_BUILD_MODULE` is defined by module/big_int.cppm and propagated
// PUBLIC by its CMake target, so it is never defined in an ordinary header
// build. `BEMAN_BIG_INT_INTERFACE_UNIT` is defined only by big_int.cppm itself;
// it guards the few declarations (e.g. the global-scope `bit_int` aliases
// below) that a module consumer receives from the import rather than
// redeclare -- redeclaring them in a consumer would give a second, distinct
// type and break overload resolution.

// A handful of detail-namespace entities are exercised directly by the module
// test suite. BEMAN_BIG_INT_TEST_EXPORT exports them only when the module is
// built for testing (BEMAN_BIG_INT_EXPORT_TESTING), so the normal module API
// stays limited to the public interface. It expands to nothing in ordinary
// (header) builds.
#if defined(BEMAN_BIG_INT_BUILD_MODULE) && defined(BEMAN_BIG_INT_EXPORT_TESTING)
    #define BEMAN_BIG_INT_TEST_EXPORT export
#else
    #define BEMAN_BIG_INT_TEST_EXPORT
#endif

#ifndef BEMAN_BIG_INT_NAMESPACE
    #define BEMAN_BIG_INT_NAMESPACE beman::big_int
#endif // BEMAN_BIG_INT_NAMESPACE

#define BEMAN_BIG_INT_BEGIN_NAMESPACE namespace BEMAN_BIG_INT_NAMESPACE {
#define BEMAN_BIG_INT_END_NAMESPACE }

#ifdef BEMAN_BIG_INT_BUILD_MODULE
    #define BEMAN_BIG_INT_EXPORT export
    // An internal-linkage namespace-scope entity reachable from an exported
    // template is a TU-local exposure, which GCC rejects. `inline constexpr`
    // keeps such an entity out of TU-local territory in module mode; ordinary
    // header builds keep the existing `static constexpr` spelling.
    #define BEMAN_BIG_INT_INLINE_CONSTEXPR inline constexpr
#else
    #define BEMAN_BIG_INT_EXPORT
    #define BEMAN_BIG_INT_INLINE_CONSTEXPR static constexpr
#endif

// Compiler identification =====================================================

#if defined(_MSC_VER)
    #define BEMAN_BIG_INT_MSVC _MSC_VER
#elif defined(__clang__)
    #define BEMAN_BIG_INT_CLANG __clang__
#elif defined(__GNUC__)
    #define BEMAN_BIG_INT_GCC __GNUC__
#else
    #error "Unknown compiler (none of MSVC, Clang, GCC)."
#endif

#ifdef __GNUC__
    // Separate case for any GNU-C-compliant compilers,
    // which is both GCC and Clang.
    #define BEMAN_BIG_INT_GNUC __GNUC__
#endif // __GNUC__

// Compiler builtins ===========================================================

// The library calls compiler builtins and intrinsics only through these wrappers,
// each defined only when feature detection found it, so a compiler that spells
// one differently needs a new mapping here and nowhere else. Code tests the
// wrapper itself with #ifdef, or BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_<NAME> where
// it must also work during constant evaluation. The floating-point ones are only
// ever needed there, so they are defined only when usable in constant evaluation.

// Every supported compiler has these, so they are always defined.
#define BEMAN_BIG_INT_BUILTIN_FILE(...) __builtin_FILE(__VA_ARGS__)
#define BEMAN_BIG_INT_BUILTIN_LINE(...) __builtin_LINE(__VA_ARGS__)
#define BEMAN_BIG_INT_BUILTIN_FUNCTION(...) __builtin_FUNCTION(__VA_ARGS__)

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_IS_INTEGRAL
    #define BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(...) __is_integral(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_IS_CONSTANT_EVALUATED
    #define BEMAN_BIG_INT_BUILTIN_IS_CONSTANT_EVALUATED(...) __builtin_is_constant_evaluated(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_CONSTANT_P
    #define BEMAN_BIG_INT_BUILTIN_CONSTANT_P(...) __builtin_constant_p(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_TRAP
    #define BEMAN_BIG_INT_BUILTIN_TRAP(...) __builtin_trap(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_ADD_OVERFLOW
    #define BEMAN_BIG_INT_BUILTIN_ADD_OVERFLOW(...) __builtin_add_overflow(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_SUB_OVERFLOW
    #define BEMAN_BIG_INT_BUILTIN_SUB_OVERFLOW(...) __builtin_sub_overflow(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_MUL_OVERFLOW
    #define BEMAN_BIG_INT_BUILTIN_MUL_OVERFLOW(...) __builtin_mul_overflow(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_ADDC
    #define BEMAN_BIG_INT_BUILTIN_ADDC(...) __builtin_addc(__VA_ARGS__)
    #define BEMAN_BIG_INT_BUILTIN_ADDCL(...) __builtin_addcl(__VA_ARGS__)
    #define BEMAN_BIG_INT_BUILTIN_ADDCLL(...) __builtin_addcll(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_SUBC
    #define BEMAN_BIG_INT_BUILTIN_SUBC(...) __builtin_subc(__VA_ARGS__)
    #define BEMAN_BIG_INT_BUILTIN_SUBCL(...) __builtin_subcl(__VA_ARGS__)
    #define BEMAN_BIG_INT_BUILTIN_SUBCLL(...) __builtin_subcll(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_ELEMENTWISE_FSHL
    #define BEMAN_BIG_INT_BUILTIN_ELEMENTWISE_FSHL(...) __builtin_elementwise_fshl(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_ELEMENTWISE_FSHR
    #define BEMAN_BIG_INT_BUILTIN_ELEMENTWISE_FSHR(...) __builtin_elementwise_fshr(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_SIGNBIT
    #define BEMAN_BIG_INT_BUILTIN_SIGNBIT(...) __builtin_signbit(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_ISFINITE
    #define BEMAN_BIG_INT_BUILTIN_ISFINITE(...) __builtin_isfinite(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_COPYSIGN
    #define BEMAN_BIG_INT_BUILTIN_COPYSIGN(...) __builtin_copysign(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_COPYSIGNF
    #define BEMAN_BIG_INT_BUILTIN_COPYSIGNF(...) __builtin_copysignf(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_COPYSIGNL
    #define BEMAN_BIG_INT_BUILTIN_COPYSIGNL(...) __builtin_copysignl(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_LDEXP
    #define BEMAN_BIG_INT_BUILTIN_LDEXP(...) __builtin_ldexp(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_LDEXPF
    #define BEMAN_BIG_INT_BUILTIN_LDEXPF(...) __builtin_ldexpf(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_LDEXPL
    #define BEMAN_BIG_INT_BUILTIN_LDEXPL(...) __builtin_ldexpl(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_FABS
    #define BEMAN_BIG_INT_BUILTIN_FABS(...) __builtin_fabs(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_FABSF
    #define BEMAN_BIG_INT_BUILTIN_FABSF(...) __builtin_fabsf(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_FABSL
    #define BEMAN_BIG_INT_BUILTIN_FABSL(...) __builtin_fabsl(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_CPU_INIT
    #define BEMAN_BIG_INT_BUILTIN_CPU_INIT(...) __builtin_cpu_init(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_BUILTIN_CPU_SUPPORTS
    #define BEMAN_BIG_INT_BUILTIN_CPU_SUPPORTS(...) __builtin_cpu_supports(__VA_ARGS__)
#endif

// The module interface unit includes <intrin.h> in its global module fragment.
#if defined(BEMAN_BIG_INT_MSVC) && !defined(BEMAN_BIG_INT_BUILD_MODULE)
    #include <intrin.h>
#endif

#ifdef BEMAN_BIG_INT_HAS_INTRINSIC_UMULH
    #define BEMAN_BIG_INT_INTRINSIC_MULH(...) __mulh(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_UMULH(...) __umulh(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_INTRINSIC_UMUL128
    #define BEMAN_BIG_INT_INTRINSIC_MUL128(...) _mul128(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_UMUL128(...) _umul128(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_INTRINSIC_UDIV128
    #define BEMAN_BIG_INT_INTRINSIC_UDIV128(...) _udiv128(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_INTRINSIC_ADDCARRY
    #define BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U8(...) _addcarry_u8(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U16(...) _addcarry_u16(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U32(...) _addcarry_u32(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U8(...) _subborrow_u8(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U16(...) _subborrow_u16(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U32(...) _subborrow_u32(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_INTRINSIC_ADDCARRY_U64
    #define BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U64(...) _addcarry_u64(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U64(...) _subborrow_u64(__VA_ARGS__)
#endif

#ifdef BEMAN_BIG_INT_HAS_INTRINSIC_CPUID
    #define BEMAN_BIG_INT_INTRINSIC_CPUID(...) __cpuid(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_CPUIDEX(...) __cpuidex(__VA_ARGS__)
    #define BEMAN_BIG_INT_INTRINSIC_XGETBV(...) _xgetbv(__VA_ARGS__)
#endif

// Undefine min()/max() for MSVC ===============================================
#ifdef min
    #error min is defined as a macro. Define NOMINMAX.
#endif
#ifdef max
    #error min is defined as a macro. Define NOMINMAX.
#endif
#ifdef BEMAN_BIG_INT_MSVC
    #define NOMINMAX
#endif

// Unsupported static_assert to nothing (for old compilers) ==============
#if (!defined(BEMAN_BIG_INT_CLANG) && (defined(BEMAN_BIG_INT_GCC) && (BEMAN_BIG_INT_GCC <= 13)))
    #define BEMAN_BIG_INT_STATIC_ASSERT_FALSE(...)
#else
    #define BEMAN_BIG_INT_STATIC_ASSERT_FALSE(...) static_assert(false, __VA_ARGS__)
#endif

// Diagnostic suppression ======================================================

// See https://stackoverflow.com/q/45762357/5740428
#define BEMAN_BIG_INT_PRAGMA_STR_IMPL(...) _Pragma(#__VA_ARGS__)
#define BEMAN_BIG_INT_PRAGMA_STR(...) BEMAN_BIG_INT_PRAGMA_STR_IMPL(__VA_ARGS__)

#if defined(BEMAN_BIG_INT_GCC)
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH() _Pragma("GCC diagnostic push")
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP() _Pragma("GCC diagnostic pop")
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...) BEMAN_BIG_INT_PRAGMA_STR(GCC diagnostic ignored __VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...) BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(__VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...)
#elif defined(BEMAN_BIG_INT_CLANG)
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH() _Pragma("clang diagnostic push")
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP() _Pragma("clang diagnostic pop")
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...) BEMAN_BIG_INT_PRAGMA_STR(clang diagnostic ignored __VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...) BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(__VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...)
#elif defined(BEMAN_BIG_INT_MSVC)
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH() _Pragma("warning(push)")
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP() _Pragma("warning(pop)")
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...) BEMAN_BIG_INT_PRAGMA_STR(warning(disable : __VA_ARGS__))
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...) BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP()
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...)
#endif

// _BitInt aliases =============================================================

// A module consumer receives these aliases from the import, so they are only
// declared in ordinary builds and in the module interface unit itself;
// declaring them again in a consumer would give a second, distinct type. They
// are not marked BEMAN_BIG_INT_EXPORT: these are global-scope aliases, and
// exporting them would inject unqualified `bit_int`/`bit_uint` into every
// importer's global namespace. They only need to be reachable here.
#if defined(BEMAN_BIG_INT_HAS_BITINT_EXTENSION) && \
    (!defined(BEMAN_BIG_INT_BUILD_MODULE) || defined(BEMAN_BIG_INT_INTERFACE_UNIT))

__extension__ template <const int N>
using bit_int = _BitInt(N);

__extension__ template <const int N>
using bit_uint = unsigned _BitInt(N);

#endif

// std::hash reach over bit-precise integers ===================================
//
// libc++ currently supports 4 words (20 Aug 2026)
// If this value increases or needs tested a user can change it on the command line,
// or simply PR this one spot to increase the availability
#ifndef BEMAN_BIG_INT_HASH_MAX_OBJECT_WORDS
    #define BEMAN_BIG_INT_HASH_MAX_OBJECT_WORDS 4
#endif

// 128-bit integer support =====================================================

#ifdef BEMAN_BIG_INT_MSVC
    #ifndef BEMAN_BIG_INT_BUILD_MODULE
        #include <__msvc_int128.hpp>
    #endif
#endif // BEMAN_BIG_INT_MSVC

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

#if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 128
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL 1
using int128_t  = bit_int<128>;
using uint128_t = bit_uint<128>;
#elif defined(BEMAN_BIG_INT_HAS_INT128_EXTENSION)
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL 1
__extension__ using int128_t  = __int128;
__extension__ using uint128_t = unsigned __int128;
#elif defined(BEMAN_BIG_INT_MSVC)
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_CLASS 1
using int128_t  = std::_Signed128;
using uint128_t = std::_Unsigned128;
#endif

// True when some 128-bit integer type exists (fundamental or class type).
// 32-bit targets such as arm-none-eabi typically have none.
#ifdef BEMAN_BIG_INT_HAS_INT128
inline constexpr bool has_int128_v = true;
#else
inline constexpr bool has_int128_v = false;
#endif

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

// Limb type selection =========================================================

BEMAN_BIG_INT_BEGIN_NAMESPACE

#ifdef BEMAN_BIG_INT_FORCED_LIMB_WIDTH
    #if BEMAN_BIG_INT_FORCED_LIMB_WIDTH != 32 && BEMAN_BIG_INT_FORCED_LIMB_WIDTH != 64
        #error BEMAN_BIG_INT_FORCED_LIMB_WIDTH must be either 32 or 64!
    #endif
    #define BEMAN_BIG_INT_LIMB_WIDTH BEMAN_BIG_INT_FORCED_LIMB_WIDTH
#else
    #define BEMAN_BIG_INT_LIMB_WIDTH BEMAN_BIG_INT_WORD_BITS
#endif // BEMAN_BIG_INT_FORCED_LIMB_WIDTH

#if BEMAN_BIG_INT_LIMB_WIDTH == 64

BEMAN_BIG_INT_EXPORT using uint_multiprecision_t = unsigned long long;
static_assert(sizeof(uint_multiprecision_t) == 8);
namespace detail {
// Signed counterpart to uint_multiprecision_t.
using int_multiprecision_t = long long;
    #ifdef BEMAN_BIG_INT_HAS_INT128
        // Indicates that `uint_wide_t` and `int_wide_t` exist.
        #define BEMAN_BIG_INT_HAS_WIDE_INT 1
// Unsigned integer type with twice the width of uint_multiprecision_t.
using uint_wide_t = uint128_t;
// Signed integer type with twice the width of int_multiprecision_t.
using int_wide_t = int128_t;
    #endif
} // namespace detail

#elif BEMAN_BIG_INT_LIMB_WIDTH == 32

BEMAN_BIG_INT_EXPORT using uint_multiprecision_t = unsigned int;
static_assert(sizeof(uint_multiprecision_t) == 4);
namespace detail {
// Signed counterpart to uint_multiprecision_t.
using int_multiprecision_t = int;
    // Indicates that `uint_wide_t` and `int_wide_t` exist.
    #define BEMAN_BIG_INT_HAS_WIDE_INT 1
// Unsigned integer type with twice the width of uint_multiprecision_t.
using uint_wide_t = unsigned long long;
// Signed integer type with twice the width of int_multiprecision_t.
using int_wide_t = long long;
} // namespace detail

#else
    #error BEMAN_BIG_INT_LIMB_WIDTH must be either 32 or 64!
#endif

#define BEMAN_BIG_INT_DOUBLE_LIMB_WIDTH (BEMAN_BIG_INT_LIMB_WIDTH * 2)

BEMAN_BIG_INT_END_NAMESPACE

// Special architecture assembly long-multiplication optimization ==============
// It is available for generic x86_64 and AArch64 on GCC/clang/MSVC ============
// The assembly kernels work on 64-bit limbs, so a build forcing 32-bit limbs
// uses the portable kernels instead. Those have C++ linkage, so they cannot
// bind to the assembly symbols that CMake still builds into the library.

#if defined(BEMAN_BIG_INT_TARGET_X86_64) && BEMAN_BIG_INT_LIMB_WIDTH == 64
    #define BEMAN_BIG_INT_ARCH_X86_64
#endif

// The BMI2/ADX and AVX-512 IFMA kernel choices (see Build configuration) only
// apply where the x86-64 kernels are called at all. IFMA covers large operands
// only; small ones still go through the BMI2/ADX-or-generic choice.
#ifndef BEMAN_BIG_INT_ARCH_X86_64
    #undef BEMAN_BIG_INT_X86_64_BMI2_ADX
    #define BEMAN_BIG_INT_X86_64_BMI2_ADX 0
    #undef BEMAN_BIG_INT_X86_64_AVX512_IFMA
    #define BEMAN_BIG_INT_X86_64_AVX512_IFMA 0
#endif

#if defined(BEMAN_BIG_INT_TARGET_AARCH64) && BEMAN_BIG_INT_LIMB_WIDTH == 64
    #define BEMAN_BIG_INT_ARCH_AARCH64
#endif

#if defined(BEMAN_BIG_INT_ARCH_X86_64) || defined(BEMAN_BIG_INT_ARCH_AARCH64)
    #define BEMAN_BIG_INT_HAS_ASM_KERNELS
#endif

#if defined(BEMAN_BIG_INT_HAS_ASM_KERNELS)
    #define BEMAN_BIG_INT_ASM_LINKAGE extern "C"
#else
    #define BEMAN_BIG_INT_ASM_LINKAGE inline
#endif

// Integer concepts and traits =================================================

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstdint>
    #include <type_traits>
    #include <concepts>
    #include <limits>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

template <class T>
concept cv_unqualified = !std::is_const_v<T> && !std::is_volatile_v<T>;

template <class T>
concept character_type =                                     //
    std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || //
    std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

#ifdef BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL
    #ifdef BEMAN_BIG_INT_HAS_BITINT
static_assert(BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(bit_int<32>) && BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(bit_uint<32>),
              "Bad compiler builtin __is_integral rejects _BitInt.");
    #endif
static_assert(BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(int) &&
              BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(const volatile unsigned int));
static_assert(BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(char) && BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(signed char) &&
              BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(unsigned char));
template <class T>
concept integral = BEMAN_BIG_INT_BUILTIN_IS_INTEGRAL(T);
#elif defined(BEMAN_BIG_INT_HAS_BITINT)
// If bit-precise integers do exist but we don't have a builtin __is_integral,
// we need to create our own concept for integral types that also includes _BitInt.
template <class T>
struct is_bit_int : std::false_type {};
template <std::size_t N>
struct is_bit_int<bit_int<N>> : std::true_type {};
template <std::size_t N>
struct is_bit_int<bit_uint<N>> : std::true_type {};
template <class T>
inline constexpr bool is_bit_int_v = is_bit_int<T>::value;
template <class T>
concept integral = std::integral<T> || is_bit_int_v<T>;
#else
// If bit-precise integers don't exist, std::integral is correct anyway.
using std::integral;
#endif

template <class T>
concept cv_unqualified_integral = integral<T> && cv_unqualified<T>;

// Modeled if `T` is a signed or unsigned integer type.
// That is, a standard integer type, extended integer type, or bit-precise integer type.
template <class T>
concept signed_or_unsigned = cv_unqualified_integral<T> //
                             && !std::is_same_v<T, bool> && !character_type<T>;
template <class T>
concept negative_representing = static_cast<T>(-1) < static_cast<T>(0);

// Modeled if `T` is a standard unsigned, extended unsigned, or bit-precise unsigned integer type.
template <class T>
concept unsigned_integer = signed_or_unsigned<T> && !negative_representing<T>;
// Modeled if `T` is a standard signed, extended signed, or bit-precise signed integer type.
template <class T>
concept signed_integer = signed_or_unsigned<T> && negative_representing<T>;

#ifdef BEMAN_BIG_INT_HAS_BITINT
static_assert(signed_or_unsigned<bit_int<32>>);
static_assert(unsigned_integer<bit_uint<32>>);
static_assert(signed_integer<bit_int<32>>);
#endif

// Like `std::make_signed`, but also supports bit-precise integers (`_BitInt`).
template <class T>
struct make_unsigned : std::make_unsigned<T> {};

#ifdef BEMAN_BIG_INT_HAS_INT128_CLASS
template <>
struct make_unsigned<int128_t> {
    using type = uint128_t;
};
template <>
struct make_unsigned<uint128_t> {
    using type = uint128_t;
};
#endif // BEMAN_BIG_INT_HAS_INT128_CLASS

#ifdef BEMAN_BIG_INT_HAS_BITINT
template <std::size_t N>
struct make_unsigned<bit_int<N>> {
    using type = bit_uint<N>;
};
template <std::size_t N>
struct make_unsigned<bit_uint<N>> {
    using type = bit_uint<N>;
};
#endif // BEMAN_BIG_INT_HAS_BITINT

template <class T>
struct make_unsigned<const T> {
    using type = const typename make_unsigned<T>::type;
};
template <class T>
struct make_unsigned<volatile T> {
    using type = volatile typename make_unsigned<T>::type;
};
template <class T>
struct make_unsigned<const volatile T> {
    using type = const volatile typename make_unsigned<T>::type;
};

template <class T>
using make_unsigned_t = typename make_unsigned<T>::type;

// Like `std::make_signed`, but also supports bit-precise integers (`_BitInt`).
template <class T>
struct make_signed : std::make_signed<T> {};

#ifdef BEMAN_BIG_INT_HAS_INT128_CLASS
template <>
struct make_signed<int128_t> {
    using type = int128_t;
};
template <>
struct make_signed<uint128_t> {
    using type = int128_t;
};
#endif // BEMAN_BIG_INT_HAS_INT128_CLASS

#ifdef BEMAN_BIG_INT_HAS_BITINT
template <std::size_t N>
struct make_signed<bit_int<N>> {
    using type = bit_int<N>;
};
template <std::size_t N>
struct make_signed<bit_uint<N>> {
    using type = bit_int<N>;
};
#endif // BEMAN_BIG_INT_HAS_BITINT

template <class T>
struct make_signed<const T> : make_signed<T> {
    using type = const typename make_signed<T>::type;
};
template <class T>
struct make_signed<volatile T> {
    using type = volatile typename make_signed<T>::type;
};
template <class T>
struct make_signed<const volatile T> {
    using type = const volatile typename make_signed<T>::type;
};

template <class T>
using make_signed_t = typename make_signed<T>::type;

// Alias template that maps a cv-unqualified integral type onto the underlying
// signed or unsigned integer type.
// For example, this converts `char8_t` to `unsigned char`, `int` to `int`, etc.
// The goal is to reduce redundant template instantiations.
template <cv_unqualified_integral T>
using make_signed_or_unsigned_t = std::conditional_t<std::is_signed_v<T>, make_signed_t<T>, make_unsigned_t<T>>;

template <class T>
concept cv_unqualified_floating_point = cv_unqualified<T> && std::floating_point<T>;

// Modeled if `T` is an arithmetic type - that is, a signed or unsigned integer type
// including `_BitInt` or a floating-point type.
// This extends `std::is_arithmetic_v to cover `_BitInt` types which are not standard integral.
template <class T>
concept cv_unqualified_arithmetic = cv_unqualified<T> && (integral<T> || std::floating_point<T>);

template <class T>
[[nodiscard, maybe_unused]] constexpr make_signed_or_unsigned_t<T> to_signed_or_unsigned(const T x) {
    return static_cast<make_signed_or_unsigned_t<T>>(x);
}

// A type trait with a `static constexpr std::size_t value` member storing the width of the type `T`.
// That is, the number of bits in the value representation.
// The trait is complete only if `T` is an integral type or (if supported)
// an integer class type such as MSVC `std::_Signed128`.
template <class T>
struct width;

template <class T>
struct width<const T> : width<T> {};
template <class T>
struct width<volatile T> : width<T> {};
template <class T>
struct width<const volatile T> : width<T> {};

template <cv_unqualified_integral T>
struct width<T>
    : std::integral_constant<std::size_t, static_cast<std::size_t>(std::numeric_limits<make_unsigned_t<T>>::digits)> {
};

#ifdef BEMAN_BIG_INT_HAS_INT128_CLASS
template <>
struct width<int128_t> : std::integral_constant<std::size_t, 128> {};
template <>
struct width<uint128_t> : std::integral_constant<std::size_t, 128> {};
#endif // BEMAN_BIG_INT_HAS_INT128_CLASS

#ifdef BEMAN_BIG_INT_HAS_BITINT
template <std::size_t N>
struct width<bit_int<N>> : std::integral_constant<std::size_t, N> {};
template <std::size_t N>
struct width<bit_uint<N>> : std::integral_constant<std::size_t, N> {};
#endif // BEMAN_BIG_INT_HAS_BITINT

template <class T>
inline constexpr std::size_t width_v = width<T>::value;

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

// Allocator trait detection ===================================================

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <memory>
#endif

#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// The C++23 feature-test macro can be set even when a particular allocator's
// `std::allocator_traits` doesn't actually expose `allocate_at_least`
// Example: libstdc++ pmr allocator
template <class Traits, class Alloc>
concept traits_has_allocate_at_least = requires(Alloc& a, typename Traits::size_type n) {
    { Traits::allocate_at_least(a, n) };
};

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST

// Trivial ABI =================================================================

#if defined(BEMAN_BIG_INT_CLANG)
    #define BEMAN_BIG_INT_TRIVIAL_ABI [[clang::trivial_abi]]
#else
    #define BEMAN_BIG_INT_TRIVIAL_ABI
#endif

// no_unique_address ===========================================================

#ifdef BEMAN_BIG_INT_MSVC
    #define BEMAN_BIG_INT_NO_UNIQUE_ADDRESS [[no_unique_address, msvc::no_unique_address]]
#else
    #define BEMAN_BIG_INT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

// Noinline ====================================================================

#if defined(BEMAN_BIG_INT_GNUC)
    #define BEMAN_BIG_INT_NOINLINE [[gnu::noinline]]
#elif defined(BEMAN_BIG_INT_MSVC)
    #define BEMAN_BIG_INT_NOINLINE __declspec(noinline)
#else
    #define BEMAN_BIG_INT_NOINLINE
#endif

// assert ======================================================================

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstdlib>
    #include <cassert>
    #include <cstdio>
#endif

// LCOV_EXCL_START
// GCOVR_EXCL_START
BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Deliberately NOT std::source_location::current(): as a defaulted argument
// it is a consteval call, and MSVC rejects that with C7595 ("call to
// immediate function is not a constant expression") when the assert expands
// inside an instantiated constexpr function template. The pre-C++20
// builtins carry the same caller location with no consteval machinery and
// exist on GCC, Clang, and MSVC.
[[noreturn]] inline void assert_fail(const char* const source,
                                     const char* const file     = BEMAN_BIG_INT_BUILTIN_FILE(),
                                     const int         line     = BEMAN_BIG_INT_BUILTIN_LINE(),
                                     const char* const function = BEMAN_BIG_INT_BUILTIN_FUNCTION()) {
    std::fprintf(stderr, "%s:%d Assertion failed: %s\nSee: %s\n", file, line, source, function);
#ifdef BEMAN_BIG_INT_BUILTIN_TRAP
    BEMAN_BIG_INT_BUILTIN_TRAP();
#else
    std::abort();
#endif
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#define BEMAN_BIG_INT_ASSERT(...) (__VA_ARGS__ ? void() : ::BEMAN_BIG_INT_NAMESPACE::detail::assert_fail(#__VA_ARGS__))
// GCOVR_EXCL_STOP
// LCOV_EXCL_STOP

// BEMAN_BIG_INT_DEBUG is defined when NDEBUG is not. Debug-only code, such as
// BEMAN_BIG_INT_DEBUG_ASSERT, tests it instead of NDEBUG.
#ifndef NDEBUG
    #define BEMAN_BIG_INT_DEBUG 1
#endif

#ifdef BEMAN_BIG_INT_DEBUG
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) BEMAN_BIG_INT_ASSERT(__VA_ARGS__)
#else
    // The requires expression makes sure that we still check for expression validity,
    // even if the expression is not evaluated.
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) void(requires { __VA_ARGS__; })
#endif

// if consteval ================================================================

#ifdef BEMAN_BIG_INT_HAS_CPP_IF_CONSTEVAL
    #define BEMAN_BIG_INT_IS_CONSTEVAL consteval
    #ifdef BEMAN_BIG_INT_MSVC
        // In MSVC, all code following `if !consteval` is considered unreachable.
        // The warning is also impossible to suppress, so NEVER use `if !consteval` on MSVC.
        // https://developercommunity.microsoft.com/t/Code-following-if-consteval-is-unreac/11073119
        #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!BEMAN_BIG_INT_BUILTIN_IS_CONSTANT_EVALUATED())
    #else
        #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL !consteval
    #endif // BEMAN_BIG_INT_MSVC
#elif defined(BEMAN_BIG_INT_BUILTIN_IS_CONSTANT_EVALUATED)
    #define BEMAN_BIG_INT_IS_CONSTEVAL (BEMAN_BIG_INT_BUILTIN_IS_CONSTANT_EVALUATED())
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!BEMAN_BIG_INT_BUILTIN_IS_CONSTANT_EVALUATED())
#else
    #ifndef BEMAN_BIG_INT_BUILD_MODULE
        #include <type_traits>
    #endif
    #define BEMAN_BIG_INT_IS_CONSTEVAL (::std::is_constant_evaluated())
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!::std::is_constant_evaluated())
#endif

// consteval bit_cast to _BitInt ===============================================

// At the time of writing, not even clang trunk supports bit-casting to _BitInt
// during constant evaluation.
// The intended usage of this macro is
// `if BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT`
// so as to guard against accidental constexpr use of such bit-casts.

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BIT_CAST_TO_BIT_INT
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT constexpr(true)
#else
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT BEMAN_BIG_INT_IS_NOT_CONSTEVAL
#endif // BEMAN_BIG_INT_HAS_CONSTEXPR_BIT_CAST_TO_BIT_INT

// Constant propagation detection ==============================================

#ifdef BEMAN_BIG_INT_BUILTIN_CONSTANT_P
    #define BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(...) BEMAN_BIG_INT_BUILTIN_CONSTANT_P(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(...) (void(__VA_ARGS__), false)
#endif

// Division result =============================================================

BEMAN_BIG_INT_BEGIN_NAMESPACE

BEMAN_BIG_INT_EXPORT template <class T>
struct div_result {
    T quotient;
    T remainder;

    friend auto operator<=>(const div_result&, const div_result&) = default;
};

BEMAN_BIG_INT_END_NAMESPACE

// Division with rounding toward positive infinity =============================

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Returns the quotient of the division `x / y`,
// rounded towards positive infinity.
template <unsigned_integer T>
[[nodiscard]] constexpr T div_to_pos_inf(const T x, const T y) {
    BEMAN_BIG_INT_DEBUG_ASSERT(y != 0);
    return (x / y) + T(x % y != 0);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

// Exceptions ==================================================================

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #ifdef BEMAN_BIG_INT_ALLOW_EXCEPTIONS
        #include <stdexcept>
    #else
        #include <cstdlib>
    #endif
#endif

// =============================================================================

#endif // BEMAN_BIG_INT_CONFIG_HPP
