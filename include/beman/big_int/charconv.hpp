// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CHARCONV_HPP
#define BEMAN_BIG_INT_CHARCONV_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <array>
    #include <bit>
    #include <charconv>
    #include <climits>
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <type_traits>
    #include <utility>
#endif

#include <beman/big_int/basic_big_int.hpp>
#include <beman/big_int/detail/base_conversion.hpp>
#include <beman/big_int/detail/base_tables.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

BEMAN_BIG_INT_BEGIN_NAMESPACE

// [big.int.charconv]
namespace detail {

[[nodiscard]] constexpr int digit_value_naive(const char c) noexcept {
    static_assert('A' == 0x41 && 'Z' == 0x5a, "This function requires the ordinary literal encoding to be ASCII.");
    return '0' <= c && c <= '9'   ? c - '0'
           : 'A' <= c && c <= 'Z' ? c - 'A' + 10
           : 'a' <= c && c <= 'z' ? c - 'a' + 10
                                  : -1;
}

inline constexpr auto digit_value_table = []() consteval {
    static_assert(CHAR_BIT == 8);
    std::array<signed char, 256> result;
    for (std::size_t i = 0; i < 256; ++i) {
        result[i] = static_cast<signed char>(digit_value_naive(static_cast<char>(i)));
    }
    return result;
}();

// Returns the value of a digit encoded in `c` using the ordinary literal encoding.
// For example, `digit_value('f')` returns `15`, which is consistent with `std::from_chars`.
// Returns `-1` if the `c` is not alphanumeric.
[[nodiscard]] constexpr int digit_value(const char c) noexcept {
    return digit_value_table[static_cast<std::size_t>(c)];
}

// Shared implementation of the two `to_chars` overloads. `X` deduces to
// `const basic_big_int&` when the caller passed an lvalue and to `basic_big_int`
// when the caller handed the value over; in the latter case the repeated-division
// path below divides that value down in place instead of copying its magnitude.
template <class X>
constexpr std::to_chars_result to_chars_impl(char* const begin, char* const end, X&& x, const int base) {
    using big_int_type                = std::remove_cvref_t<X>;
    using size_type                   = typename big_int_type::size_type;
    using allocator_type              = typename big_int_type::allocator_type;
    constexpr size_type bits_per_limb = big_int_type::bits_per_limb;
    constexpr bool      consume       = !std::is_reference_v<X>;

    BEMAN_BIG_INT_DEBUG_ASSERT(begin);
    BEMAN_BIG_INT_DEBUG_ASSERT(end);
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(base <= 36);

    if (begin == end) {
        return {end, std::errc::value_too_large};
    }

    const size_type width = x.size();
    if (width == 0) {
        *begin = '0';
        return {begin + 1, std::errc{}};
    }

    // Read once: the consuming path below clears the sign on `x` itself.
    const bool is_neg        = x.is_negative();
    char*      current_begin = begin;
    if (is_neg) {
        *current_begin = '-';
        ++current_begin;
    }
    if (current_begin == end) {
        return {end, std::errc::value_too_large};
    }

    // For values that are small enough,
    // the entire implementation (regardless of base) can be delegated to `std::to_chars`.
    {
        constexpr bool ignore_sign = true;
        if (width <= bits_per_limb) {
            // The super happy case is that we can use the std::to_chars implementation for a single limb.
            return std::to_chars(current_begin, end, x.template to<uint_multiprecision_t, ignore_sign>(), base);
        } else if constexpr (bits_per_limb < detail::width_v<std::uintmax_t>) {
            // The slightly less happy case is that we need to use
            // the multiprecision implementation of `std::to_chars`.
            // While we could skip the previous "super happy" check, it may be cheaper to dispatch here
            // because `std::to_chars` would otherwise need to re-check
            // whether it can delegate to a single-limb implementation itself.
            if (width <= detail::width_v<std::uintmax_t>) {
                return std::to_chars(current_begin, end, x.template to<std::uintmax_t, ignore_sign>(), base);
            }
        }
    }

    const uint_multiprecision_t max_pow                  = detail::limb_max_power(base);
    const auto                  max_digits_per_iteration = static_cast<size_type>(detail::limb_max_input_digits(base));
    constexpr const char*       alphabet                 = "0123456789abcdefghijklmnopqrstuvwxyz";

    switch (base) {
    case 2:
    case 4:
    case 8:
    case 16:
    case 32: {
        const auto bits_per_digit     = static_cast<size_type>(std::countr_zero(static_cast<unsigned char>(base)));
        const auto bits_per_iteration = static_cast<size_type>(std::countr_zero(max_pow));
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration != 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration <= bits_per_limb);
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration % bits_per_digit == 0);

        const size_type leading_bits       = width % bits_per_iteration;
        size_type       current_bit_offset = width - leading_bits;

        // First, we need to take care of the leading "head" bits.
        // For example, for octal, we operate on 63 bits at a time,
        // and 2 leading bits are left over.
        if (leading_bits != 0) {
            const uint_multiprecision_t head = x.get_bits_at(current_bit_offset);
            if (head != 0) {
                const std::to_chars_result head_result = std::to_chars(current_begin, end, head, base);
                if (head_result.ec != std::errc{}) {
                    return head_result;
                }
                current_begin = head_result.ptr;
            }
        }

        const auto digit_mask = (uint_multiprecision_t{1} << bits_per_digit) - 1;

        // Once the head digits are printed out, every subsequent block of bits
        // has exactly the same amount of digits.
        // For example, when printing a 128-bit integer in octal, there are 126 bits left,
        // handled exactly 63 bits at a time.

        if (max_pow == 0) {
            // Special case for bases 2, 4, and 16 where we process one limb per iteration.
            BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration == bits_per_limb);
            BEMAN_BIG_INT_DEBUG_ASSERT(current_bit_offset % bits_per_limb == 0);
            BEMAN_BIG_INT_DEBUG_ASSERT(current_bit_offset % bits_per_iteration == 0);

            const auto* const limbs       = x.limb_ptr();
            const size_type   whole_limbs = current_bit_offset / bits_per_limb;
            for (size_type limb_index = whole_limbs; limb_index-- > 0;) {
                const uint_multiprecision_t limb = limbs[limb_index];

                for (size_type i = max_digits_per_iteration; i-- > 0;) {
                    if (current_begin == end) {
                        return {end, std::errc::value_too_large};
                    }
                    const uint_multiprecision_t digit_value = (limb >> (i * bits_per_digit)) & digit_mask;
                    *current_begin                          = alphabet[digit_value];
                    ++current_begin;
                }
            }
        } else {
            // General case for other powers of two.
            BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration < bits_per_limb);
            const auto chunk_mask = (uint_multiprecision_t{1} << bits_per_iteration) - 1;

            while (current_bit_offset >= bits_per_iteration) {
                current_bit_offset -= bits_per_iteration;

                const uint_multiprecision_t chunk = x.get_bits_at(current_bit_offset) & chunk_mask;

                for (size_type i = max_digits_per_iteration; i-- > 0;) {
                    if (current_begin == end) {
                        return {end, std::errc::value_too_large};
                    }
                    const uint_multiprecision_t digit_value = (chunk >> (i * bits_per_digit)) & digit_mask;
                    *current_begin                          = alphabet[digit_value];
                    ++current_begin;
                }
            }
            BEMAN_BIG_INT_DEBUG_ASSERT(current_bit_offset == 0);
        }

        return {current_begin, std::errc{}};
    }

    case 3:
    case 5:
    case 6:
    case 7:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 33:
    case 34:
    case 35:
    case 36: {
        // For magnitudes large enough that the divide-and-conquer ladder
        // engages, the sub-quadratic FastIntegerOutput kernel replaces the
        // repeated short-division loop. Smaller values (and constant
        // evaluation) keep the inline path below.
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            const std::span<const uint_multiprecision_t> value_span{x.limb_ptr(), x.limb_count()};
            if (detail::fast_limbs_to_digits_profitable(value_span, base)) {
                // The kernel emits the exact MSD-first digit VALUES; map them to
                // ASCII in place. No reversal -- already most-significant-first.
                auto              alloc = x.get_allocator();
                const std::size_t bound = detail::base_conversion_digit_bound(value_span, base);
                detail::digit_value_buffer<allocator_type> values(alloc, bound);
                const std::span<unsigned char>             vspan = values.span();
                const std::size_t                          n = detail::limbs_to_digits(vspan, value_span, base, alloc);
                if (static_cast<std::size_t>(end - current_begin) < n) {
                    return {end, std::errc::value_too_large};
                }
                for (std::size_t i = 0; i < n; ++i) {
                    current_begin[i] = alphabet[vspan[i]];
                }
                return {current_begin + n, std::errc{}};
            }
        }

        // An lvalue argument has to survive the call, so its magnitude is copied;
        // a value the caller handed over is divided down where it already lives.
        std::conditional_t<consume, big_int_type&, big_int_type> remainder = x;
        remainder.unchecked_set_sign(false);
        // Zero should have been handled above already.
        BEMAN_BIG_INT_DEBUG_ASSERT(!remainder.is_zero());

        // Process max_digits_per_iteration digits at a time by dividing by max_pow.
        // std::to_chars writes each remainder's digits most-significant-first;
        // we reverse each chunk immediately after writing (undoing the most-significant-first order),
        // then pad to exactly max_digits_per_iteration characters.
        // The final big reverse at the end converts the accumulated least-significant-first chunks
        // back into the correct most-significant-first output.
        while (remainder.limb_count() > 1) {
            if (end - current_begin < static_cast<std::ptrdiff_t>(max_digits_per_iteration)) {
                return {end, std::errc::value_too_large};
            }
            constexpr bool              divisor_neg = false;
            const uint_multiprecision_t r_limb =
                remainder.divmod_in_place_short(max_pow, divisor_neg, detail::division_op::div_rem);

            const std::to_chars_result chunk_result =
                std::to_chars(current_begin, current_begin + max_digits_per_iteration, r_limb, base);
            BEMAN_BIG_INT_DEBUG_ASSERT(chunk_result.ec == std::errc{});

            const auto written = static_cast<size_type>(chunk_result.ptr - current_begin);
            std::reverse(current_begin, chunk_result.ptr);
            std::fill_n(chunk_result.ptr, max_digits_per_iteration - written, '0');

            current_begin += max_digits_per_iteration;
        }

        BEMAN_BIG_INT_DEBUG_ASSERT(remainder.limb_count() == 1);
        const std::to_chars_result final_result = std::to_chars(current_begin, end, remainder.limb_ptr()[0], base);
        if (final_result.ec != std::errc{}) {
            return final_result;
        }
        std::reverse(current_begin, final_result.ptr);
        // No zero-pad is needed for the final chunk because that would put leading zeros
        // in front of the entire result.
        current_begin = final_result.ptr;

        // We wrote all the digits in reverse order.
        // Everything except the leading minus sign (if any) needs to be reversed.
        std::reverse(begin + (is_neg ? 1 : 0), current_begin);
        return {current_begin, std::errc{}};
    }

    default:
        break;
    }

    // Invalid base; earlier checks should have caught this already, but let's make sure.
    BEMAN_BIG_INT_ASSERT(false);
}

} // namespace detail

// Writes `x` in the given base into `[begin, end)`. `x` is left unchanged.
template <std::size_t b, class L, class A>
constexpr std::to_chars_result
to_chars(char* const begin, char* const end, const basic_big_int<b, L, A>& x, const int base) {
    return detail::to_chars_impl(begin, end, x, base);
}

// The overload for an argument the caller has handed over: the repeated-division
// path renders `x` by dividing it down in place, so no copy of the magnitude is
// made. Every other path reads `x` without touching it.
template <std::size_t b, class L, class A>
constexpr std::to_chars_result
to_chars(char* const begin, char* const end, basic_big_int<b, L, A>&& x, const int base) {
    return detail::to_chars_impl(begin, end, std::move(x), base);
}

template <std::size_t b, class L, class A>
[[nodiscard]] constexpr std::from_chars_result
from_chars(const char* const begin, const char* const end, basic_big_int<b, L, A>& out, const int base) {
    using size_type = typename basic_big_int<b, L, A>::size_type;

    if (begin == nullptr || begin == end || base < 2 || base > 36) {
        return {end, std::errc::invalid_argument};
    }

    const char* current_begin = *begin == '-' ? begin + 1 : begin;
    if (current_begin == end) {
        return {end, std::errc::invalid_argument};
    }

    const uint_multiprecision_t max_pow                  = detail::limb_max_power(base);
    const std::ptrdiff_t        max_digits_per_iteration = detail::limb_max_input_digits(base);
    const bool                  is_pow_2                 = (base & (base - 1)) == 0;
    BEMAN_BIG_INT_DEBUG_ASSERT(max_pow != 0 || is_pow_2);

    const char* current_end = current_begin;
    for (; current_end != end; ++current_end) {
        const int value = detail::digit_value(*current_end);
        if (value < 0 || value >= base) {
            break;
        }
    }
    // This indicates that we have parsed either nothing or only the minus sign:
    if (current_end == current_begin) {
        return {end, std::errc::invalid_argument};
    }
    const char* const returned_end   = current_end;
    const auto        digit_count    = static_cast<size_type>(current_end - current_begin);
    const auto        bits_per_digit = static_cast<size_type>(std::countr_zero(static_cast<unsigned char>(base)));

    if (max_pow == 0) {
        // Special powers of two (2, 4, 16): one digit block maps exactly to one limb.
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_digit != 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(detail::width_v<uint_multiprecision_t> % bits_per_digit == 0);

        const auto digits_per_limb = detail::width_v<uint_multiprecision_t> / bits_per_digit;
        const auto limbs_needed    = detail::div_to_pos_inf(digit_count, digits_per_limb);

        out.set_zero();
        out.grow(limbs_needed);

        auto* const     out_limbs      = out.limb_ptr();
        std::uint32_t   out_limb_index = 0;
        const size_type block_len      = static_cast<size_type>(digits_per_limb);

        while (true) {
            const auto digit_block_length =
                std::min(current_end - current_begin, static_cast<std::ptrdiff_t>(block_len));
            const char* const digit_block_begin = current_end - digit_block_length;
            BEMAN_BIG_INT_DEBUG_ASSERT(digit_block_length != 0);

            // We already validated the digit run and bounded each block to one limb.
            // Pack the digit blocks ourselves for up to 6x performance improvement with libc++,
            // since they don't have optimizations for any of the power of two cases.
            // Libstdc++ does so we see a tiny improvement due to reduced overhead
            uint_multiprecision_t limb = 0;
            for (const char* digit = digit_block_begin; digit != current_end; ++digit) {
                const int value = detail::digit_value(*digit);
                BEMAN_BIG_INT_DEBUG_ASSERT(value >= 0 && value < base);
                limb = (limb << bits_per_digit) | static_cast<uint_multiprecision_t>(value);
            }
            out_limbs[out_limb_index] = limb;
            ++out_limb_index;

            if (digit_block_begin == current_begin) {
                break;
            }
            current_end = digit_block_begin;
        }

        out.unchecked_set_limb_count(out_limb_index);
        // This trim may be necessary if we have parsed leading zeros in the string.
        out.unchecked_trim_magnitude();
        if (*begin == '-') {
            out.negate();
        }
        return {returned_end, std::errc{}};
    }

    if (is_pow_2) {
        // When the base is a power of two, parsing is significantly different.
        // Namely, it takes place FROM LAST TO FIRST, i.e. starting with the least significant digit.
        // This is done so that the parsed blocks of digits can simply be appended to the representation.
        // Parsing in this way essentially involves no multiprecision operations,
        // not even a multiprecision shift.
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_digit != 0);
        const auto limbs_needed =
            detail::div_to_pos_inf(digit_count * bits_per_digit, detail::width_v<uint_multiprecision_t>);
        const auto         bits_per_iteration = static_cast<big_int::size_type>(std::countr_zero(max_pow));
        big_int::size_type bit_offset         = 0;

        // In any other case, we have the guarantee that at least one digit can be parsed.
        out.set_zero();
        out.grow(limbs_needed);
        // The digit blocks are OR-ed in, so a reused destination must not keep stale limbs.
        std::fill_n(out.limb_ptr(), limbs_needed, uint_multiprecision_t{0});
        while (true) {
            const auto        digit_block_length = std::min(current_end - current_begin, max_digits_per_iteration);
            const char* const digit_block_begin  = current_end - digit_block_length;
            BEMAN_BIG_INT_DEBUG_ASSERT(digit_block_length != 0);

            // Pack the digit block (most-significant-first) with shift/or; see
            // the max_pow == 0 case above for why std::from_chars is not used.
            uint_multiprecision_t bits = 0;
            for (const char* digit = digit_block_begin; digit != current_end; ++digit) {
                const int value = detail::digit_value(*digit);
                BEMAN_BIG_INT_DEBUG_ASSERT(value >= 0 && value < base); // pre-validated by the scan above
                bits = (bits << bits_per_digit) | static_cast<uint_multiprecision_t>(value);
            }

            out.unchecked_init_magnitude_bits_at(bits, bit_offset);
            bit_offset += bits_per_iteration;

            if (digit_block_begin == current_begin) {
                break;
            }
            current_end -= digit_block_length;
            BEMAN_BIG_INT_DEBUG_ASSERT(current_end >= digit_block_begin);
        }
        // This trim may be necessary if we have parsed leading zeros in the string.
        out.unchecked_trim_magnitude();
        if (*begin == '-') {
            out.negate();
        }
        return {returned_end, std::errc{}};
    }

    // This is the "usual case" for all bases that are not powers of two.
    // Since every added digit can affect any existing bit in theory,
    // the problem can't be solved in blocks of bits.
    // Instead, we need to use the classic multiplication and addition approach.
    //
    // However, instead of doing so naively digit-by-digit,
    // each iteration, we can process as many digit as possible without exceeding
    // the greatest representable power in the base.
    //
    // For example, if `base` is `10`, instead of doing parsing base 10,
    // we do it base `1'000'000'000` assuming `uint_multiprecision_t` is 32-bit.
    //
    // For inputs large enough that the divide-and-conquer ladder engages, the
    // sub-quadratic FastIntegerInput kernel replaces the Horner loop. Smaller
    // inputs (and constant evaluation) keep the inline path below, whose fixed
    // overhead is lower than the kernel's temp buffer plus owned scratch arena.
    // The kernel also allocates, so it runs only when the value cannot fit the
    // in-place storage anyway; values that fit keep the zero-allocation path.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (detail::fast_digits_to_limbs_profitable(static_cast<std::size_t>(digit_count), base) &&
            detail::base_conversion_limb_bound(static_cast<std::size_t>(digit_count), base) >
                basic_big_int<b, L, A>::inplace_capacity) {
            // Transcode the already-validated ASCII run into MSD-first digit
            // VALUES (0..base-1), then run the kernel straight into out's limbs.
            auto                           alloc = out.get_allocator();
            detail::digit_value_buffer<A>  values(alloc, static_cast<std::size_t>(digit_count));
            const std::span<unsigned char> vspan = values.span();
            for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(digit_count); ++i) {
                vspan[static_cast<std::size_t>(i)] = static_cast<unsigned char>(detail::digit_value(current_begin[i]));
            }
            const std::size_t bound = detail::base_conversion_limb_bound(static_cast<std::size_t>(digit_count), base);
            out.set_zero();
            out.grow(bound);
            const std::size_t count =
                detail::digits_to_limbs(std::span<uint_multiprecision_t>{out.limb_ptr(), bound}, vspan, base, alloc);
            out.unchecked_set_limb_count(static_cast<std::uint32_t>(count));
            out.unchecked_trim_magnitude();
            if (*begin == '-') {
                out.negate();
            }
            return {returned_end, std::errc{}};
        }
    }

    // Parse the valid digit run in blocks that fit into `uint_multiprecision_t`.
    // The first block may be shorter so that all following blocks have the same width,
    // then the accumulated result is built left-to-right by multiplying by `max_pow` before adding each next block.
    {
        const std::size_t bits_needed_upper_bound = detail::approximate_ceil_mul_log2(digit_count, base);
        const std::size_t limbs_needed_upper_bound =
            detail::div_to_pos_inf(bits_needed_upper_bound, detail::width_v<uint_multiprecision_t>);
        out.grow(limbs_needed_upper_bound);
    }

    // clang-format off
    const std::ptrdiff_t first_block_length = (current_end - current_begin) % max_digits_per_iteration == 0
                                            ? max_digits_per_iteration
                                            : (current_end - current_begin) % max_digits_per_iteration;
    // clang-format on

    uint_multiprecision_t        leading_digits{};
    const std::from_chars_result first_result =
        std::from_chars(current_begin, current_begin + first_block_length, leading_digits, base);
    BEMAN_BIG_INT_DEBUG_ASSERT(first_result.ec == std::errc{});
    out = leading_digits;

    for (const char* current_first = current_begin + first_block_length; current_first != current_end;
         current_first += max_digits_per_iteration) {
        const char* const current_last = std::min(current_first + max_digits_per_iteration, current_end);

        uint_multiprecision_t        digits         = {};
        const std::from_chars_result partial_result = std::from_chars(current_first, current_last, digits, base);
        // Same reasoning as for powers of two; see above.
        BEMAN_BIG_INT_DEBUG_ASSERT(partial_result.ec == std::errc{});
        BEMAN_BIG_INT_DEBUG_ASSERT(digits < max_pow);

        out *= max_pow;
        out += digits;
    }

    if (*begin == '-') {
        out.negate();
    }
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        out.shrink_to_fit();
    }
    return {returned_end, std::errc{}};
}

namespace detail {

using std::from_chars;

// Detects the base automatically based on the `0x`, `0b`, or `0` prefix
// and dispatches to `from_chars` with the identified base.
// Leading minus signs are not supported.
template <class T>
[[nodiscard]] constexpr std::from_chars_result
from_chars_auto_base(const char* const begin, const char* const end, T& out)
    requires requires { from_chars(begin, end, out, 10); }
{
    if (begin == end || *begin < '0' || *begin > '9') {
        return {end, std::errc::invalid_argument};
    }
    if (*begin != '0' || end - begin <= 1) {
        return from_chars(begin, end, out, 10);
    }
    switch (begin[1]) {
    case 'b':
    case 'B':
        return from_chars(begin + 2, end, out, 2);
    // In the future, this will also have a case for 'o'
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p0085r3.html
    case 'x':
    case 'X':
        return from_chars(begin + 2, end, out, 16);
    default:
        break;
    }
    // This case (leading zero for octal) is deprecated,
    // but we have no real way to communicate that and raise a warning here.
    return from_chars(begin, end, out, 8);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_CHARCONV_HPP
