// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_STRING_HPP
#define BEMAN_BIG_INT_STRING_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstddef>
    #include <string>
    #include <system_error>
    #include <type_traits>
    #include <utility>
#endif

#include <beman/big_int/basic_big_int.hpp>
#include <beman/big_int/charconv.hpp>
#include <beman/big_int/detail/base_tables.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

BEMAN_BIG_INT_BEGIN_NAMESPACE

// [big.int.charconv], string conversions
namespace detail {

// Renders the magnitude and sign of `x` in the given base into a freshly
// allocated string of character type `C`. The heavy lifting is delegated to
// `to_chars`, which only ever emits narrow ASCII; for wide character types the
// narrow result is widened afterwards. The digit alphabet and the minus sign
// belong to the basic execution character set, whose member values are
// preserved by the narrow-to-wide conversion.
//
// `x` is forwarded to `to_chars`, so an argument the caller has handed over is
// rendered without copying its magnitude. Its width is read up front, before
// the `to_chars` call is in a position to consume it.
template <class C, class X>
    requires detail::is_basic_big_int_v<X>
[[nodiscard]] constexpr std::basic_string<C> to_basic_string(X&& x, const int base) {
    BEMAN_BIG_INT_ASSERT(base >= 2 && base <= 36);
    constexpr std::size_t minus_sign_size = 1;

    // For up to 1 bit, we need at most one digit in any base.
    // Otherwise, the digit width in any base is generally `log_base(x) + 1`.
    // To convert from the width (which is `log2(x) + 1`), we need to decrement,
    // convert the binary logarithm to a logarithm base `base`, and increment.
    const auto        width           = x.size();
    const std::size_t required_digits = width <= 1
                                            ? std::size_t{1} + minus_sign_size
                                            : detail::approximate_ceil_div_log2(width - 1, base) + 1 + minus_sign_size;

#ifdef __cpp_lib_string_resize_and_overwrite
    std::string narrow;
    // `resize_and_overwrite` invokes the operation exactly once, so forwarding
    // from inside the lambda hands `x` over at most once.
    narrow.resize_and_overwrite(required_digits, [&](char* const data, const std::size_t n) {
        const auto [p, ec] = to_chars(data, data + n, std::forward<X>(x), base);
        BEMAN_BIG_INT_ASSERT(ec == std::errc{});
        return static_cast<std::size_t>(p - data);
    });
#else
    std::string narrow(required_digits, char{});
    const auto [p, ec] = to_chars(narrow.data(), narrow.data() + narrow.size(), std::forward<X>(x), base);
    BEMAN_BIG_INT_ASSERT(ec == std::errc{});
    narrow.resize(static_cast<std::size_t>(p - narrow.data()));
#endif

    if constexpr (std::is_same_v<C, char>) {
        return narrow;
    } else {
        return std::basic_string<C>(narrow.begin(), narrow.end());
    }
}

} // namespace detail

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
[[nodiscard]] constexpr std::string to_string(const basic_big_int<b, L, A>& x, const int base = 10) {
    return detail::to_basic_string<char>(x, base);
}

// The overload for an argument the caller has handed over. It renders through the
// `to_chars` overload that consumes its operand, so the repeated-division path
// divides `x` down in place rather than copying its magnitude first.
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
[[nodiscard]] constexpr std::string to_string(basic_big_int<b, L, A>&& x, const int base = 10) {
    return detail::to_basic_string<char>(std::move(x), base);
}

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
[[nodiscard]] constexpr std::wstring to_wstring(const basic_big_int<b, L, A>& x, const int base = 10) {
    return detail::to_basic_string<wchar_t>(x, base);
}

// The wide twin of the consuming `to_string` overload; see above.
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
[[nodiscard]] constexpr std::wstring to_wstring(basic_big_int<b, L, A>&& x, const int base = 10) {
    return detail::to_basic_string<wchar_t>(std::move(x), base);
}

BEMAN_BIG_INT_END_NAMESPACE

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_STRING_HPP
