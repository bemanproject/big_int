// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iomanip>
#include <iostream>

template <class BigIntType>
constexpr auto factorial(unsigned int n) -> BigIntType {
    return (n <= 1) ? 1 : n * factorial<BigIntType>(n - 1);
}

auto main() -> int {
    using beman::big_int::big_int;

    // Compute the 100th Factorial number.
    const big_int fact_100{factorial<big_int>(100)};

    using namespace beman::big_int::literals;

    const big_int bn_ctrl{
        93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000_n};

    const bool result_is_ok{fact_100 == bn_ctrl};

    std::cout << "fact_100:\n"
              << to_string(fact_100) << "\n\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
