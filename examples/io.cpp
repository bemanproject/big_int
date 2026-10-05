// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>
#include <string>
#include <version>

#if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
    #include <format>
#endif

auto main() -> int {
    using namespace beman::big_int::literals;
    using beman::big_int::big_int;

    const big_int value = 1_n << 128; // 2^128

    // 1. big_int has no stream inserter of its own. To display a value, convert
    //    it to text with to_string (base 10 by default, or any base up to 36)
    //    and write the result to any output stream.
    const std::string decimal = to_string(value);
    const std::string hex     = to_string(value, 16);

    std::cout << "decimal: " << decimal << "\n"
              << "hex:     " << hex << "\n";

    bool formatted_ok = true;

    // 2. When <format> is available, big_int plugs into std::format just like a
    //    built-in integer: the b/o/x/X bases, the '#' alternate form, the sign
    //    option, and fill / align / width all behave as the standard specifies.
#if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
    const std::string as_hex    = std::format("{:#x}", value);  // 0x1000...0
    const std::string as_binary = std::format("{:b}", 42_n);    // 101010
    const std::string padded    = std::format("{:>12}", 255_n); // right-justified in a width of 12
    const std::string with_sign = std::format("{:+}", 7_n);     // a leading + on a positive value

    std::cout << "format {:#x}:  " << as_hex << "\n"
              << "format {:b}:   " << as_binary << "\n"
              << "format {:>12}: '" << padded << "'\n"
              << "format {:+}:   " << with_sign << "\n";

    formatted_ok =
        as_hex == "0x" + hex && as_binary == "101010" && padded == std::string(9, ' ') + "255" && with_sign == "+7";
#else
    std::cout << "(std::format is unavailable in this configuration)\n";
#endif

    const bool result_is_ok = decimal == "340282366920938463463374607431768211456" && // 2^128
                              hex == "1" + std::string(32, '0') &&                    // 2^128 == 16^32
                              formatted_ok;

    std::cout << "\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
