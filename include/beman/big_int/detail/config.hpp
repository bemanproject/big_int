// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_HPP
#define BEMAN_BIG_INT_CONFIG_HPP

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

// Builtin detection ===========================================================

#ifdef __has_builtin
    #define BEMAN_BIG_INT_HAS_BUILTIN(...) __has_builtin(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_HAS_BUILTIN(...) 0
#endif // __has_builtin

// In addition to BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN, which is an exact test,
// `BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN` falls back onto `BEMAN_BIG_INT_HAS_BUILTIN`
// if constexpr builtin testing is not available.
// This is useful for GCC, which supports __has_builtin but not __has_constexpr_builtin,
// and many GCC intrinsics are usable during constant evaluation.
#ifdef __has_constexpr_builtin
    #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(...) __has_constexpr_builtin(__VA_ARGS__)
    #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(...) __has_constexpr_builtin(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(...) 0
    #define BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(...) BEMAN_BIG_INT_HAS_BUILTIN(__VA_ARGS__)
#endif // __has_constexpr_builtin

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

// _BitInt detection ===========================================================

// Guarding this include is safe only because the .cppm supplies <climits> in
// its global module fragment before the purview include below tests
// BITINT_MAXWIDTH.
#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <climits> // for BITINT_MAXWIDTH
#endif

#ifdef BITINT_MAXWIDTH
    // Once _BitInt is a standard feature and available on all compilers,
    // this case should be selected for all compilers.
    #define BEMAN_BIG_INT_BITINT_MAXWIDTH BITINT_MAXWIDTH
    #define BEMAN_BIG_INT_HAS_BITINT 1
#elif defined(__BITINT_MAXWIDTH__)
    // This case is for Clang when it provides _BitInt as an extension.
    #define BEMAN_BIG_INT_BITINT_MAXWIDTH __BITINT_MAXWIDTH__
    #define BEMAN_BIG_INT_HAS_BITINT 1

    // A module consumer receives these aliases from the import, so they are only
    // declared in ordinary builds and in the module interface unit itself;
    // declaring them again in a consumer would give a second, distinct type. They
    // are not marked BEMAN_BIG_INT_EXPORT: these are global-scope aliases, and
    // exporting them would inject unqualified `bit_int`/`bit_uint` into every
    // importer's global namespace. They only need to be reachable here.
    #if !defined(BEMAN_BIG_INT_BUILD_MODULE) || defined(BEMAN_BIG_INT_INTERFACE_UNIT)

__extension__ template <const int N>
using bit_int = _BitInt(N);

__extension__ template <const int N>
using bit_uint = unsigned _BitInt(N);

    #endif

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

// std::hash reach over bit-precise integers ===================================
//
// libc++ currently supports 4 words (20 Aug 2026)
// If this value increases or needs tested a user can change it on the command line,
// or simply PR this one spot to increase the availability
#ifndef BEMAN_BIG_INT_HASH_MAX_OBJECT_WORDS
    #define BEMAN_BIG_INT_HASH_MAX_OBJECT_WORDS 4
#endif

// 32-bit/64-bit ===============================================================

// Guarding this include is safe only because the .cppm supplies <cstdint> in
// its global module fragment before the purview include below tests
// INTPTR_MAX.
#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstdint>
#endif

#if INTPTR_MAX == INT64_MAX || defined(__wasm__) || defined(__EMSCRIPTEN__)
    #define BEMAN_BIG_INT_WORD_BITS 64
#elif INTPTR_MAX == INT32_MAX
    #define BEMAN_BIG_INT_WORD_BITS 32
#else
    #error Unknown pointer size or missing size macros!
#endif

// 128-bit integer support =====================================================

#ifdef BEMAN_BIG_INT_MSVC
    #ifndef BEMAN_BIG_INT_BUILD_MODULE
        #include <__msvc_int128.hpp>
    #endif
#endif // BEMAN_BIG_INT_MSVC

namespace beman::big_int::detail {

#if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 128
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL 1
using int128_t  = bit_int<128>;
using uint128_t = bit_uint<128>;
#elif defined(__SIZEOF_INT128__)
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

} // namespace beman::big_int::detail

// Limb type selection =========================================================

namespace beman::big_int {

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

} // namespace beman::big_int

// Special architecture assembly long-multiplication optimization ==============
// It is available for generic x86_64 and AArch64 on GCC/clang/MSVC ============
// The assembly kernels work on 64-bit limbs, so a build forcing 32-bit limbs
// uses the portable kernels instead. Those have C++ linkage, so they cannot
// bind to the assembly symbols that CMake still builds into the library.

#if ((defined(BEMAN_BIG_INT_CLANG) && (defined(__x86_64__) || defined(_M_X64))) ||                               \
     (defined(BEMAN_BIG_INT_GCC) && defined(__x86_64__)) || (defined(BEMAN_BIG_INT_MSVC) && defined(_M_X64))) && \
    BEMAN_BIG_INT_LIMB_WIDTH == 64
    #define BEMAN_BIG_INT_ARCH_X86_64
#endif

#if (((defined(BEMAN_BIG_INT_GCC) || defined(BEMAN_BIG_INT_CLANG)) && defined(__aarch64__)) || \
     (defined(BEMAN_BIG_INT_MSVC) && defined(_M_ARM64))) &&                                    \
    BEMAN_BIG_INT_LIMB_WIDTH == 64
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

namespace beman::big_int::detail {

template <class T>
concept cv_unqualified = !std::is_const_v<T> && !std::is_volatile_v<T>;

template <class T>
concept character_type =                                     //
    std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || //
    std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

#if BEMAN_BIG_INT_HAS_BUILTIN(__is_integral)
    #ifdef BEMAN_BIG_INT_HAS_BITINT
static_assert(__is_integral(bit_int<32>) && __is_integral(bit_int<32>),
              "Bad compiler builtin __is_integral rejects _BitInt.");
    #endif
static_assert(__is_integral(int) && __is_integral(const volatile unsigned int));
static_assert(__is_integral(char) && __is_integral(signed char) && __is_integral(unsigned char));
template <class T>
concept integral = __is_integral(T);
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

} // namespace beman::big_int::detail

// Allocator trait detection ===================================================

// `<memory>` defines `__cpp_lib_allocate_at_least`
#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <memory>
#endif

#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L

namespace beman::big_int::detail {

// The C++23 feature-test macro can be set even when a particular allocator's
// `std::allocator_traits` doesn't actually expose `allocate_at_least`
// Example: libstdc++ pmr allocator
template <class Traits, class Alloc>
concept traits_has_allocate_at_least = requires(Alloc& a, typename Traits::size_type n) {
    { Traits::allocate_at_least(a, n) };
};

} // namespace beman::big_int::detail

#endif // __cpp_lib_allocate_at_least

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
namespace beman::big_int::detail {

// Deliberately NOT std::source_location::current(): as a defaulted argument
// it is a consteval call, and MSVC rejects that with C7595 ("call to
// immediate function is not a constant expression") when the assert expands
// inside an instantiated constexpr function template. The pre-C++20
// builtins carry the same caller location with no consteval machinery and
// exist on GCC, Clang, and MSVC.
[[noreturn]] inline void assert_fail(const char* const source,
                                     const char* const file     = __builtin_FILE(),
                                     const int         line     = __builtin_LINE(),
                                     const char* const function = __builtin_FUNCTION()) {
    std::fprintf(stderr, "%s:%d Assertion failed: %s\nSee: %s\n", file, line, source, function);
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_trap)
    __builtin_trap();
#else
    std::abort();
#endif
}

} // namespace beman::big_int::detail

#define BEMAN_BIG_INT_ASSERT(...) (__VA_ARGS__ ? void() : ::beman::big_int::detail::assert_fail(#__VA_ARGS__))
// GCOVR_EXCL_STOP
// LCOV_EXCL_STOP

#ifndef NDEBUG
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) BEMAN_BIG_INT_ASSERT(__VA_ARGS__)
#else
  // The requires expression makes sure that we still check for expression validity,
  // even if the expression is not evaluated.
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) void(requires { __VA_ARGS__; })
#endif

// if consteval ================================================================

#if defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
    #define BEMAN_BIG_INT_IS_CONSTEVAL consteval
    #ifdef BEMAN_BIG_INT_MSVC
  // In MSVC, all code following `if !consteval` is considered unreachable.
  // The warning is also impossible to suppress, so NEVER use `if !consteval` on MSVC.
  // https://developercommunity.microsoft.com/t/Code-following-if-consteval-is-unreac/11073119
        #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!__builtin_is_constant_evaluated())
    #else
        #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL !consteval
    #endif // BEMAN_BIG_INT_MSVC
#elif BEMAN_BIG_INT_HAS_BUILTIN(__builtin_is_constant_evaluated)
    #define BEMAN_BIG_INT_IS_CONSTEVAL (__builtin_is_constant_evaluated())
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!__builtin_is_constant_evaluated())
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

#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_constant_p)
    #define BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(...) __builtin_constant_p(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(...) (void(__VA_ARGS__), false)
#endif

// Division result =============================================================

namespace beman::big_int {

BEMAN_BIG_INT_EXPORT template <class T>
struct div_result {
    T quotient;
    T remainder;

    friend auto operator<=>(const div_result&, const div_result&) = default;
};

} // namespace beman::big_int

// Division with rounding toward positive infinity =============================

namespace beman::big_int::detail {

// Returns the quotient of the division `x / y`,
// rounded towards positive infinity.
template <unsigned_integer T>
[[nodiscard]] constexpr T div_to_pos_inf(const T x, const T y) {
    BEMAN_BIG_INT_DEBUG_ASSERT(y != 0);
    return (x / y) + T(x % y != 0);
}

} // namespace beman::big_int::detail

// Exceptions ==================================================================

#if (defined(_MSC_VER) && defined(_CPPUNWIND)) || defined(__EXCEPTIONS)
    #ifndef BEMAN_BIG_INT_BUILD_MODULE
        #include <stdexcept>
    #endif
    #define BEMAN_BIG_INT_ALLOW_EXCEPTIONS
#else
    #ifndef BEMAN_BIG_INT_BUILD_MODULE
        #include <cstdlib>
    #endif
    #define BEMAN_BIG_INT_NO_EXCEPTIONS
#endif

// =============================================================================

#endif // BEMAN_BIG_INT_CONFIG_HPP
