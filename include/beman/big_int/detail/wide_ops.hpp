// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_WIDE_OPS_HPP
#define BEMAN_BIG_INT_WIDE_OPS_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <bit>
#endif

#include <beman/big_int/detail/config.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Maps an integer type to the integer type of twice its width and the same
// signedness. SFINAE-friendly: when no such type exists (e.g. 64 -> 128 on a
// target without any 128-bit integer type), no specialization matches and the
// primary template is left incomplete, so `requires { typename wider_t<T>; }`
// is false instead of a hard error.
template <class T>
struct wider;

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT
template <>
struct wider<int_multiprecision_t> {
    using type = int_wide_t;
};
template <>
struct wider<uint_multiprecision_t> {
    using type = uint_wide_t;
};
#endif

#ifdef BEMAN_BIG_INT_HAS_BITINT
template <std::size_t N>
    requires(2 * N <= BEMAN_BIG_INT_BITINT_MAXWIDTH)
struct wider<bit_int<N>> {
    using type = bit_int<2 * N>;
};
template <std::size_t N>
    requires(2 * N <= BEMAN_BIG_INT_BITINT_MAXWIDTH)
struct wider<bit_uint<N>> {
    using type = bit_uint<2 * N>;
};
#endif

template <signed_integer T>
    requires(width_v<T> == 8 || width_v<T> == 16 || width_v<T> == 32 || (width_v<T> == 64 && has_int128_v))
struct wider<T> {
    [[nodiscard]] static consteval auto select() {
        if constexpr (width_v<T> == 8) {
            return std::int16_t{};
        } else if constexpr (width_v<T> == 16) {
            return std::int32_t{};
        } else if constexpr (width_v<T> == 32) {
            return std::int64_t{};
        }
#ifdef BEMAN_BIG_INT_HAS_INT128
        else if constexpr (width_v<T> == 64) {
            return int128_t{};
        }
#endif
        else {
            BEMAN_BIG_INT_STATIC_ASSERT_FALSE("No wider signed integer type available.");
        }
    }
    using type = decltype(select());
};
template <unsigned_integer T>
    requires(width_v<T> == 8 || width_v<T> == 16 || width_v<T> == 32 || (width_v<T> == 64 && has_int128_v))
struct wider<T> {
    [[nodiscard]] static consteval auto select() {
        if constexpr (width_v<T> == 8) {
            return std::uint16_t{};
        } else if constexpr (width_v<T> == 16) {
            return std::uint32_t{};
        } else if constexpr (width_v<T> == 32) {
            return std::uint64_t{};
        }
#ifdef BEMAN_BIG_INT_HAS_INT128
        else if constexpr (width_v<T> == 64) {
            return uint128_t{};
        }
#endif
        else {
            BEMAN_BIG_INT_STATIC_ASSERT_FALSE("No wider unsigned integer type available.");
        }
    }
    using type = decltype(select());
};

// Denotes the integer type with twice the width of `T`
// and with the same signedness.
template <signed_or_unsigned T>
using wider_t = typename wider<T>::type;

// Modeled if a doubled-width counterpart of `T` exists. Unlike a bare
// requires-expression with a concrete type, a concept-id is always checked
// via substitution, so this is usable with non-dependent arguments.
template <class T>
concept has_wider = requires { typename wider_t<T>; };

template <signed_or_unsigned T>
struct wide {
    T low_bits;
    T high_bits;

    [[nodiscard]] friend constexpr bool operator==(const wide& x, const wide& y) noexcept = default;
};

template <signed_or_unsigned T>
    requires has_wider<T>
struct wide<T> {
    T low_bits;
    T high_bits;

    [[nodiscard]] friend constexpr bool operator==(const wide& x, const wide& y) noexcept = default;

    // The split/join fallbacks below also serve constant evaluation on the
    // little-endian path: constexpr bit_cast involving _BitInt is not
    // supported on every compiler (e.g. AppleClang).
    [[nodiscard]] static constexpr wide from_int(wider_t<T> x) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
                return std::bit_cast<wide>(x);
            }
        }
        return {
            .low_bits  = static_cast<T>(x),
            .high_bits = static_cast<T>(x >> width_v<T>),
        };
    }

    [[nodiscard]] constexpr wider_t<T> to_int() const noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
                return std::bit_cast<wider_t<T>>(*this);
            }
        }
        return (static_cast<wider_t<T>>(high_bits) << width_v<T>) | low_bits;
    }
};

namespace high_mul_detail {

template <signed_or_unsigned T>
    requires(width_v<T> == 64)
constexpr T high_mul_portable(const T x, const T y) noexcept {
    if constexpr (std::is_signed_v<T>) {
        // https://stackoverflow.com/a/28904636/5740428
        const T a_lo = static_cast<unsigned>(x);
        const T a_hi = static_cast<unsigned>(x >> 32);
        const T b_lo = static_cast<unsigned>(y);
        const T b_hi = static_cast<unsigned>(y >> 32);

        const T a_x_b_hi  = a_hi * b_hi;
        const T a_x_b_mid = a_hi * b_lo;
        const T b_x_a_mid = b_hi * a_lo;
        const T a_x_b_lo  = a_lo * b_lo;

        const T carry_bit = (static_cast<T>(static_cast<unsigned>(a_x_b_mid)) + //
                             static_cast<T>(static_cast<unsigned>(b_x_a_mid)) + //
                             (a_x_b_lo >> 32))                                  //
                            >> 32;

        return a_x_b_hi + (a_x_b_mid >> 32) + (b_x_a_mid >> 32) + carry_bit;
    } else {
        const T a_lo = static_cast<std::uint32_t>(x);
        const T a_hi = x >> 32;
        const T b_lo = static_cast<std::uint32_t>(y);
        const T b_hi = y >> 32;

        const T ll = a_lo * b_lo;
        const T lh = a_lo * b_hi;
        const T hl = a_hi * b_lo;
        const T hh = a_hi * b_hi;

        const T mid = (ll >> 32) + static_cast<std::uint32_t>(lh) + static_cast<std::uint32_t>(hl);
        return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    }
}

} // namespace high_mul_detail

// Returns the high 64 bits of the multiplication `x * y`.
template <signed_or_unsigned T>
    requires(width_v<T> <= 64)
constexpr T high_mul(const T x, const T y) noexcept {
#ifdef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
    return (x * static_cast<wider_t<T>>(y)) >> width_v<T>;
#else
    if constexpr (width_v<T> <= 32) {
        return (x * static_cast<wider_t<T>>(y)) >> width_v<T>;
    } else {
            // MSVC intrinsics are not usable during constant evaluation, so fall through
            // to the portable path when we're in a consteval context.
    #ifdef BEMAN_BIG_INT_INTRINSIC_UMULH
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if constexpr (std::is_signed_v<T>) {
                return BEMAN_BIG_INT_INTRINSIC_MULH(x, y);
            } else {
                return BEMAN_BIG_INT_INTRINSIC_UMULH(x, y);
            }
        }
    #endif
        if constexpr (width_v<T> == 64) {
            return high_mul_detail::high_mul_portable(x, y);
        } else {
            static_assert(width_v<unsigned long long> == 64);
            return high_mul<unsigned long long>(x, y);
        }
    }
#endif // BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
}

// Returns both the low and the high bits of the multiplication `x * y`.
template <signed_or_unsigned T>
    requires(width_v<T> <= 64)
[[nodiscard]] constexpr wide<T> widening_mul(const T x, const T y) noexcept {
    // Based on mul_wide from P3161R4, but with different names.
    // P4052R0 renamed "mul_sat" to "saturating_mul",
    // and the corresponding Rust-style rename for "mul_wide" is "widening_mul".
    //
    // Currently, it would be fine if we just returned an integer,
    // but that enshrines the assumption that we have a 128-bit integer type everywhere.
    // Also, we often need to break up the result into limbs anyway.

#ifdef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
    const auto product = static_cast<wider_t<T>>(x) * static_cast<wider_t<T>>(y);
    return wide<T>::from_int(product);
#else
    if constexpr (width_v<T> <= 32) {
        const auto product = static_cast<wider_t<T>>(x) * static_cast<wider_t<T>>(y);
        return wide<T>::from_int(product);
    } else {
    #ifdef BEMAN_BIG_INT_INTRINSIC_UMUL128
        // MSVC intrinsics are not usable during constant evaluation, so fall through
        // to the portable path when we're in a consteval context.
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if constexpr (std::is_signed_v<T>) {
                std::int64_t high;
                std::int64_t low = BEMAN_BIG_INT_INTRINSIC_MUL128(x, y, &high);
                return {low, high};
            } else {
                std::uint64_t high;
                std::uint64_t low = BEMAN_BIG_INT_INTRINSIC_UMUL128(x, y, &high);
                return {low, high};
            }
        }
    #endif
        using U = detail::make_unsigned_t<T>;
        return {
            .low_bits  = static_cast<T>(static_cast<U>(x) * static_cast<U>(y)),
            .high_bits = high_mul(x, y),
        };
    }
#endif // BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
}

// The cast is needed for less than 16-bit integers,
// since they get implicitly promoted to int which sets off -Wimplicit-int-conversion
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wuseless-cast")

// Design for funnel shifts is similar to P4010R0.

// Returns `x.high_bits << s`,
// except that the low bits are filled with `x.low_bits` instead of zeroes.
template <signed_or_unsigned T>
[[nodiscard]] constexpr T funnel_shl(const wide<T> x, const unsigned s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s < width_v<T>);
#ifdef BEMAN_BIG_INT_BUILTIN_ELEMENTWISE_FSHL
    return BEMAN_BIG_INT_BUILTIN_ELEMENTWISE_FSHL(x.high_bits, x.low_bits, static_cast<T>(s));
#else
    if (s == 0) {
        return x.high_bits;
    }
    return static_cast<T>((x.high_bits << s) | (x.low_bits >> (width_v<T> - s)));
#endif
}

BEMAN_BIG_INT_DIAGNOSTIC_POP()

// Returns `x.low_bits >> s`,
// except that the high bits are filled with `x.high_bits` instead sign bits.
template <signed_or_unsigned T>
[[nodiscard]] constexpr T funnel_shr(const wide<T> x, const unsigned s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s < width_v<T>);
#ifdef BEMAN_BIG_INT_BUILTIN_ELEMENTWISE_FSHR
    return BEMAN_BIG_INT_BUILTIN_ELEMENTWISE_FSHR(x.high_bits, x.low_bits, static_cast<T>(s));
#else
    // It usually makes sense to implement this in terms of a right-shift of a wider type.
    // However, the 128-bit version optimizes poorly;
    // Clang recognizes the else case as a funnel shift and optimizes that better.
    // In general, the 128-bit shift provides little benefit here.
    if constexpr (width_v<T> < 64 && requires { x.to_int(); }) {
        return static_cast<T>(x.to_int() >> s);
    } else {
        if (s == 0) {
            return x.low_bits;
        }
        return (x.low_bits >> s) | (x.high_bits << (width_v<T> - s));
    }
#endif
}

// These are going to be the standardized forms.
// Padding is expected and acceptable.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

template <class T>
struct overflow_result {
    T    value;
    bool overflow;
};

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_add(const T x, const T y) noexcept {
#ifdef BEMAN_BIG_INT_BUILTIN_ADD_OVERFLOW
    T    value;
    bool overflow = BEMAN_BIG_INT_BUILTIN_ADD_OVERFLOW(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    if constexpr (std::is_unsigned_v<T>) {
        // Compare the truncated sum: types narrower than int promote, so x + y itself never wraps.
        const T value = static_cast<T>(x + y);
        return {.value = value, .overflow = value < x};
    } else {
        const auto wide  = static_cast<wider_t<T>>(x) + static_cast<wider_t<T>>(y);
        const auto value = static_cast<T>(wide);
        return {.value = value, .overflow = wide != value};
    }
#endif // BEMAN_BIG_INT_BUILTIN_ADD_OVERFLOW
}

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_sub(const T x, const T y) noexcept {
#ifdef BEMAN_BIG_INT_BUILTIN_SUB_OVERFLOW
    T    value;
    bool overflow = BEMAN_BIG_INT_BUILTIN_SUB_OVERFLOW(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    if constexpr (std::is_unsigned_v<T>) {
        return {.value = static_cast<T>(x - y), .overflow = x < y};
    } else {
        const auto wide  = static_cast<wider_t<T>>(x) - static_cast<wider_t<T>>(y);
        const auto value = static_cast<T>(wide);
        return {.value = value, .overflow = wide != value};
    }
#endif // BEMAN_BIG_INT_BUILTIN_SUB_OVERFLOW
}

template <unsigned_integer T>
[[nodiscard]] constexpr overflow_result<T> overflowing_mul(const T x, const T y) noexcept {
#ifdef BEMAN_BIG_INT_BUILTIN_MUL_OVERFLOW
    T    value;
    bool overflow = BEMAN_BIG_INT_BUILTIN_MUL_OVERFLOW(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    const T value = x * y;
    return {.value = value, .overflow = x != 0 && value / x != y};
#endif // BEMAN_BIG_INT_BUILTIN_MUL_OVERFLOW
}

template <class T>
struct carry_result {
    T    value;
    bool carry;
};

template <unsigned_integer T>
[[nodiscard]] constexpr carry_result<T>
carrying_add_portable(const T x, const T y, const bool carry = false) noexcept {
    const auto [sum_x_y, carry_x_y] = overflowing_add(x, y);
    const auto [sum_out, carry_out] = overflowing_add(sum_x_y, static_cast<T>(carry));
    return {.value = sum_out, .carry = carry_x_y || carry_out};
}

template <unsigned_integer T>
[[nodiscard]] constexpr carry_result<T> carrying_add(const T x, const T y, const bool carry = false) noexcept {
#ifdef BEMAN_BIG_INT_BUILTIN_ADDC
    #ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_ADDC
    if constexpr (true)
    #else
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL
    #endif
    {
        if constexpr (width_v<T> == width_v<unsigned>) {
            unsigned carry_out;
            unsigned value = BEMAN_BIG_INT_BUILTIN_ADDC(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long>) {
            unsigned long carry_out;
            unsigned long value = BEMAN_BIG_INT_BUILTIN_ADDCL(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long long>) {
            unsigned long long carry_out;
            unsigned long long value = BEMAN_BIG_INT_BUILTIN_ADDCLL(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        } else {
            return carrying_add_portable(x, y, carry);
        }
    } else {
        return carrying_add_portable(x, y, carry);
    }
#elif defined(BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U32)
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        // Theoretically we could use the ADX intrinsic, but then the user always has to compile for it
        // This is more portable.
        // Each branch returns directly, so we don't share uninitialized state
        // across the `if constexpr` chain.
        if constexpr (width_v<T> == 8) {
            std::uint8_t        value;
            const unsigned char carry_out =
                BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U8(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == 16) {
            std::uint16_t       value;
            const unsigned char carry_out =
                BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U16(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == 32) {
            std::uint32_t       value;
            const unsigned char carry_out =
                BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U32(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        }
    #ifdef BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U64
        else if constexpr (width_v<T> == 64) {
            std::uint64_t       value;
            const unsigned char carry_out =
                BEMAN_BIG_INT_INTRINSIC_ADDCARRY_U64(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        }
    #endif
        else {
            return carrying_add_portable(x, y, carry);
        }
    } else {
        return carrying_add_portable(x, y, carry);
    }
#else
    return carrying_add_portable(x, y, carry);
#endif
}

template <class T>
struct borrow_result {
    T    value;
    bool borrow;
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

template <unsigned_integer T>
[[nodiscard]] constexpr borrow_result<T>
borrowing_sub_portable(const T x, const T y, const bool borrow = false) noexcept {
    const auto [diff_x_y, borrow_x_y] = overflowing_sub(x, y);
    const auto [diff_out, borrow_out] = overflowing_sub(diff_x_y, static_cast<T>(borrow));
    return {.value = diff_out, .borrow = borrow_x_y || borrow_out};
}

template <unsigned_integer T>
[[nodiscard]] constexpr borrow_result<T> borrowing_sub(const T x, const T y, const bool borrow = false) noexcept {
#ifdef BEMAN_BIG_INT_BUILTIN_SUBC
    #ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_SUBC
    if constexpr (true)
    #else
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL
    #endif
    {
        if constexpr (width_v<T> == width_v<unsigned>) {
            unsigned borrow_out;
            unsigned value = BEMAN_BIG_INT_BUILTIN_SUBC(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long>) {
            unsigned long borrow_out;
            unsigned long value = BEMAN_BIG_INT_BUILTIN_SUBCL(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long long>) {
            unsigned long long borrow_out;
            unsigned long long value = BEMAN_BIG_INT_BUILTIN_SUBCLL(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        } else {
            return borrowing_sub_portable(x, y, borrow);
        }
    } else {
        return borrowing_sub_portable(x, y, borrow);
    }
#elif defined(BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U32)
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        // Mirror the `carrying_add` MSVC path using the matching `_subborrow_*` intrinsics.
        // Each branch returns directly, so we don't share uninitialized state across the
        // `if constexpr` chain.
        if constexpr (width_v<T> == 8) {
            std::uint8_t        value;
            const unsigned char borrow_out =
                BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U8(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == 16) {
            std::uint16_t       value;
            const unsigned char borrow_out =
                BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U16(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == 32) {
            std::uint32_t       value;
            const unsigned char borrow_out =
                BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U32(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        }
    #ifdef BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U64
        else if constexpr (width_v<T> == 64) {
            std::uint64_t       value;
            const unsigned char borrow_out =
                BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U64(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        }
    #endif // BEMAN_BIG_INT_INTRINSIC_SUBBORROW_U64
        else {
            return borrowing_sub_portable(x, y, borrow);
        }
    } else {
        return borrowing_sub_portable(x, y, borrow);
    }
#else
    return borrowing_sub_portable(x, y, borrow);
#endif
}

template <signed_or_unsigned T>
struct wide_div_result {
    T       quotient;
    wide<T> remainder;
};

// Portable bit-by-bit restoring long division of the 2-limb unsigned value `x`
// by the single limb `y`, for targets where no wider type backs narrowing_div.
// Precondition: `x.high_bits < y`, so the quotient fits in a single limb.
template <unsigned_integer T>
[[nodiscard]] constexpr div_result<T> narrowing_div_portable(const wide<T> x, const T y) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(x.high_bits < y);

    constexpr std::size_t limb_bits = width_v<T>;

    T q = 0;
    T r = x.high_bits;

    for (std::size_t i = limb_bits; i-- > 0;) {
        // The shifted remainder can spill into a virtual top bit; if it does,
        // it certainly exceeds y, and the truncated subtraction is still exact.
        const T r_top = static_cast<T>(r >> (limb_bits - 1));
        r             = static_cast<T>((r << 1) | ((x.low_bits >> i) & T{1}));
        q <<= 1;
        if (r_top != 0 || r >= y) {
            r = static_cast<T>(r - y);
            q |= T{1};
        }
    }
    return {.quotient = q, .remainder = r};
}

// Returns the quotient and remainder of the division `x / y`.
// The behavior is undefined if the quotient is not representable as `T`,
// which is the case if and only if `x.high_bits < y`.
template <unsigned_integer T>
[[nodiscard]] constexpr div_result<T> narrowing_div(const wide<T> x, const T y) noexcept {
    const bool quotient_fits = x.high_bits < y;
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        BEMAN_BIG_INT_ASSERT(quotient_fits);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(quotient_fits);
    }

#if BEMAN_BIG_INT_LIMB_WIDTH == 64
    // For 64-bit, there potentially exists the optimization opportunity of using a `div` instruction.
    // This is only available on x86_64, and traps if the quotient does not fit into 64 bits.
    //
    // We also cannot use this during constant evaluation,
    // and if the divisor is known to the optimizer,
    // relying on full 128-bit division is better because it can be transformed into fixed-point multiplication.
    // Inline assembly does not get optimized.
    // See https://quick-bench.com/q/o-0RD_0_bnc5tQqJ4o21wdeeuco
    if constexpr (width_v<T> == 64) {
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if (!BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(y)) {
    #ifdef BEMAN_BIG_INT_HAS_X86_GNU_ASM
                T q, r;
                // volatile is load-bearing: a non-volatile asm counts as
                // pure, and GCC at -O2 speculatively hoisted this above a
                // caller's normalization branch (divide_unsigned_short via
                // reciprocal_word), executing the div with an unnormalized
                // divisor -- the quotient overflow traps with SIGFPE on x86.
                // This has happened with GCC-14 in release mode
                __asm__ volatile("div %[d]" : "=a"(q), "=d"(r) : "a"(x.low_bits), "d"(x.high_bits), [d] "r"(y) : "cc");
                return {.quotient = q, .remainder = r};
    #elif defined(BEMAN_BIG_INT_INTRINSIC_UDIV128)
                T r;
                T q = BEMAN_BIG_INT_INTRINSIC_UDIV128(
                    static_cast<T>(x.high_bits), static_cast<T>(x.low_bits), static_cast<T>(y), &r);
                return {.quotient = static_cast<T>(q), .remainder = static_cast<T>(r)};
    #endif
            }
        }
    }
#endif // BEMAN_BIG_INT_LIMB_WIDTH == 64

    // In the general case, use `wider_t<T>` division when it exists. Otherwise
    // (64-bit limbs without any 128-bit type), divide in software.
    if constexpr (has_wider<T>) {
        const auto x_int = x.to_int();
        return {
            .quotient  = static_cast<T>(x_int / y),
            .remainder = static_cast<T>(x_int % y),
        };
    } else {
        return narrowing_div_portable(x, y);
    }
}

// Portable bit-by-bit restoring long division of the 2-limb unsigned value `a`
// by the 2-limb unsigned value `b`.
template <unsigned_integer T>
[[nodiscard]] constexpr wide_div_result<T> divide_wide_by_wide_portable(const wide<T> a, const wide<T> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(b.high_bits != 0);

    constexpr std::size_t limb_bits = width_v<T>;

    T q    = 0;
    T r_lo = 0;
    T r_hi = 0;

    for (std::size_t i = 2 * limb_bits; i-- > 0;) {
        const T r_top = static_cast<T>(r_hi >> (limb_bits - 1));
        r_hi          = funnel_shl(wide<T>{.low_bits = r_lo, .high_bits = r_hi}, 1u);
        const T bit   = (i >= limb_bits) ? static_cast<T>((a.high_bits >> (i - limb_bits)) & T{1})
                                         : static_cast<T>((a.low_bits >> i) & T{1});
        r_lo          = static_cast<T>((r_lo << 1) | bit);
        q <<= 1;

        // If the virtual 3-limb remainder (r_top, r_hi, r_lo) is >= b, subtract.
        const bool r_ge_b = (r_top != 0) || (r_hi > b.high_bits) || (r_hi == b.high_bits && r_lo >= b.low_bits);
        if (r_ge_b) {
            const auto [diff_lo, borrow_lo] = borrowing_sub(r_lo, b.low_bits);
            r_lo                            = diff_lo;
            r_hi                            = borrowing_sub(r_hi, b.high_bits, borrow_lo).value;
            q |= T{1};
        }
    }
    return {
        .quotient  = q,
        .remainder = {.low_bits = r_lo, .high_bits = r_hi},
    };
}

// Returns the quotient and remainder of dividing the 2-limb unsigned value `a`
// by the 2-limb unsigned value `b`.
// Precondition: `b.high_bits != 0`, which guarantees the quotient fits in a single limb of `T`.
template <unsigned_integer T>
[[nodiscard]] constexpr wide_div_result<T> divide_wide_by_wide(const wide<T> a, const wide<T> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(b.high_bits != 0);

    if constexpr (has_wider<T>) {
        const auto a_int = a.to_int();
        const auto b_int = b.to_int();
        return {
            .quotient  = static_cast<T>(a_int / b_int),
            .remainder = wide<T>::from_int(a_int % b_int),
        };
    } else {
        return divide_wide_by_wide_portable(a, b);
    }
}

// ---------------------------------------------------------------------------
// Division by invariant (precomputed-reciprocal) divisors.
// Reference: N. Moller, T. Granlund, "Improved division by invariant
// integers", IEEE Transactions on Computers 60(2), 2011; the formulations
// below match GMP's udiv_qrnnd_preinv / invert_pi1 / udiv_qr_3by2.
// All routines require the divisor's top bit set ("normalized").
// ---------------------------------------------------------------------------

// 2/1 reciprocal of a normalized divisor: v = floor((B^2 - 1) / d) - B where
// B = 2^limb_bits. Computed with one narrowing division: the quotient of
// <B-1-d, B-1> / d equals v, and B-1-d < d holds exactly when d is
// normalized.
// Deliberately NOT the divide-free Moller-Granlund Algorithm 2 table ladder
// (GMP invert_limb): measured 2026-06-10 via division_kernel_bench, the
// ladder's ~six serial multiplies LOST to this single division on both
// tuning machines -- 8.0 ns vs 3.5 ns on an i9-11900K (fast hardware
// divider) and 8.4 ns vs 6.4 ns on an M4 Max (whose __udivti3 path runs
// ~28 cycles). Revisit only with evidence from a slow-divider target.
template <unsigned_integer T>
[[nodiscard]] constexpr T reciprocal_word(const T d) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT((d >> (width_v<T> - 1)) == 1);
    constexpr T max_limb = static_cast<T>(~static_cast<T>(0));
    return narrowing_div(wide<T>{.low_bits = max_limb, .high_bits = static_cast<T>(max_limb - d)}, d).quotient;
}

// 3/2 reciprocal of a normalized two-limb divisor <d1,d0>:
//   v = floor((B^3 - 1) / <d1,d0>) - B
// computed by refining reciprocal_word(d1) for the low limb (Moller-Granlund
// Algorithm 6 / GMP invert_pi1).
template <unsigned_integer T>
[[nodiscard]] constexpr T reciprocal_word_3by2(const T d1, const T d0) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT((d1 >> (width_v<T> - 1)) == 1);

    // widening_mul(...).low_bits is the promotion-safe mod-B product (a plain
    // `*` on sub-int limb types would overflow the promoted int).
    T v = reciprocal_word(d1);
    T p = widening_mul(d1, v).low_bits;
    p   = static_cast<T>(p + d0);
    if (p < d0) {
        --v;
        const bool ge = p >= d1;
        p             = static_cast<T>(p - d1);
        if (ge) {
            --v;
            p = static_cast<T>(p - d1);
        }
    }
    const wide<T> t = widening_mul(d0, v);
    p               = static_cast<T>(p + t.high_bits);
    if (p < t.high_bits) {
        --v;
        if (p > d1 || (p == d1 && t.low_bits >= d0)) {
            --v;
        }
    }
    return v;
}

// Divides <u.high_bits, u.low_bits> by the normalized divisor `d` using its
// precomputed reciprocal `v` (Moller-Granlund Algorithm 4): two multiplies
// and a branch-light correction instead of a hardware divide.
// Precondition: u.high_bits < d, so the quotient fits one limb.
template <unsigned_integer T>
[[nodiscard]] constexpr div_result<T> div_2by1_preinv(const wide<T> u, const T d, const T v) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT((d >> (width_v<T> - 1)) == 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(u.high_bits < d);

    // <q1,q0> = (B + v) * u.high + u.low, with the +1 of the algorithm folded
    // into the high add (u.high + 1 cannot wrap: u.high < d <= B-1).
    const wide<T> qw          = widening_mul(v, u.high_bits);
    const auto [q0, q0_carry] = carrying_add(qw.low_bits, u.low_bits);
    const T q1                = static_cast<T>(qw.high_bits + u.high_bits + static_cast<T>(q0_carry) + 1);

    T r = static_cast<T>(u.low_bits - widening_mul(q1, d).low_bits);

    // Candidate is off by at most one in each direction; the first adjustment
    // is taken often enough to be worth keeping branch-free.
    const T mask = static_cast<T>(static_cast<T>(0) - static_cast<T>(r > q0));
    const T q    = static_cast<T>(q1 + mask);
    r            = static_cast<T>(r + (mask & d));
    if (r >= d) { // unlikely
        return {static_cast<T>(q + 1), static_cast<T>(r - d)};
    }
    return {q, r};
}

// Divides <u2,u1,u0> by the normalized two-limb divisor <d1,d0> using its
// precomputed 3/2 reciprocal `v` (Moller-Granlund Algorithm 5 / GMP
// udiv_qr_3by2). Returns one quotient limb and the two-limb remainder.
// Precondition: <u2,u1> < <d1,d0>, so the quotient fits one limb.
template <unsigned_integer T>
[[nodiscard]] constexpr wide_div_result<T>
div_3by2_preinv(const T u2, const T u1, const T u0, const T d1, const T d0, const T v) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT((d1 >> (width_v<T> - 1)) == 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(u2 < d1 || (u2 == d1 && u1 < d0));

    // <q1,q0> = v * u2 + <u2,u1>.
    const wide<T> qw          = widening_mul(v, u2);
    const auto [q0, q0_carry] = carrying_add(qw.low_bits, u1);
    T q1                      = static_cast<T>(qw.high_bits + u2 + static_cast<T>(q0_carry));

    // <r1,r0> = <u1,u0> - q1 * <d1,d0> - <d1,d0>  (mod B^2), tracking only the
    // low two limbs: r1 = u1 - q1*d1 captures the high part exactly.
    T r1 = static_cast<T>(u1 - widening_mul(q1, d1).low_bits);

    const auto [s0, s_borrow] = borrowing_sub(u0, d0);
    T r0                      = s0;
    r1                        = static_cast<T>(r1 - d1 - static_cast<T>(s_borrow));

    const wide<T> t           = widening_mul(d0, q1);
    const auto [w0, w_borrow] = borrowing_sub(r0, t.low_bits);
    r0                        = w0;
    r1                        = static_cast<T>(r1 - t.high_bits - static_cast<T>(w_borrow));

    ++q1;

    // Common correction, branch-free: if r1 >= q0 the candidate is one too
    // large; give back one divisor.
    const T mask           = static_cast<T>(static_cast<T>(0) - static_cast<T>(r1 >= q0));
    q1                     = static_cast<T>(q1 + mask);
    const T add0           = static_cast<T>(mask & d0);
    const T add1           = static_cast<T>(mask & d1);
    const auto [a0, carry] = carrying_add(r0, add0);
    r0                     = a0;
    r1                     = static_cast<T>(r1 + add1 + static_cast<T>(carry));

    // Rare final correction.
    if (r1 > d1 || (r1 == d1 && r0 >= d0)) { // unlikely
        ++q1;
        const auto [f0, f_borrow] = borrowing_sub(r0, d0);
        r0                        = f0;
        r1                        = static_cast<T>(r1 - d1 - static_cast<T>(f_borrow));
    }

    return {q1, {.low_bits = r0, .high_bits = r1}};
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_WIDE_OPS_HPP
