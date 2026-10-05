// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>

auto main() -> int {
    // The user-defined literals live in an inline namespace; a single
    // using-directive makes the _n suffix (and the n suffix where the compiler
    // supports it) visible.
    using namespace beman::big_int::literals;

    // 1. A decimal literal far larger than any built-in integer type. Digit
    //    separators (') may be placed between digits and are ignored.
    const auto big = 340'282'366'920'938'463'463'374'607'431'768'211'457_n; // 2^128 + 1

    // 2. The base follows the usual integer-literal prefixes: 0x for
    //    hexadecimal, 0b for binary, and a leading 0 for octal. Each of the
    //    three literals below names the value 255.
    const auto from_hex = 0xFF_n;
    const auto from_bin = 0b1111'1111_n;
    const auto from_oct = 0377_n;

    // 3. A literal is never negative; apply unary minus to obtain a negative
    //    value.
    const auto negative = -1'000'000'000'000'000'000'000_n;

    // 4. The suffix may be capitalized: _N delegates to _n (as N does to n), so
    //    it yields the same value and the same type. Prefer the underscored
    //    spellings, which every supported compiler accepts.
    const auto upper_case = 0xDEAD'BEEF_N;

    const bool big_is_ok    = big == (1_n << 128) + 1_n;
    const bool bases_agree  = from_hex == 255_n && from_bin == 255_n && from_oct == 255_n;
    const bool sign_is_ok   = negative < 0_n;
    const bool cases_agree  = upper_case == 0xdead'beef_n;
    const bool result_is_ok = big_is_ok && bases_agree && sign_is_ok && cases_agree;

    std::cout << "big:      " << to_string(big) << "\n"
              << "from_hex: " << to_string(from_hex) << "\n"
              << "negative: " << to_string(negative) << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
