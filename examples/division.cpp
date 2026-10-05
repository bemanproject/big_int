// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>

auto main() -> int {
    using namespace beman::big_int::literals;
    using beman::big_int::big_int;
    using beman::big_int::div_rem_to_zero;

    // div_rem_to_zero (declared in <beman/big_int/basic_big_int.hpp>) computes the
    // quotient and remainder together in a single division, which is cheaper
    // than evaluating operator/ and operator% separately.

    const big_int dividend = 100'000'000'000'000'000'000'000'000'000'000_n; // 10^32
    const big_int divisor  = 1'000'000'007_n;

    // 1. The result is a div_result aggregate exposing .quotient and .remainder.
    //    Division rounds (truncates) toward zero, so the identity
    //    dividend == quotient * divisor + remainder always holds.
    const auto [quotient, remainder] = div_rem_to_zero(dividend, divisor);

    // 2. For a negative dividend the quotient is negated and the remainder takes
    //    the sign of the dividend, matching the built-in integer operators.
    const auto negative = div_rem_to_zero(-dividend, divisor);

    const bool result_is_ok = quotient * divisor + remainder == dividend && quotient == dividend / divisor &&
                              remainder == dividend % divisor && negative.quotient == -quotient &&
                              negative.remainder == -remainder;

    std::cout << "dividend:  " << to_string(dividend) << "\n"
              << "divisor:   " << to_string(divisor) << "\n"
              << "quotient:  " << to_string(quotient) << "\n"
              << "remainder: " << to_string(remainder) << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
