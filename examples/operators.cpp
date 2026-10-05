// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <compare>
#include <iostream>

auto main() -> int {
    using namespace beman::big_int::literals;
    using beman::big_int::big_int;

    // 1. The usual binary arithmetic operators are available and never overflow;
    //    the result simply grows to as many limbs as it needs.
    const big_int a          = 1'000'000'000'000_n; // 10^12
    const big_int b          = 7_n;
    const big_int sum        = a + b;
    const big_int difference = a - b;
    const big_int product    = a * b;
    const big_int quotient   = a / b; // division truncates toward zero
    const big_int remainder  = a % b;

    // 2. Mixing a big_int with a built-in integer works too; the built-in
    //    operand is promoted to big_int automatically.
    const big_int mixed = product * 2 + 5;

    // 3. Unary operators: negation, unary plus, and bitwise NOT. On a
    //    two's-complement value, ~x is mathematically -(x + 1).
    const big_int negated    = -a;
    const big_int complement = ~a;

    // 4. Pre- and post-increment / decrement behave as for the built-in types.
    big_int counter = 10_n;
    ++counter;                       // 11
    --counter;                       // 10
    const big_int after = counter++; // after == 10, counter == 11

    // 5. Comparisons give the natural ordering; operator<=> yields a
    //    std::strong_ordering directly.
    const bool ordered   = a > b && b < a && a >= a && a != b;
    const bool spaceship = (a <=> b) == std::strong_ordering::greater;

    // 6. Bitwise and shift operators treat the value as an arbitrarily wide
    //    two's-complement integer.
    const big_int shifted = 1_n << 100;           // 2^100
    const big_int masked  = (shifted - 1) & 0xFF; // low eight bits
    const big_int xored   = shifted ^ shifted;    // 0

    // 7. Every binary operator has a compound-assignment form.
    big_int acc = 2_n;
    acc <<= 10; // 2048
    acc += 1;   // 2049
    acc /= 3;   // 683

    const bool result_is_ok = sum == 1'000'000'000'007_n && difference == 999'999'999'993_n &&
                              product == 7'000'000'000'000_n && quotient == 142'857'142'857_n && remainder == 1_n &&
                              mixed == 14'000'000'000'005_n && negated == -1'000'000'000'000_n &&
                              complement == -1'000'000'000'001_n && after == 10_n && counter == 11_n && ordered &&
                              spaceship && shifted == (1_n << 100) && masked == 255_n && xored == 0_n && acc == 683_n;

    std::cout << "sum:        " << to_string(sum) << "\n"
              << "difference: " << to_string(difference) << "\n"
              << "product:    " << to_string(product) << "\n"
              << "quotient:   " << to_string(quotient) << " remainder " << to_string(remainder) << "\n"
              << "complement: " << to_string(complement) << "\n"
              << "2^100:      " << to_string(shifted) << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
