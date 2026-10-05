// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

// Generic code asks std::numeric_limits what a type can hold. A bounded type has a
// largest value to report; big_int grows to fit its value and has none, so the
// question has to be asked through is_bounded rather than by reaching straight for
// max(). Specializing numeric_limits is what lets one template serve both.
template <class T>
auto describe_range() -> std::string {
    using lim = std::numeric_limits<T>;
    static_assert(lim::is_specialized && lim::is_integer, "an integer type is required");

    if constexpr (lim::is_bounded) {
        return std::to_string((lim::min)()) + " .. " + std::to_string((lim::max)());
    } else {
        return "unbounded";
    }
}

auto main() -> int {
    using beman::big_int::big_int;
    using namespace beman::big_int::literals;

    using lim = std::numeric_limits<big_int>;

    // 1. big_int is a signed, exact, radix-2 integer type, and numeric_limits says so.
    //    That is what lets a constrained template accept it alongside int.
    const bool classified_as_an_integer =
        lim::is_specialized && lim::is_integer && lim::is_exact && lim::is_signed && lim::radix == 2;

    // 2. It is unbounded, so there is no extreme value to report: min(), max() and
    //    lowest() all yield zero, matching boost::multiprecision::cpp_int. is_bounded
    //    is the member to test; a zero from max() means "no answer", not "the answer".
    const bool unbounded = !lim::is_bounded && (lim::max)() == 0 && (lim::min)() == 0 && lim::lowest() == 0;

    // 3. And unbounded in practice, not just in what it advertises: a value far past
    //    the top of any built-in integer is ordinary.
    const big_int past_every_builtin = 1_n << 4096;
    const bool    really_unbounded =
        past_every_builtin > big_int{(std::numeric_limits<std::uint64_t>::max)()} && past_every_builtin > (lim::max)();

    // 4. digits saturates at INT_MAX instead of reporting a width, and the two decimal
    //    counts are derived from it: digits10 always round-trips, max_digits10 is
    //    enough to tell any two values apart.
    const bool digits_saturate = lim::digits == (std::numeric_limits<int>::max)() && lim::digits10 < lim::max_digits10;

    // 5. Nothing floating-point applies, and arithmetic never wraps: an operation that
    //    would overflow a fixed-width type grows the representation instead.
    const bool integral_behaviour = !lim::has_infinity && !lim::has_quiet_NaN && !lim::is_iec559 && !lim::is_modulo &&
                                    !lim::traps && lim::round_style == std::round_toward_zero;

    const bool result_is_ok =
        classified_as_an_integer && unbounded && really_unbounded && digits_saturate && integral_behaviour;

    std::cout << "int32_t range: " << describe_range<std::int32_t>() << "\n"
              << "big_int range: " << describe_range<big_int>() << "\n\n"
              << "digits:        " << lim::digits << "\n"
              << "digits10:      " << lim::digits10 << "\n"
              << "max_digits10:  " << lim::max_digits10 << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
