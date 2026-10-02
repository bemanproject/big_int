// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_LITERALS_HPP
#define BEMAN_BIG_INT_LITERALS_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <array>
    #include <charconv>
    #include <cstddef>
    #include <type_traits>
    #include <utility>
#endif

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/charconv.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE

// [big.int.literal]
namespace detail {

// Helper class which assists in removing digit separators from literals.
template <char... digits>
struct literal_buffer {
  private:
    // The sizeof... is necessary here because of:
    // https://github.com/llvm/llvm-project/issues/79750
    static constexpr char        raw_buffer[sizeof...(digits)]{digits...};
    static constexpr const char* raw_end = raw_buffer + sizeof(raw_buffer);

    static consteval std::size_t make_buffer_impl(const char* begin, const char* const end, char* out = nullptr) {
        bool        digit_separator_allowed = false;
        std::size_t length                  = 0;
        for (; begin != end; ++begin) {
            if (*begin == '\'') {
                if (!digit_separator_allowed) {
                    return 0;
                }
                digit_separator_allowed = false;
            } else {
                digit_separator_allowed = true;
                ++length;
                if (out != nullptr) {
                    *out++ = *begin;
                }
            }
        }
        return length;
    }

    static constexpr std::size_t result_length = make_buffer_impl(raw_buffer, raw_end);

    [[nodiscard]] static consteval auto make_buffer() {
        if constexpr (result_length == 0) {
            return std::array<char, 0>{};
        } else {
            std::array<char, result_length> result;
            make_buffer_impl(raw_buffer, raw_end, result.data());
            return result;
        }
    }

  public:
    static constexpr std::array<char, result_length> value = make_buffer();
};

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")
struct parse_non_allocating_result {
    big_int            value;
    big_int::size_type limb_count;
    std::errc          ec;
};
BEMAN_BIG_INT_DIAGNOSTIC_POP()

// Returns the result of parsing a `big_int` using `from_chars_auto_base`.
// However, if the result is too large to fit into inplace storage,
// `{limb_count(), std::errc::result_out_of_range}` is returned.
// Reachable from the exported `operator""n`, so internal linkage (`static`) would make this
// a TU-local exposure in module mode; BEMAN_BIG_INT_INLINE_CONSTEXPR avoids that.
[[nodiscard]] BEMAN_BIG_INT_INLINE_CONSTEXPR parse_non_allocating_result
parse_non_allocating_impl(const char* const begin, const char* const end) {
    // This function is not consteval because of compiler bugs,
    // but should only be called during constant evaluation.
    // https://developercommunity.microsoft.com/t/Nonsensical-error-C2440-when-initializin/11077170

    big_int parsed;
    const auto [p, ec] = from_chars_auto_base(begin, end, parsed);
    if (ec != std::errc{}) {
        return parse_non_allocating_result{0, 0, ec};
    }
    if (p != end) {
        return parse_non_allocating_result{0, 0, std::errc::invalid_argument};
    }
    const auto parsed_size = parsed.representation().size();
    if (parsed.representation_capacity() != big_int::inplace_capacity) {
        return {big_int{}, parsed_size, std::errc::result_out_of_range};
    }
    return {std::move(parsed), parsed_size, std::errc{}};
}

template <std::array buffer>
struct parse_non_allocating {
  private:
    static_assert(std::is_same_v<typename decltype(buffer)::value_type, char>);
    static constexpr parse_non_allocating_result result =
        parse_non_allocating_impl(buffer.data(), buffer.data() + buffer.size());

  public:
    static constexpr big_int   value      = result.value;
    static constexpr auto      limb_count = result.limb_count;
    static constexpr std::errc ec         = result.ec;
};

template <std::size_t limb_count>
[[nodiscard]] consteval std::array<uint_multiprecision_t, limb_count>
literal_operator_n_compute_limbs(const char* const begin, const char* const end) {
    std::array<uint_multiprecision_t, limb_count> result;
    big_int                                       parsed;
    const auto [p, ec] = detail::from_chars_auto_base(begin, end, parsed);
    // We've already parsed this successfully once in parse_non_allocating,
    // so there's no reason it would fail now.
    BEMAN_BIG_INT_ASSERT(p == end);
    BEMAN_BIG_INT_ASSERT(ec == std::errc{});
    BEMAN_BIG_INT_ASSERT(parsed.representation().size() == limb_count);
    std::copy_n(parsed.representation().data(), limb_count, result.data());
    return result;
}

template <std::array buffer>
inline constexpr std::array<uint_multiprecision_t, detail::parse_non_allocating<buffer>::limb_count>
    literal_operator_n_limbs_v = literal_operator_n_compute_limbs<detail::parse_non_allocating<buffer>::limb_count>(
        buffer.data(), buffer.data() + buffer.size());

} // namespace detail

inline namespace literals {
inline namespace big_int_literals {

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wuser-defined-literals")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wreserved-user-defined-literal")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wliteral-suffix")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4455)

// Formatting suppressions are needed to prevent insertion of space between `""` and `n`.
// clang-format off
BEMAN_BIG_INT_EXPORT template <char... digits>
[[nodiscard]] constexpr big_int operator""n()
  noexcept(detail::parse_non_allocating<detail::literal_buffer<digits...>::value>::ec == std::errc{})
  {
    // clang-format on

    // The first step is to do basic validation and to eliminate any digit separators.
    constexpr const auto& buffer = detail::literal_buffer<digits...>::value;
    static_assert(!buffer.empty(), "The given literal is not a valid integer-literal.");

    if constexpr (detail::parse_non_allocating<buffer>::ec == std::errc{}) {
        // The happy case is where the literal's integer value is small enough to fit into big_int without allocations.
        // In that case, we've already parsed the literal, and simply return the pre-computed big_int.
        return detail::parse_non_allocating<buffer>::value;
    } else if constexpr (detail::parse_non_allocating<buffer>::ec == std::errc::result_out_of_range) {
        // The unhappy case is where allocations are required.
        // While we don't have non-transient allocations and cannot store a constexpr big_int,
        // we can precompute a constexpr limb array which is used for fast runtime initialization.
        // We already know the limb count from the previous parsing attempt:
        constexpr const auto& limbs = detail::literal_operator_n_limbs_v<buffer>;
        return big_int(limbs.data(), limbs.data() + limbs.size());
    } else {
        static_assert(detail::parse_non_allocating<buffer>::ec == std::errc::invalid_argument);
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("The given literal is not a valid integer-literal.");
    }
}

// All other literals are just alternate spellings
// UDLs without underscore don't work on Clang:
// https://github.com/llvm/llvm-project/issues/76394
// clang-format off

BEMAN_BIG_INT_EXPORT template <char... digits>
[[nodiscard]] constexpr big_int operator""N() noexcept(noexcept(operator""n<digits...>())) {
    return operator""n<digits...>();
}

BEMAN_BIG_INT_EXPORT template <char... digits>
[[nodiscard]] constexpr big_int operator""_n() noexcept(noexcept(operator""n<digits...>())) {
    return operator""n<digits...>();
}

BEMAN_BIG_INT_EXPORT template <char... digits>
[[nodiscard]] constexpr big_int operator""_N() noexcept(noexcept(operator""n<digits...>())) {
    return operator""n<digits...>();
}

// clang-format on

BEMAN_BIG_INT_DIAGNOSTIC_POP()

} // namespace big_int_literals
} // namespace literals
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_LITERALS_HPP
