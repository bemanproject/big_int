// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <array>
#include <iostream>
#include <utility>
#include <version>

auto main() -> int {
    using beman::big_int::big_int;
    using beman::big_int::uint_multiprecision_t;

    // 1. A default-constructed big_int holds the value zero. Small values live
    //    inside the object itself, so no allocation is performed.
    const big_int zero;

    // 2. Construction from a built-in integer is implicit, so a big_int may be
    //    initialized or assigned from one directly. Both the sign and the full
    //    width of the source type are preserved.
    const big_int from_int = -123456789;
    const big_int from_ull = 18446744073709551615ULL; // 2^64 - 1, the widest built-in magnitude

    // 3. Construction from a floating-point value is explicit and truncates
    //    toward zero, exactly like a narrowing conversion to a built-in integer.
    const big_int from_double{1e15}; // 1000000000000000
    const big_int truncated{-42.9};  // -42

    // 4. A user-defined literal builds a value of any size directly in source.
    using namespace beman::big_int::literals;
    const big_int from_literal = 170141183460469231731687303715884105728_n; // 2^127

    // 5. Copies and moves behave as expected. A move transfers the internal
    //    storage, leaving the source in a valid state ready for reuse.
    big_int       source = from_literal;
    const big_int copied{source};
    const big_int moved{std::move(source)};

    // 6. A big_int can also be built from a range of unsigned limbs given in
    //    little-endian order (least significant limb first).
    bool from_limbs_ok = true;
#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    const std::array<uint_multiprecision_t, 2> limbs{0U, 1U}; // 0 + 1 * 2^64
    const big_int                              from_range{std::from_range, limbs};
    from_limbs_ok = from_range == (big_int{1} << 64);
#endif

    const bool result_is_ok = zero == 0 && from_int == -123456789 && from_ull == (big_int{1} << 64) - 1 &&
                              from_double == 1000000000000000 && truncated == -42 &&
                              from_literal == (big_int{1} << 127) && copied == from_literal && moved == from_literal &&
                              from_limbs_ok;

    std::cout << "zero:         " << to_string(zero) << "\n"
              << "from_int:     " << to_string(from_int) << "\n"
              << "from_ull:     " << to_string(from_ull) << "\n"
              << "from_double:  " << to_string(from_double) << "\n"
              << "truncated:    " << to_string(truncated) << "\n"
              << "from_literal: " << to_string(from_literal) << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
