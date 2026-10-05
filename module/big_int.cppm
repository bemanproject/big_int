// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

module;

// Tier A - platform headers, always textual regardless of import std. Only two,
// and only on MSVC: <__msvc_int128.hpp> supplies std::_Signed128/std::_Unsigned128
// for detail::int128_t, and <intrin.h> supplies the widening-multiply / bit-scan
// intrinsics detail/wide_ops.hpp uses on that compiler.
//
// Deliberately NOT included here: <immintrin.h> and <arm_neon.h>. Those are
// reachable only from src/ntt_fp_avx2.cpp and src/ntt_fp_neon.cpp, which are
// ordinary translation units in the static library carrying their own
// -mavx2 -mfma / NEON-baseline compile options set per-source in the root
// CMakeLists. Neither is ever reachable from the header graph rooted at
// big_int.hpp, so pulling them into this interface unit's purview would be
// actively wrong.
#ifdef _MSC_VER
    #include <__msvc_int128.hpp> // std::_Signed128 / std::_Unsigned128
    #include <intrin.h>          // widening-multiply / bit-scan intrinsics (detail/wide_ops.hpp)
#endif

// Tier B - macro-carrying headers. These stay textual even under `import std`
// because `import std` exports declarations but not macros, and the headers
// branch on the macros/objects these six provide regardless of which mode is
// active.
#include <version> // every __cpp_lib_* feature test the headers branch on
#include <cassert> // parity with the sibling Boost modules
#include <cfloat>  // LDBL_MANT_DIG / LDBL_MAX_EXP (detail/floats.hpp has a hard #error without them)
#include <climits> // BITINT_MAXWIDTH (selects whether _BitInt support exists), CHAR_BIT
#include <cstdint> // INTPTR_MAX / INT64_MAX / INT32_MAX (selects the limb width, hence the ABI)
#include <cstdio>  // stderr, which `import std` deliberately does not provide

// Tier C - everything the headers include inside their own
// #ifndef BEMAN_BIG_INT_BUILD_MODULE guards, minus the six Tier B headers above
// (those are needed for their macros either way). When BEMAN_BIG_INT_USE_STD_MODULE
// is set these are instead provided by `import std;` below. Regenerate this list
// after adding a guarded include under include/ with:
//   grep -rn -B1 '#include <' include | grep -A1 BUILD_MODULE
#ifndef BEMAN_BIG_INT_USE_STD_MODULE

    #include <algorithm>
    #include <array>
    #include <bit>
    #include <charconv>
    #include <cmath>
    #include <compare>
    #include <concepts>
    #include <cstddef>
    #include <cstdlib>
    #include <functional>
    #include <initializer_list>
    #include <limits>
    #include <locale>
    #include <memory>
    #include <memory_resource>
    #include <ranges>
    #include <span>
    #include <stdexcept>
    #include <string>
    #include <string_view>
    #include <system_error>
    #include <type_traits>
    #include <utility>
    #include <vector>

    #if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
        #include <format>
    #endif

    #if __has_include(<stdfloat>)
        #include <stdfloat>
    #endif

#endif // BEMAN_BIG_INT_USE_STD_MODULE

// Guarded because the module target carries BEMAN_BIG_INT_BUILD_MODULE as a PUBLIC
// compile definition: when an installed consumer recompiles this interface unit, the
// macro already arrives on the command line, and redefining it here with a different
// spelling would warn in every downstream build.
#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #define BEMAN_BIG_INT_BUILD_MODULE
#endif

// Marks this translation unit as the interface unit, as opposed to a consumer
// that imports the module. The global-scope bit_int/bit_uint aliases in
// detail/config.hpp key off this macro so a consumer does not redeclare them.
#define BEMAN_BIG_INT_INTERFACE_UNIT

export module beman.big_int;

#ifdef BEMAN_BIG_INT_USE_STD_MODULE
import std;
#endif

extern "C++" {

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 5244)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#endif

#include <beman/big_int.hpp>

#ifdef _MSC_VER
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif

} // extern "C++"
