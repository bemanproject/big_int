// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_LIMITS_HPP
#define BEMAN_BIG_INT_LIMITS_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstddef>
    #include <limits>
    #include <type_traits>
#endif

#include <beman/big_int/big_int.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// The digit counts below are derived exactly as Boost.Multiprecision derives them for cpp_int
inline constexpr double log10_of_2 = 0.301029995663981195213738894724493026768189881462108541310;

// floor(log10(2) * (d - 1)): decimal digits that survive a round trip through the type.
[[nodiscard]] constexpr int limits_digits10(const int d) noexcept {
    return static_cast<int>(log10_of_2 * static_cast<double>(d - 1));
}

// floor(log10(2) * d) + 2: decimal digits needed to tell any two values apart.
// The +2 stands in for the ceil() plus one that round-tripping needs
[[nodiscard]] constexpr int limits_max_digits10(const int d) noexcept {
    return static_cast<int>(log10_of_2 * static_cast<double>(d)) + 2;
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

// `has_denorm` and `has_denorm_loss` are deprecated in C++23.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wdeprecated-declarations")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wdeprecated-declarations")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4996)

// [big.int.limits], numeric limits
//
// `basic_big_int` grows to fit its value, so it has no largest or smallest value to
// report: every value-returning member yields zero, `is_bounded` is false, and `digits`
// saturates at INT_MAX. This mirrors what std::numeric_limits reports for an unbounded
// boost::multiprecision::cpp_int. A real ceiling does exist -- the representation caps
// out at `max_size()` bits -- but it is a property of one object's storage rather than
// of the type, and it is far beyond any reachable allocation.
template <std::size_t b, class L, class A>
class std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::basic_big_int<b, L, A>> {

    using type = BEMAN_BIG_INT_NAMESPACE::basic_big_int<b, L, A>;

    // Every value-returning member below returns a default-constructed `type`, so each
    // is exactly as noexcept as that constructor: unconditionally so for the default
    // allocator, and otherwise as the allocator's own default constructor allows.
    static constexpr bool nothrow_default = std::is_nothrow_default_constructible_v<type>;

  public:
    // Member constants, in the order cppreference and [numeric.limits] list them.
    static constexpr bool is_specialized = true;
    static constexpr bool is_signed      = true;
    static constexpr bool is_integer     = true;
    static constexpr bool is_exact       = true;

    static constexpr bool                    has_infinity      = false;
    static constexpr bool                    has_quiet_NaN     = false;
    static constexpr bool                    has_signaling_NaN = false;
    static constexpr std::float_denorm_style has_denorm        = std::denorm_absent;
    static constexpr bool                    has_denorm_loss   = false;

    // Division truncates toward zero, matching `div_rem_to_zero`.
    static constexpr std::float_round_style round_style = std::round_toward_zero;

    static constexpr bool is_iec559  = false;
    static constexpr bool is_bounded = false;
    // No wraparound: an operation that would overflow grows the representation instead.
    static constexpr bool is_modulo = false;

    static constexpr int digits       = (std::numeric_limits<int>::max)();
    static constexpr int digits10     = BEMAN_BIG_INT_NAMESPACE::detail::limits_digits10(digits);
    static constexpr int max_digits10 = BEMAN_BIG_INT_NAMESPACE::detail::limits_max_digits10(digits);
    static constexpr int radix        = 2;

    static constexpr int min_exponent   = 0;
    static constexpr int min_exponent10 = 0;
    static constexpr int max_exponent   = 0;
    static constexpr int max_exponent10 = 0;

    static constexpr bool traps           = false;
    static constexpr bool tinyness_before = false;

    // Member functions, likewise in cppreference order. Unbounded: there is no extreme
    // value and nothing floating-point to report, so every one of these yields zero.
    [[nodiscard]] static constexpr type(min)() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type lowest() noexcept(nothrow_default) { return (min)(); }
    [[nodiscard]] static constexpr type(max)() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type epsilon() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type round_error() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type infinity() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type quiet_NaN() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type signaling_NaN() noexcept(nothrow_default) { return type{}; }
    [[nodiscard]] static constexpr type denorm_min() noexcept(nothrow_default) { return type{}; }
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_LIMITS_HPP
