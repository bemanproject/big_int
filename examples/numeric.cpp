// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>
#include <limits>
#include <utility>

auto main() -> int {
    using namespace beman::big_int::literals;
    using beman::big_int::abs;
    using beman::big_int::big_int;
    using beman::big_int::gcd;
    using beman::big_int::in_range;
    using beman::big_int::lcm;
    using beman::big_int::midpoint;
    using beman::big_int::saturating_cast;

    // The free numeric helpers live in <beman/big_int/numeric.hpp>.

    // 1. abs returns the magnitude of a value. Because big_int is unbounded it
    //    can never overflow, unlike std::abs on the most negative built-in.
    const big_int negative  = -170141183460469231731687303715884105728_n; // -2^127
    const big_int magnitude = abs(negative);                              // negative is left untouched

    //    Handing the value over instead selects the noexcept overload, which
    //    clears the sign bit on the storage it was given: no limb is copied and
    //    nothing is allocated, however wide the value.
    big_int owned = -170141183460469231731687303715884105728_n;
    owned         = abs(std::move(owned));

    // 2. saturating_cast narrows a big_int to a built-in integer, clamping to
    //    that type's range instead of wrapping or invoking undefined behavior.
    const big_int huge = 1_n << 200; // far larger than any built-in integer

    const int      clamped_hi  = saturating_cast<int>(huge);      // INT_MAX
    const int      clamped_lo  = saturating_cast<int>(-huge);     // INT_MIN
    const unsigned clamped_neg = saturating_cast<unsigned>(-1_n); // 0 (below the minimum)

    // 3. A value that already fits the destination type is returned exactly.
    const int exact = saturating_cast<int>(12345_n);

    // 4. Verify in_range.
    const bool range = (!in_range<std::size_t>(big_int(-1))) && in_range<std::size_t>(big_int(42));

    // 5. gcd works on any mix of big_int and built-in integer arguments, and
    //    always returns a non-negative result.
    const big_int lhs = 1071_n * (1_n << 128);
    const big_int rhs = 462_n * (1_n << 120);

    const big_int divisor = gcd(lhs, rhs);  // 21 * 2^121: the common odd factor times the common power of two
    const big_int mixed   = gcd(lhs, -462); // signs are ignored
    // Dividing both operands by their gcd leaves them coprime.
    const big_int coprime = gcd(lhs / divisor, rhs / divisor);

    // 6. lcm is the companion of gcd: the smallest value that both operands
    //    divide. The two split the product of the operands between them, which is
    //    the identity checked below.
    const big_int multiple  = lcm(lhs, rhs); // 23562 * 2^127
    const big_int mixed_lcm = lcm(rhs, -15); // signs are ignored here too
    const bool    split     = divisor * multiple == lhs * rhs;

    // 7. midpoint returns half the sum of its operands, rounded toward the first
    //    argument when the two are an odd distance apart. big_int is unbounded, so
    //    the overflow that std::midpoint has to work around cannot happen here.
    const big_int between         = midpoint(lhs, rhs); // 137319 * 2^120
    const big_int halfway_to_zero = midpoint(rhs, 0);   // 231 * 2^120
    // An odd gap leaves the exact midpoint half way between two integers; the
    // result is the one on the side of the first argument.
    const bool rounds_toward_first = midpoint(lhs, lhs + 1) == lhs && midpoint(lhs + 1, lhs) == lhs + 1;

    // clang-format off
    const bool result_is_ok =
        magnitude == (1_n << 127) &&
        owned == magnitude &&
        negative == -magnitude &&
        clamped_hi == std::numeric_limits<int>::max() &&
        clamped_lo == std::numeric_limits<int>::min() &&
        clamped_neg == 0U &&
        exact == 12345 &&
        range &&
        divisor == 21_n * (1_n << 121) &&
        mixed == 42 &&
        coprime == 1 &&
        multiple == 23562_n * (1_n << 127) &&
        mixed_lcm == 2310_n * (1_n << 120) &&
        split &&
        between == 137319_n * (1_n << 120) &&
        halfway_to_zero == 231_n * (1_n << 120) &&
        rounds_toward_first;
    // clang-format on

    std::cout << "magnitude:   " << to_string(magnitude) << "\n"
              << "owned:       " << to_string(owned) << "\n"
              << "clamped_hi:  " << clamped_hi << "\n"
              << "clamped_lo:  " << clamped_lo << "\n"
              << "clamped_neg: " << clamped_neg << "\n"
              << "exact:       " << exact << "\n"
              << "range:       " << std::boolalpha << range << "\n"
              << "divisor:     " << to_string(divisor) << "\n"
              << "mixed:       " << to_string(mixed) << "\n"
              << "coprime:     " << to_string(coprime) << "\n"
              << "multiple:    " << to_string(multiple) << "\n"
              << "mixed_lcm:   " << to_string(mixed_lcm) << "\n"
              << "split:       " << std::boolalpha << split << "\n"
              << "between:     " << to_string(between) << "\n"
              << "halfway:     " << to_string(halfway_to_zero) << "\n"
              << "rounds_first:" << std::boolalpha << rounds_toward_first << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
