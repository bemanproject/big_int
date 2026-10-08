// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>

// 3^4000 is about 100 limbs, so its square and the division below go through
// the compiled multiplication and division kernels, not just the headers.
auto main() -> int {
    using beman::big_int::big_int;

    big_int a{1};
    for (int i{0}; i < 4000; ++i) {
        a *= 3;
    }

    const big_int square{a * a};
    const auto    digits{to_string(square).size()};

    std::cout << "beman.big_int: 3^8000 has " << digits << " decimal digits\n";

    return (digits == 3817 && square / a == a && square % a == 0) ? 0 : 1;
}
