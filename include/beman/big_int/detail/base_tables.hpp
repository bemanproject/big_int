// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BASE_TABLES_HPP
#define BEMAN_BIG_INT_BASE_TABLES_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <array>
    #include <bit>
    #include <cstddef>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

[[nodiscard]] consteval unsigned char limb_max_input_digits_naive(const int base) {
    BEMAN_BIG_INT_ASSERT(base >= 2);
    const auto ubase = static_cast<uint_multiprecision_t>(base);
    if (std::has_single_bit(ubase)) {
        return detail::width_v<uint_multiprecision_t> / static_cast<unsigned char>(std::countr_zero(ubase));
    }

    uint_multiprecision_t x      = 1;
    int                   result = 0;
    while (true) {
        const auto [product, overflow] = overflowing_mul(x, ubase);
        if (overflow) {
            return static_cast<unsigned char>(product == 0 ? result + 1 : result);
        }
        x = product;
        ++result;
        BEMAN_BIG_INT_ASSERT(product != 0);
    }
}

inline constexpr auto limb_max_input_digits_table = []() consteval {
    std::array<unsigned char, 37> result{};
    for (std::size_t i = 2; i < result.size(); ++i) {
        result[i] = limb_max_input_digits_naive(static_cast<int>(i));
    }
    return result;
}();

// Returns the amount of digits that `uint_multiprecision_t` can represent in the given base.
// Mathematically, this is `floor(log(pow(2, width_v<uint_multiprecision_t>)) / log(base))`.
[[nodiscard]] constexpr int limb_max_input_digits(const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    return limb_max_input_digits_table[std::size_t(base)];
}

[[nodiscard]] consteval uint_multiprecision_t limb_pow_naive(const uint_multiprecision_t x, const int y) {
    uint_multiprecision_t result = 1;
    for (int i = 0; i < y; ++i) {
        result *= x;
    }
    return result;
}

inline constexpr auto limb_max_power_table = []() consteval {
    std::array<uint_multiprecision_t, 37> result{};
    for (std::size_t i = 2; i < result.size(); ++i) {
        const int max_exponent = limb_max_input_digits(static_cast<int>(i));
        result[i]              = limb_pow_naive(i, max_exponent);
    }
    return result;
}();

// Returns the greatest power of `base` representable in `uint_multiprecision_t`,
// or zero if the next greater power is exactly `pow(2, width_v<uint_multiprecision_t>)`.
//
// A result of zero essentially communicates that no bit of `std::uint64_t` is wasted,
// such as in the base-2 or base-16 case.
[[nodiscard]] constexpr uint_multiprecision_t limb_max_power(const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    return limb_max_power_table[std::size_t(base)];
}

// Fixed-point ceil(log2(base)) coefficients in Q7.4 format.
// Each entry stores ceil(log2(base) * 16), i.e. ceil(log2(base)) represented with 4 fractional bits.
inline constexpr std::array<unsigned short, 37> approximate_ceil_mul_log2_q7_4_table{
    0x00, 0x00, 0x10, 0x1A, 0x20, 0x26, 0x2A, 0x2D, 0x30, 0x33, 0x36, 0x38, 0x3A, 0x3C, 0x3D, 0x3F, 0x40, 0x42, 0x43,
    0x44, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4D, 0x4E, 0x4F, 0x50, 0x50, 0x51, 0x52, 0x53, 0x53,
};

// Computes a fast approximation of the amount of bits
// required to represent an integer in base `base` with `digit_count` digits.
// Mathematically, this is `ceil(digit_count * log2(base))`.
// For powers of two, the exact result is returned.
[[nodiscard]] constexpr std::size_t approximate_ceil_mul_log2(const std::size_t digit_count, const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    constexpr std::size_t fractional_bits = 4;
    constexpr std::size_t fractional_mask = (std::size_t{1} << fractional_bits) - 1;

    const auto        coeff = static_cast<std::size_t>(approximate_ceil_mul_log2_q7_4_table[std::size_t(base)]);
    const std::size_t scaled_result = digit_count * coeff;
    return (scaled_result >> fractional_bits) + static_cast<std::size_t>((scaled_result & fractional_mask) != 0);
}

// Fixed-point ceil(1/log2(base)) coefficients in Q0.8 format.
// Each entry stores ceil(256 / log2(base)) as an unsigned char.
// Indices 0, 1, and 2 are unused; base 2 is handled separately in approximate_ceil_div_log2.
inline constexpr std::array<unsigned char, 37> approximate_ceil_div_log2_q0_8_table{
    0x00, 0x00, 0x00, 0xA2, 0x80, 0x6F, 0x64, 0x5C, 0x56, 0x51, 0x4E, 0x4B, 0x48, 0x46, 0x44, 0x42, 0x40, 0x3F, 0x3E,
    0x3D, 0x3C, 0x3B, 0x3A, 0x39, 0x38, 0x38, 0x37, 0x36, 0x36, 0x35, 0x35, 0x34, 0x34, 0x33, 0x33, 0x32, 0x32,
};

// Computes a fast approximation of `ceil(x / log2(base))`.
// For base 2, the exact result is returned.
[[nodiscard]] constexpr std::size_t approximate_ceil_div_log2(const std::size_t x, const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    constexpr std::size_t fractional_bits = 8;
    constexpr std::size_t fractional_mask = (std::size_t{1} << fractional_bits) - 1;

    if (base == 2) {
        return x;
    }
    const auto coeff = static_cast<std::size_t>(approximate_ceil_div_log2_q0_8_table[static_cast<std::size_t>(base)]);
    const std::size_t scaled_result = x * coeff;
    return (scaled_result >> fractional_bits) + static_cast<std::size_t>((scaled_result & fractional_mask) != 0);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_BASE_TABLES_HPP
