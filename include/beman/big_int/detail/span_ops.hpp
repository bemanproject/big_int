// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SPAN_OPS_HPP
#define BEMAN_BIG_INT_SPAN_OPS_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <bit>
    #include <initializer_list>
    #include <ranges>
    #include <utility>
    #include <span>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// ---------------------------------------------------------------------------
// In-place +1 on a little-endian unsigned span. Returns true on carry out.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr bool increment_span(const std::span<uint_multiprecision_t> s) noexcept {
    for (auto& limb : s) {
        if (++limb != 0) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// In-place -1 on a little-endian unsigned span. Returns true on borrow out
// (i.e. the span was all zero on entry).
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr bool decrement_span(const std::span<uint_multiprecision_t> s) noexcept {
    for (auto& limb : s) {
        if (limb-- != 0) {
            return false;
        }
    }
    return true;
}

// Returns true if all limbs in the span are zero
constexpr bool is_span_zero(const std::span<const uint_multiprecision_t> s) noexcept {
    return std::ranges::all_of(s, [](const uint_multiprecision_t limb) { return limb == 0; });
}

// Returns true if the trimmed span holds exactly a power of two (a single set
// bit in the top limb, all lower limbs zero). Cheap for ordinary values: the
// single-bit test on the top limb almost always fails, and the zero-scan exits
// at the first non-zero low limb.
[[nodiscard]] constexpr bool is_power_of_two_span(const std::span<const uint_multiprecision_t> s) noexcept {
    return !s.empty() && std::has_single_bit(s.back()) && is_span_zero(s.first(s.size() - 1));
}

// Bit index of the lowest set bit of a magnitude, i.e. the exponent of the
// largest power of two that divides it. The magnitude must be nonzero.
[[nodiscard]] constexpr std::size_t trailing_zero_bits_span(const std::span<const uint_multiprecision_t> x) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!is_span_zero(x));
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    std::size_t           i         = 0;
    while (x[i] == 0) {
        ++i;
    }
    return i * limb_bits + static_cast<std::size_t>(std::countr_zero(x[i]));
}

// Limb `i` of a magnitude, or zero when the magnitude does not reach that far.
// Lets an algorithm read two operands at the same limb positions without
// special-casing the shorter one.
[[nodiscard]] constexpr uint_multiprecision_t limb_or_zero(const std::span<const uint_multiprecision_t> x,
                                                           const std::size_t                            i) noexcept {
    return i < x.size() ? x[i] : uint_multiprecision_t{0};
}

// ---------------------------------------------------------------------------
// Three-way compare of two little-endian unsigned spans.
// Operands need not be trimmed: trailing zero limbs on either side are
// treated as insignificant.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::strong_ordering
compare_unsigned_spans(const std::span<const uint_multiprecision_t> a,
                       const std::span<const uint_multiprecision_t> b) noexcept {
    const std::size_t na = a.size();
    const std::size_t nb = b.size();

    // Scan the high tail of the longer operand: any non-zero limb there
    // immediately decides the comparison.
    if (na > nb) {
        for (std::size_t i = na; i-- > nb;) {
            if (a[i] != 0) {
                return std::strong_ordering::greater;
            }
        }
    } else if (nb > na) {
        for (std::size_t i = nb; i-- > na;) {
            if (b[i] != 0) {
                return std::strong_ordering::less;
            }
        }
    }

    // Overlap: both operands have limbs at every position.
    const std::size_t overlap = std::min(na, nb);
    for (std::size_t i = overlap; i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? std::strong_ordering::less : std::strong_ordering::greater;
        }
    }
    return std::strong_ordering::equal;
}

// ---------------------------------------------------------------------------
// Unsigned span addition: result = a + b
// `result.size()` must be >= max(a.size(), b.size()).
// `result` may alias `a`. Returns true if there is a carry out.
// ---------------------------------------------------------------------------
constexpr bool add_unsigned_spans(const std::span<uint_multiprecision_t>       result,
                                  const std::span<const uint_multiprecision_t> a,
                                  const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= std::max(a.size(), b.size()));

    const std::size_t overlap     = std::min(a.size(), b.size());
    const auto&       longer      = a.size() > b.size() ? a : b;
    const std::size_t longer_size = longer.size();

    bool        carry = false;
    std::size_t i     = 0;

    // Overlap: both operands contribute.
    for (; i < overlap; ++i) {
        const auto [r_value, c1] = carrying_add(a[i], b[i], carry);
        result[i]                = r_value;
        carry                    = c1;
    }

    // Tail of the longer operand: shorter is now zero.
    for (; i < longer_size; ++i) {
        const auto [r_value, c1] = carrying_add(longer[i], uint_multiprecision_t{0}, carry);
        result[i]                = r_value;
        carry                    = c1;
    }

    // Carry propagation + zero fill (when result extends beyond both operands).
    for (; i < result.size(); ++i) {
        const auto [r_value, c1] = carrying_add(uint_multiprecision_t{0}, uint_multiprecision_t{0}, carry);
        result[i]                = r_value;
        carry                    = c1;
    }
    return carry;
}

// Trim leading zero limbs returning the effective size
constexpr std::size_t trimmed_size_span(const std::span<const uint_multiprecision_t> s) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!s.empty());
    std::size_t n = s.size();
    while (n > 1 && s[n - 1] == 0) {
        --n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Unsigned span subtraction: result = a - b
// Requires |a| >= |b|. `result` may alias `a`.
// Returns the trimmed number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t subtract_unsigned_spans(const std::span<uint_multiprecision_t>       result,
                                              const std::span<const uint_multiprecision_t> a,
                                              const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());

    bool borrow = false;

    // Overlap: both operands contribute.
    for (std::size_t i = 0; i < b.size(); ++i) {
        const auto [r_value, r_borrow] = borrowing_sub(a[i], b[i], borrow);
        result[i]                      = r_value;
        borrow                         = r_borrow;
    }

    // Tail of a: b is now zero.
    for (std::size_t i = b.size(); i < a.size(); ++i) {
        const auto [r_value, r_borrow] = borrowing_sub(a[i], uint_multiprecision_t{0}, borrow);
        result[i]                      = r_value;
        borrow                         = r_borrow;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size()});
}

// ---------------------------------------------------------------------------
// Wraparound unsigned span subtraction: result = (a - b) mod B^a.size().
// Unlike subtract_unsigned_spans, value(a) may be less than value(b); the
// borrow out of the top limb is returned instead of asserted away.
// Requires result.size() >= a.size() >= b.size(). `result` may alias `a`.
// Writes exactly a.size() limbs; no trimming.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr bool
subtract_unsigned_spans_borrow_out(const std::span<uint_multiprecision_t>       result,
                                   const std::span<const uint_multiprecision_t> a,
                                   const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());

    bool borrow = false;

    // Overlap: both operands contribute.
    for (std::size_t i = 0; i < b.size(); ++i) {
        const auto [r_value, r_borrow] = borrowing_sub(a[i], b[i], borrow);
        result[i]                      = r_value;
        borrow                         = r_borrow;
    }

    // Tail of a: b is now zero.
    for (std::size_t i = b.size(); i < a.size(); ++i) {
        const auto [r_value, r_borrow] = borrowing_sub(a[i], uint_multiprecision_t{0}, borrow);
        result[i]                      = r_value;
        borrow                         = r_borrow;
    }

    return borrow;
}

// Void wrappers that discard the existing functions' returns and assert their
// expected conditions. Their matching `void` return lets callers express the
// sign-aware "result = a + b_signed" dispatch as a single ternary:
//   sign_b ? add_unsigned_spans_no_carry(r, a, b)
//          : subtract_unsigned_spans_no_borrow(r, a, b);
constexpr void add_unsigned_spans_no_carry(const std::span<uint_multiprecision_t>       result,
                                           const std::span<const uint_multiprecision_t> a,
                                           const std::span<const uint_multiprecision_t> b) noexcept {
    const bool carry = add_unsigned_spans(result, a, b);
    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
}

constexpr void subtract_unsigned_spans_no_borrow(const std::span<uint_multiprecision_t>       result,
                                                 const std::span<const uint_multiprecision_t> a,
                                                 const std::span<const uint_multiprecision_t> b) noexcept {
    [[maybe_unused]] const auto unused = subtract_unsigned_spans(result, a, b);
}

// ---------------------------------------------------------------------------
// Fused (a + b) >> n into result in a single pass. Returns the dropped low n
// bits (caller asserts == 0 for exact division by 2^n). Asserts no carry-out
// from the sum. `result` may alias `a` or `b`.
// 0 <= n < limb_width.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
add_unsigned_spans_and_shift_right_n(const std::span<uint_multiprecision_t>       result,
                                     const std::span<const uint_multiprecision_t> a,
                                     const std::span<const uint_multiprecision_t> b,
                                     const unsigned                               n) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= std::max(a.size(), b.size()));

    if (n == 0) {
        const bool carry = add_unsigned_spans(result, a, b);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    if (result.empty()) {
        return 0;
    }

    // Compute sum[0] separately so we can capture the dropped low bits.
    const auto a0     = !a.empty() ? a[0] : uint_multiprecision_t{0};
    const auto b0     = !b.empty() ? b[0] : uint_multiprecision_t{0};
    auto [s_prev, c0] = carrying_add(a0, b0);
    bool carry        = c0;

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = s_prev & mask;

    // Each iteration computes sum[i] then writes the shifted result[i-1] by
    // funnel-shifting (sum[i-1], sum[i]) right by n. Reads at index i happen
    // before the write at i-1, so result may safely alias a or b.
    const std::size_t overlap     = std::min(a.size(), b.size());
    const auto&       longer      = a.size() > b.size() ? a : b;
    const std::size_t longer_size = longer.size();
    std::size_t       i           = 1;

    // Overlap: both operands contribute.
    for (; i < overlap; ++i) {
        const auto [s_curr, ci] = carrying_add(a[i], b[i], carry);
        carry                   = ci;
        result[i - 1]           = funnel_shr(wide<uint_multiprecision_t>{.low_bits = s_prev, .high_bits = s_curr}, n);
        s_prev                  = s_curr;
    }

    // Tail of the longer operand: shorter is now zero.
    for (; i < longer_size; ++i) {
        const auto [s_curr, ci] = carrying_add(longer[i], uint_multiprecision_t{0}, carry);
        carry                   = ci;
        result[i - 1]           = funnel_shr(wide<uint_multiprecision_t>{.low_bits = s_prev, .high_bits = s_curr}, n);
        s_prev                  = s_curr;
    }

    // Carry propagation + zero fill beyond both operands.
    for (; i < result.size(); ++i) {
        const auto [s_curr, ci] = carrying_add(uint_multiprecision_t{0}, uint_multiprecision_t{0}, carry);
        carry                   = ci;
        result[i - 1]           = funnel_shr(wide<uint_multiprecision_t>{.low_bits = s_prev, .high_bits = s_curr}, n);
        s_prev                  = s_curr;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    result[result.size() - 1] = s_prev >> n;
    return rem;
}

[[nodiscard]] constexpr uint_multiprecision_t
add_unsigned_spans_and_shift_right_one(const std::span<uint_multiprecision_t>       result,
                                       const std::span<const uint_multiprecision_t> a,
                                       const std::span<const uint_multiprecision_t> b) noexcept {
    return add_unsigned_spans_and_shift_right_n(result, a, b, 1u);
}

// ---------------------------------------------------------------------------
// Fused (a - b) >> n into result in a single pass. Returns the dropped low n
// bits. Requires |a| >= |b|. `result` may alias `a` or `b`.
// 0 <= n < limb_width.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
subtract_unsigned_spans_and_shift_right_n(const std::span<uint_multiprecision_t>       result,
                                          const std::span<const uint_multiprecision_t> a,
                                          const std::span<const uint_multiprecision_t> b,
                                          const unsigned                               n) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());

    if (n == 0) {
        subtract_unsigned_spans(result, a, b);
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    if (result.empty()) {
        return 0;
    }

    const auto a0      = !a.empty() ? a[0] : uint_multiprecision_t{0};
    const auto b0      = !b.empty() ? b[0] : uint_multiprecision_t{0};
    auto [d_prev, bo0] = borrowing_sub(a0, b0);
    bool borrow        = bo0;

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = d_prev & mask;

    std::size_t i = 1;

    // Overlap: both operands contribute (i < b.size()).
    for (; i < b.size(); ++i) {
        const auto [d_curr, boi] = borrowing_sub(a[i], b[i], borrow);
        borrow                   = boi;
        result[i - 1]            = funnel_shr(wide<uint_multiprecision_t>{.low_bits = d_prev, .high_bits = d_curr}, n);
        d_prev                   = d_curr;
    }

    // Tail of a (a.size() >= b.size() by precondition).
    for (; i < a.size(); ++i) {
        const auto [d_curr, boi] = borrowing_sub(a[i], uint_multiprecision_t{0}, borrow);
        borrow                   = boi;
        result[i - 1]            = funnel_shr(wide<uint_multiprecision_t>{.low_bits = d_prev, .high_bits = d_curr}, n);
        d_prev                   = d_curr;
    }

    // Beyond a: only borrow propagation (must terminate with borrow == false).
    for (; i < result.size(); ++i) {
        const auto [d_curr, boi] = borrowing_sub(uint_multiprecision_t{0}, uint_multiprecision_t{0}, borrow);
        borrow                   = boi;
        result[i - 1]            = funnel_shr(wide<uint_multiprecision_t>{.low_bits = d_prev, .high_bits = d_curr}, n);
        d_prev                   = d_curr;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    result[result.size() - 1] = d_prev >> n;
    return rem;
}

[[nodiscard]] constexpr uint_multiprecision_t
subtract_unsigned_spans_and_shift_right_one(const std::span<uint_multiprecision_t>       result,
                                            const std::span<const uint_multiprecision_t> a,
                                            const std::span<const uint_multiprecision_t> b) noexcept {
    return subtract_unsigned_spans_and_shift_right_n(result, a, b, 1u);
}

// ---------------------------------------------------------------------------
// Fused dst = dst_view - (src << shift) in a single pass, streaming the shifted
// source via funnel_shl without materializing it into a buffer. Replaces the
// open-coded sequence:
//   fill(tmp, 0); copy(src, tmp); s = shift_left_n(tmp, src.size(), shift);
//   subtract_unsigned_spans(dst, dst_view, {tmp, s});
// 0 <= shift < limb_width. `dst` may alias `dst_view`. `src` must not alias
// either; if src.empty() and dst aliases dst_view, this is a no-op.
// Requires dst_view.size() >= src.size() + (shift > 0 ? 1 : 0) so the shifted
// source's top limb has somewhere to land. Returns the trimmed result size
// (matches subtract_unsigned_spans); callers may ignore it.
// ---------------------------------------------------------------------------
constexpr std::size_t subtract_shifted_unsigned(const std::span<uint_multiprecision_t>       dst,
                                                const std::span<const uint_multiprecision_t> dst_view,
                                                const std::span<const uint_multiprecision_t> src,
                                                const unsigned                               shift) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(dst.size() >= dst_view.size());

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(shift < local_limb_bits);

    if (shift == 0) {
        return subtract_unsigned_spans(dst, dst_view, src);
    }
    if (src.empty()) {
        if (dst.data() != dst_view.data()) {
            for (std::size_t i = 0; i < dst_view.size(); ++i) {
                dst[i] = dst_view[i];
            }
        }
        return trimmed_size_span(dst_view);
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(dst_view.size() >= src.size() + 1u);

    bool                  borrow = false;
    uint_multiprecision_t prev   = 0;
    std::size_t           i      = 0;

    // Overlap: i in [0, src.size()) — shifted_src has live data here.
    for (; i < src.size(); ++i) {
        const auto curr    = src[i];
        const auto shifted = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = curr}, shift);
        const auto [r, br] = borrowing_sub(dst_view[i], shifted, borrow);
        dst[i]             = r;
        borrow             = br;
        prev               = curr;
    }

    // High limb of shifted src: prev >> (limb_bits - shift).
    {
        const auto carry_out = prev >> (local_limb_bits - shift);
        const auto [r, br]   = borrowing_sub(dst_view[i], carry_out, borrow);
        dst[i]               = r;
        borrow               = br;
        ++i;
    }

    // Tail: src exhausted, only borrow propagates.
    for (; i < dst_view.size(); ++i) {
        const auto [r, br] = borrowing_sub(dst_view[i], uint_multiprecision_t{0}, borrow);
        dst[i]             = r;
        borrow             = br;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{dst.data(), dst_view.size()});
}

// ---------------------------------------------------------------------------
// Signed span subtraction: writes |a - b| into result, returns its size and
// whether the result is negative (i.e. b > a, so result holds b - a).
// `result` may alias `a` or `b`. Used by Toom-Cook 3 to evaluate at x = -1.
// ---------------------------------------------------------------------------
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

struct signed_sub_result {
    std::size_t size;
    bool        negative;
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

constexpr signed_sub_result subtract_unsigned_spans_signed(const std::span<uint_multiprecision_t>       result,
                                                           const std::span<const uint_multiprecision_t> a,
                                                           const std::span<const uint_multiprecision_t> b) noexcept {
    // Trim before comparing so the size relationship matches the value relationship,
    // satisfying subtract_unsigned_spans's a.size() >= b.size() invariant.
    const std::size_t a_trim = a.empty() ? 0 : trimmed_size_span(a);
    const std::size_t b_trim = b.empty() ? 0 : trimmed_size_span(b);
    const auto        a_view = a.first(a_trim);
    const auto        b_view = b.first(b_trim);

    if (compare_unsigned_spans(a_view, b_view) != std::strong_ordering::less) {
        BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_trim);
        if (a_trim == 0) {
            return {0, false};
        }
        return {subtract_unsigned_spans(result.first(a_trim), a_view, b_view), false};
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= b_trim);
    return {subtract_unsigned_spans(result.first(b_trim), b_view, a_view), true};
}

// ---------------------------------------------------------------------------
// Single-limb short division via the Moller-Granlund reciprocal: normalize
// the divisor once, then stream `quotient[i] := floor(<r, dividend'[i]> / d)`
// from the top limb down with div_2by1_preinv (two multiplies per limb
// instead of a hardware/software 2-limb divide). Returns the scalar
// remainder.
// `quotient` and `dividend` may be the same range (i.e. alias each other),
// but `quotient` may not be a strict subrange of `dividend`.
//
// Preconditions:
//   - divisor != 0
//   - quotient.size() >= dividend.size()
//   - dividend.size() >= 1
//   - quotient may alias dividend (each iteration reads dividend[i] and
//     dividend[i-1] before writing quotient[i]; later iterations touch
//     strictly lower indices).
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
divide_unsigned_short(const std::span<uint_multiprecision_t>       quotient,
                      const std::span<const uint_multiprecision_t> dividend,
                      const uint_multiprecision_t                  divisor) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());

    // Single-limb dividend: one native divide beats the reciprocal setup.
    if (dividend.size() == 1) {
        const uint_multiprecision_t value = dividend[0];
        quotient[0]                       = value / divisor;
        return value % divisor;
    }

    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    const unsigned        shift     = static_cast<unsigned>(std::countl_zero(divisor));

    if (shift == 0) {
        const uint_multiprecision_t v = reciprocal_word(divisor);
        uint_multiprecision_t       r = 0;
        for (std::size_t i = dividend.size(); i-- > 0;) {
            const auto qr =
                div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = dividend[i], .high_bits = r}, divisor, v);
            quotient[i] = qr.quotient;
            r           = qr.remainder;
        }
        return r;
    }

    // Normalize on the fly: divide the bit-shifted dividend by divisor <<
    // shift, funneling adjacent limbs. The quotient digits are unaffected by
    // the common shift; the final remainder shifts back down.
    const uint_multiprecision_t d = divisor << shift;
    const uint_multiprecision_t v = reciprocal_word(d);

    uint_multiprecision_t cur = dividend[dividend.size() - 1];
    uint_multiprecision_t r   = cur >> (limb_bits - shift);
    for (std::size_t i = dividend.size() - 1; i > 0; --i) {
        const uint_multiprecision_t next = dividend[i - 1];
        const uint_multiprecision_t u =
            funnel_shl(wide<uint_multiprecision_t>{.low_bits = next, .high_bits = cur}, shift);
        const auto qr = div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = u, .high_bits = r}, d, v);
        quotient[i]   = qr.quotient;
        r             = qr.remainder;
        cur           = next;
    }
    const auto qr = div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = cur << shift, .high_bits = r}, d, v);
    quotient[0]   = qr.quotient;
    return qr.remainder >> shift;
}

// ---------------------------------------------------------------------------
// Remainder of a multi-limb magnitude modulo a single limb.
// Same normalize-once, stream-down-with-a-reciprocal structure as
// divide_unsigned_short, but it keeps only the remainder, so no quotient buffer
// (and no mutable copy of the dividend) is needed.
//
// Preconditions:
//   - divisor != 0
//   - dividend.size() >= 1
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t mod_unsigned_short(const std::span<const uint_multiprecision_t> dividend,
                                                                 const uint_multiprecision_t divisor) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());

    if (dividend.size() == 1) {
        return dividend[0] % divisor;
    }

    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    const auto            shift     = static_cast<unsigned>(std::countl_zero(divisor));

    if (shift == 0) {
        const uint_multiprecision_t v = reciprocal_word(divisor);
        uint_multiprecision_t       r = 0;
        for (std::size_t i = dividend.size(); i-- > 0;) {
            r = div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = dividend[i], .high_bits = r}, divisor, v)
                    .remainder;
        }
        return r;
    }

    // Normalize on the fly: the remainder of the bit-shifted dividend by the
    // divisor shifted by the same amount is the wanted remainder, shifted up.
    const uint_multiprecision_t d = divisor << shift;
    const uint_multiprecision_t v = reciprocal_word(d);

    uint_multiprecision_t cur = dividend[dividend.size() - 1];
    uint_multiprecision_t r   = cur >> (limb_bits - shift);
    for (std::size_t i = dividend.size() - 1; i > 0; --i) {
        const uint_multiprecision_t next = dividend[i - 1];
        const uint_multiprecision_t u =
            funnel_shl(wide<uint_multiprecision_t>{.low_bits = next, .high_bits = cur}, shift);
        r   = div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = u, .high_bits = r}, d, v).remainder;
        cur = next;
    }
    r = div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = cur << shift, .high_bits = r}, d, v).remainder;
    return r >> shift;
}

// ---------------------------------------------------------------------------
// Fused multiply-subtract: result -= a * val over the low a.size() limbs of
// `result`, in place. Returns the borrow-out limb (the amount owed at
// position a.size(); always at most B-1, so a single limb suffices).
// `result.size()` must be >= a.size(); `result` must NOT alias `a`.
// The inner step of Knuth-D schoolbook division.
//
// An AVX-512 IFMA version was probed on an i9-11900K (2026-06-11) and
// REJECTED: the vpmadd52 product kernel itself runs 0.13-0.21 ns/limb
// against this loop's 0.69-0.96 (a real ~4x multiply ceiling), but a
// one-shot call must stage through radix 2^52 -- the 64->52 conversion,
// 52->64 recompose, and separated subtract pass each cost about as much as
// this entire loop, landing the pipeline at 3.0-3.7x SLOWER at every span
// from 8 to 4096 limbs. Fully vectorized staging (VBMI multishift repack,
// masked-borrow subtract) still models >= 1.4x slower. The ceiling is only
// reachable by keeping the whole schoolbook division resident in radix
// 2^52, a redesign out of proportion to its modeled <= 1.4x basecase win
// on one ISA family.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t submul_single_limb(const std::span<uint_multiprecision_t>       result,
                                                                 const std::span<const uint_multiprecision_t> a,
                                                                 const uint_multiprecision_t val) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data() || a.empty());

    // Owed at each position: a[i] * val + borrow <= (B-1)^2 + (B-1), so the
    // carry folds into the high half without overflow; and when the limb
    // subtraction borrows, that high half is at most B-2, so the running
    // borrow always fits one limb.
    // A four-way unroll with hoisted multiplies was measured 2026-06-10
    // (division_kernel_bench): flat on an M4 Max and 3-7% SLOWER on an
    // i9-11900K -- the compilers already schedule this form at the borrow
    // chain's bound. Closing the remaining gap to hand-tuned kernels would
    // take per-arch inline assembly, deliberately not used here.
    uint_multiprecision_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto [lo, hi]   = widening_mul(a[i], val);
        const auto [low, c]   = carrying_add(lo, borrow);
        const auto [r_val, b] = borrowing_sub(result[i], low);
        result[i]             = r_val;
        borrow                = hi + static_cast<uint_multiprecision_t>(c) + static_cast<uint_multiprecision_t>(b);
    }
    return borrow;
}

// ---------------------------------------------------------------------------
// Multiply a multi-limb number `a` by a single limb `val`.
// `result` must have space for `a.size() + 1` limbs and must NOT alias `a`.
// Returns the number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t multiply_single_limb(const std::span<uint_multiprecision_t>       result,
                                           const std::span<const uint_multiprecision_t> a,
                                           const uint_multiprecision_t                  val) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    uint_multiprecision_t carry = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto [lo, hi] = widening_mul(a[i], val);
        const auto [s1, c1] = carrying_add(lo, carry);
        result[i]           = s1;
        carry               = hi + static_cast<uint_multiprecision_t>(c1);
    }

    if (carry != 0) {
        result[a.size()] = carry;
        return a.size() + 1;
    }

    return a.size();
}

// ---------------------------------------------------------------------------
// s.first(size) <- s.first(size) * mul + add, in place (the GMP mpn_mul_1 +
// add fusion the base-conversion Horner basecase folds with; pass add == 0 for a
// plain in-place multiply). `mul` must be non-zero, so the value never shrinks
// and a trimmed value stays trimmed; `s` must allow one limb of growth whenever
// the result needs it. Returns the new size.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::size_t mul_add_single_limb_in_place(const std::span<uint_multiprecision_t> s,
                                                                 const std::size_t                      size,
                                                                 const uint_multiprecision_t            mul,
                                                                 const uint_multiprecision_t            add) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(size >= 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(size <= s.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(mul != 0);

    uint_multiprecision_t carry = add;
#ifdef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
    // A direct wide multiply-add keeps the product in registers; GCC spills the
    // `widening_mul` pair to the stack in this loop. s[i] * mul + carry fits two limbs.
    using wide_type = wider_t<uint_multiprecision_t>;
    for (std::size_t i = 0; i < size; ++i) {
        const wide_type product = static_cast<wide_type>(s[i]) * mul + carry;
        s[i]                    = static_cast<uint_multiprecision_t>(product);
        carry                   = static_cast<uint_multiprecision_t>(product >> width_v<uint_multiprecision_t>);
    }
#else
    for (std::size_t i = 0; i < size; ++i) {
        const auto [lo, hi]              = widening_mul(s[i], mul);
        const uint_multiprecision_t next = lo + carry;
        carry                            = hi + static_cast<uint_multiprecision_t>(next < lo);
        s[i]                             = next;
    }
#endif
    if (carry == 0) {
        return size;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(size < s.size());
    s[size] = carry;
    return size + 1;
}

// ---------------------------------------------------------------------------
// In-place dst -= src * val over all of `dst`, where the difference is known to
// be non-negative. Unlike submul_single_limb, which hands the borrow at src's
// top position back to its caller, this propagates it through the rest of `dst`,
// so `dst` may span more limbs than `src`.
// `dst.size()` must be >= src.size(); `dst` must NOT alias `src`.
// ---------------------------------------------------------------------------
constexpr void submul_single_limb_wide(const std::span<uint_multiprecision_t>       dst,
                                       const std::span<const uint_multiprecision_t> src,
                                       const uint_multiprecision_t                  val) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(dst.size() >= src.size());

    uint_multiprecision_t borrow = submul_single_limb(dst, src, val);
    for (std::size_t i = src.size(); borrow != 0; ++i) {
        BEMAN_BIG_INT_DEBUG_ASSERT(i < dst.size());
        const auto [value, next] = borrowing_sub(dst[i], borrow);
        dst[i]                   = value;
        borrow                   = static_cast<uint_multiprecision_t>(next);
    }
}

// ---------------------------------------------------------------------------
// Helpers shared by the Toom-Cook variants. All operate on little-endian
// multi-precision spans of uint_multiprecision_t limbs.
// ---------------------------------------------------------------------------

// In-place tmp[0..size) += addend; returns new size (may grow by 1).
[[nodiscard]] constexpr std::size_t add_into_tmp(const std::span<uint_multiprecision_t>       tmp,
                                                 const std::size_t                            size,
                                                 const std::span<const uint_multiprecision_t> addend) noexcept {
    if (addend.empty()) {
        return size;
    }

    const std::size_t new_size = std::max(size, addend.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > new_size);

    const bool carry =
        add_unsigned_spans(tmp.first(new_size), std::span<const uint_multiprecision_t>{tmp.data(), size}, addend);

    if (carry) {
        tmp[new_size] = 1;
        return new_size + 1;
    }

    return new_size;
}

// In-place tmp[0..size) <<= n (0 < n < limb_width); returns new size (may grow by 1).
// Pass size == tmp.size() to shift the full span; the carry-branch assertion
// then enforces that the shift did not overflow.
// Single-pass equivalent of n chained shift_left_one calls.
[[nodiscard]] constexpr std::size_t
shift_left_n(const std::span<uint_multiprecision_t> tmp, std::size_t size, const unsigned n) noexcept {
    if (size == 0 || n == 0) {
        return size;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(size <= tmp.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    uint_multiprecision_t prev = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, n);
        prev            = limb;
    }

    if (const auto carry = prev >> (local_limb_bits - n)) {
        BEMAN_BIG_INT_DEBUG_ASSERT(size < tmp.size());
        tmp[size++] = carry;
    }

    return size;
}

// In-place tmp[0..size) <<= 1; returns new size (may grow by 1). Thin wrapper
// around shift_left_n for the common single-bit Horner-style shift.
[[nodiscard]] constexpr std::size_t shift_left_one(const std::span<uint_multiprecision_t> tmp,
                                                   const std::size_t                      size) noexcept {
    return shift_left_n(tmp, size, 1u);
}

// In-place tmp[0..size) <<= bits, for a shift of any width: unlike shift_left_n,
// `bits` may reach past a limb, and the whole-limb part of the shift is a move
// rather than a pass over the limbs. Returns the new size; `tmp` must be large
// enough to hold it.
[[nodiscard]] constexpr std::size_t
shift_left_bits(const std::span<uint_multiprecision_t> tmp, std::size_t size, const std::size_t bits) noexcept {
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    if (bits == 0) {
        return size;
    }

    const std::size_t whole   = bits / local_limb_bits;
    const auto        partial = static_cast<unsigned>(bits % local_limb_bits);

    if (whole != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() >= size + whole);
        const auto begin = tmp.begin();
        std::copy_backward(
            begin, begin + static_cast<std::ptrdiff_t>(size), begin + static_cast<std::ptrdiff_t>(size + whole));
        std::fill_n(begin, static_cast<std::ptrdiff_t>(whole), uint_multiprecision_t{0});
        size += whole;
    }
    if (partial != 0) {
        size = shift_left_n(tmp, size, partial);
    }
    return size;
}

// In-place tmp <- addends[0] + addends[1] + ... + addends[N-1]; returns new size.
// Single fused pass over tmp: at each position i, sums addend[j][i] for every j
// in one shot with multi-input carry propagation. Replaces N-1 separate passes
// (one per chained add_into_tmp), so the number of writes to tmp drops from
// O(N*max_size) to O(max_size). Empty addends are tolerated (treated as zero).
[[nodiscard]] constexpr std::size_t
add_many_into_tmp(const std::span<uint_multiprecision_t>                              tmp,
                  const std::initializer_list<std::span<const uint_multiprecision_t>> addends) noexcept {
    if (addends.size() == 0) {
        return 0;
    }

    std::size_t max_size = 0;
    for (const auto& addend : addends) {
        if (addend.size() > max_size) {
            max_size = addend.size();
        }
    }
    if (max_size == 0) {
        return 0;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > max_size);

    // Per-position carry is bounded by addends.size() (one increment per overflow),
    // so it always fits in a single limb for any plausible N.
    uint_multiprecision_t carry = 0;
    for (std::size_t i = 0; i < max_size; ++i) {
        uint_multiprecision_t sum       = carry;
        uint_multiprecision_t new_carry = 0;
        for (const auto& addend : addends) {
            if (i < addend.size()) {
                const auto [s, c] = carrying_add(sum, addend[i]);
                sum               = s;
                new_carry += static_cast<uint_multiprecision_t>(c);
            }
        }
        tmp[i] = sum;
        carry  = new_carry;
    }

    std::size_t size = max_size;
    if (carry != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(size < tmp.size());
        tmp[size++] = carry;
    }
    return size;
}

// In-place tmp <- (tmp[0..size) << shift) + addend; returns new size (may grow
// by up to 2: one from the shift carry-out, one from the add carry-out). Fused
// single-pass equivalent of (shift_left_n, add_into_tmp). 0 <= shift < limb_width.
// Used as the inner step of horner_eval_into_tmp, but also useful as a standalone
// primitive.
[[nodiscard]] constexpr std::size_t
shift_left_n_and_add_into_tmp(const std::span<uint_multiprecision_t>       tmp,
                              const std::size_t                            size,
                              const unsigned                               shift,
                              const std::span<const uint_multiprecision_t> addend) noexcept {
    if (shift == 0) {
        return add_into_tmp(tmp, size, addend);
    }
    if (size == 0) {
        if (addend.empty()) {
            return 0;
        }
        std::ranges::copy(addend, tmp.begin());
        return addend.size();
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(shift < local_limb_bits);

    // The shifted region is `size + 1` limbs if the top limb's high `shift` bits
    // are non-zero, else `size` limbs. After adding `addend` the new size is
    // max(shifted_size, addend.size()) plus a possible carry-out.
    const uint_multiprecision_t shift_out    = tmp[size - 1] >> (local_limb_bits - shift);
    const std::size_t           shifted_size = size + (shift_out != 0 ? 1u : 0u);
    const std::size_t           output_size  = std::max(shifted_size, addend.size());

    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > output_size);

    bool                  add_carry = false;
    uint_multiprecision_t prev      = 0;
    for (std::size_t i = 0; i < output_size; ++i) {
        uint_multiprecision_t shifted;
        if (i < size) {
            const auto limb = tmp[i];
            shifted         = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, shift);
            prev            = limb;
        } else if (i == size) {
            shifted = shift_out;
        } else {
            shifted = 0;
        }

        const auto ai            = i < addend.size() ? addend[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(shifted, ai, add_carry);
        tmp[i]                   = r_value;
        add_carry                = c1;
    }

    if (add_carry) {
        BEMAN_BIG_INT_DEBUG_ASSERT(output_size < tmp.size());
        tmp[output_size] = 1;
        return output_size + 1;
    }
    return output_size;
}

// In-place tmp <- ((...((coeffs[0] << shift) + coeffs[1]) << shift) + ... ) + coeffs[N-1];
// returns new size. Evaluates a polynomial with MSB-first coefficients at x = 2^shift
// via Horner's method. Each Horner step is a single fused pass over tmp (shift+add),
// halving the per-step memory traffic versus calling shift_left_n then add_into_tmp.
[[nodiscard]] constexpr std::size_t
horner_eval_into_tmp(const std::span<uint_multiprecision_t>                              tmp,
                     const std::initializer_list<std::span<const uint_multiprecision_t>> coeffs,
                     const unsigned                                                      shift) noexcept {
    if (coeffs.size() == 0) {
        return 0;
    }
    auto it = coeffs.begin();
    std::ranges::copy(*it, tmp.begin());
    std::size_t size = it->size();
    for (++it; it != coeffs.end(); ++it) {
        size = shift_left_n_and_add_into_tmp(tmp, size, shift, *it);
    }
    return size;
}

// In-place tmp >>= n (0 < n < limb_width); returns the dropped low n bits packed
// as a single value (caller asserts == 0 for exact division by 2^n).
// Single-pass equivalent of n chained shift_right_one calls.
[[nodiscard]] constexpr uint_multiprecision_t shift_right_n(const std::span<uint_multiprecision_t> tmp,
                                                            const unsigned                         n) noexcept {
    if (tmp.empty() || n == 0) {
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = tmp[0] & mask;
    uint_multiprecision_t       high = 0;
    for (std::size_t i = tmp.size(); i-- > 0;) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shr(wide<uint_multiprecision_t>{.low_bits = limb, .high_bits = high}, n);
        high            = limb;
    }

    return rem;
}

// In-place tmp >>= 1; returns the dropped low bit (caller asserts == 0 for exact div).
// Thin wrapper around shift_right_n for the common single-bit halving case.
[[nodiscard]] constexpr uint_multiprecision_t shift_right_one(const std::span<uint_multiprecision_t> tmp) noexcept {
    return shift_right_n(tmp, 1u);
}

// In-place tmp[0..size) >>= bits, for a shift of any width: unlike shift_right_n,
// `bits` may reach past a limb, and the whole-limb part of the shift is a move
// rather than a pass over the limbs. Returns the trimmed limb count. Every bit
// shifted out must be zero, which holds for the usual caller -- one shifting by a
// trailing-zero count.
[[nodiscard]] constexpr std::size_t
shift_right_bits(const std::span<uint_multiprecision_t> tmp, const std::size_t size, const std::size_t bits) noexcept {
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(size <= tmp.size());

    const std::size_t whole   = bits / local_limb_bits;
    const auto        partial = static_cast<unsigned>(bits % local_limb_bits);
    BEMAN_BIG_INT_DEBUG_ASSERT(whole < size);

    std::size_t n = size;
    if (whole != 0) {
        const auto first = tmp.begin() + static_cast<std::ptrdiff_t>(whole);
        std::copy(first, tmp.begin() + static_cast<std::ptrdiff_t>(size), tmp.begin());
        n = size - whole;
    }
    if (partial != 0) {
        [[maybe_unused]] const uint_multiprecision_t dropped = shift_right_n(tmp.first(n), partial);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
    }
    return trimmed_size_span(tmp.first(n));
}

// In-place result[shift..) += src; asserts no carry out of the result span.
constexpr void add_shifted(const std::span<uint_multiprecision_t>       result,
                           const std::size_t                            shift,
                           const std::span<const uint_multiprecision_t> src) noexcept {
    const auto dest  = result.subspan(shift);
    bool       carry = false;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        const auto si            = i < src.size() ? src[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(dest[i], si, carry);
        dest[i]                  = r_value;
        carry                    = c1;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
}

// Fused multi-source variant of add_shifted with regular k-stride shifts:
//   result[(j+1)*k .. (j+1)*k + sources[j].size()) += sources[j]  for j = 0..N-1
// performed as a single pass over result with a multi-input carry chain.
// Equivalent to N chained add_shifted calls but writes each result limb exactly
// once instead of N times, which is the dominant cost when N gets large (e.g.
// the 10-coefficient recomposition at the end of Toom-Cook 6.5). Asserts no
// carry escapes result.
constexpr void recompose(const std::span<uint_multiprecision_t>                              result,
                         const std::size_t                                                   k,
                         const std::initializer_list<std::span<const uint_multiprecision_t>> sources) noexcept {
    if (sources.size() == 0 || k >= result.size()) {
        return;
    }

    const auto* const src_begin = sources.begin();
    const std::size_t n_sources = sources.size();

    // [active_lo, active_hi) is the slice of `sources` whose ranges currently
    // cover position p. As p advances, active_hi grows when sources enter and
    // active_lo grows when sources leave; each index increments at most N times
    // across the whole outer loop, so the inner work is O(1) amortized.
    std::size_t active_lo = 0;
    std::size_t active_hi = 0;

    uint_multiprecision_t carry = 0;
    for (std::size_t p = k; p < result.size(); ++p) {
        while (active_hi < n_sources && (active_hi + 1) * k <= p) {
            ++active_hi;
        }
        while (active_lo < active_hi && (active_lo + 1) * k + src_begin[active_lo].size() <= p) {
            ++active_lo;
        }

        uint_multiprecision_t sum       = result[p];
        uint_multiprecision_t new_carry = 0;

        if (carry != 0) {
            const auto [s, c] = carrying_add(sum, carry);
            sum               = s;
            new_carry         = static_cast<uint_multiprecision_t>(c);
        }

        for (std::size_t j = active_lo; j < active_hi; ++j) {
            const std::size_t shift = (j + 1) * k;
            const auto [s, c]       = carrying_add(sum, src_begin[j][p - shift]);
            sum                     = s;
            new_carry += static_cast<uint_multiprecision_t>(c);
        }

        result[p] = sum;
        carry     = new_carry;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(carry == 0);
}

// Given s = a + b and (|a - b|, sign_d) for non-negative a and b, recover the
// pair (a, b) and place them in (lower_dst, higher_dst):
//   sign_d == false  =>  a >= b: lower_dst = a = (s + |d|)/2; higher_dst = b = (s - |d|)/2
//   sign_d == true   =>  a <  b: lower_dst = a = (s - |d|)/2; higher_dst = b = (s + |d|)/2
// Used by Toom-Cook palindromic interpolation to split sum/diff back into
// individual coefficients. `tmp_scratch` is a workspace of at least
// max(s_view.size(), d_view.size()) limbs; it does not need to be pre-zeroed.
// The destinations may alias s_view and d_view (recover_pair reads s and d
// before writing the destinations); destinations must not alias each other
// nor tmp_scratch. Both s_view + d_view and s_view - d_view must be even
// (caller's responsibility, asserted via shift_right_one's rem == 0 check).
constexpr void recover_pair(const std::span<uint_multiprecision_t>       lower_dst,
                            const std::span<uint_multiprecision_t>       higher_dst,
                            const std::span<const uint_multiprecision_t> s_view,
                            const std::span<const uint_multiprecision_t> d_view,
                            const bool                                   sign_d,
                            const std::span<uint_multiprecision_t>       tmp_scratch) noexcept {
    // Stage (s + |d|) in tmp_scratch first; the subtract-into-destination step
    // below would otherwise clobber s_view / d_view if either aliases a destination.
    std::ranges::fill(tmp_scratch, uint_multiprecision_t{0});
    const bool sum_carry = add_unsigned_spans(tmp_scratch, s_view, d_view);
    BEMAN_BIG_INT_DEBUG_ASSERT(!sum_carry);
    const std::size_t plus_size = std::max(s_view.size(), d_view.size());
    const auto        plus_span = std::span<const uint_multiprecision_t>{tmp_scratch.data(), plus_size};

    // (s - |d|)/2 into the destination chosen by sign.
    const std::span<uint_multiprecision_t> minus_dst = sign_d ? lower_dst : higher_dst;
    subtract_unsigned_spans(minus_dst, s_view, d_view);
    {
        const auto rem = shift_right_one(minus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // (s + |d|)/2 into the other destination.
    const std::span<uint_multiprecision_t> plus_dst = sign_d ? higher_dst : lower_dst;
    std::ranges::fill(plus_dst, uint_multiprecision_t{0});
    std::ranges::copy(plus_span, plus_dst.begin());
    {
        const auto rem = shift_right_one(plus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
}

// Used by the solve_subsystem lambda in toom_cook_6_5
struct subsystem_signs {
    bool outer;
    bool inner;
};

// ===== Front-end primitives: tail-aware add/sub and copying shifts ==================================
// Single-pass building blocks for the basic_big_int front end. They are independent of the
// kernels above, which stay untouched as a fixed yardstick.

// dst[0..a.size()) = a + b, returns the carry out of the top limb. Requires a.size() >= b.size() and
// dst.size() >= a.size(). `dst` may equal `a` exactly (in-place add); otherwise it must not overlap `a`.
// Once the carry dies the rest of `a` is copied (skipped entirely when dst == a).
[[nodiscard]] constexpr bool add_n_tail(const std::span<uint_multiprecision_t>       dst,
                                        const std::span<const uint_multiprecision_t> a,
                                        const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(dst.size() >= a.size());
    const std::size_t common = b.size();
    const std::size_t total  = a.size();
    bool              carry  = false;
    std::size_t       i      = 0;
#if !defined(__GNUC__) || defined(__clang__)
    // GCC re-materializes the carry flag per limb in an unrolled chain and runs the one-limb loop below faster.
    for (; i + 4 <= common; i += 4) {
        const auto [v0, c0] = carrying_add(a[i + 0], b[i + 0], carry);
        const auto [v1, c1] = carrying_add(a[i + 1], b[i + 1], c0);
        const auto [v2, c2] = carrying_add(a[i + 2], b[i + 2], c1);
        const auto [v3, c3] = carrying_add(a[i + 3], b[i + 3], c2);
        dst[i + 0]          = v0;
        dst[i + 1]          = v1;
        dst[i + 2]          = v2;
        dst[i + 3]          = v3;
        carry               = c3;
    }
#endif
    for (; i < common; ++i) {
        const auto [v, c] = carrying_add(a[i], b[i], carry);
        dst[i]            = v;
        carry             = c;
    }
    while (i < total && carry) {
        const auto [v, c] = carrying_add(a[i], uint_multiprecision_t{0}, true);
        dst[i]            = v;
        carry             = c;
        ++i;
    }
    if (dst.data() != a.data()) {
        for (; i < total; ++i) {
            dst[i] = a[i];
        }
    }
    return carry;
}

// dst[0..a.size()) = a - b, returns the borrow out of the top limb. Same contract as `add_n_tail`.
[[nodiscard]] constexpr bool sub_n_tail(const std::span<uint_multiprecision_t>       dst,
                                        const std::span<const uint_multiprecision_t> a,
                                        const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(dst.size() >= a.size());
    const std::size_t common = b.size();
    const std::size_t total  = a.size();
    bool              borrow = false;
    std::size_t       i      = 0;
#if !defined(__GNUC__) || defined(__clang__)
    for (; i + 4 <= common; i += 4) {
        const auto [v0, b0] = borrowing_sub(a[i + 0], b[i + 0], borrow);
        const auto [v1, b1] = borrowing_sub(a[i + 1], b[i + 1], b0);
        const auto [v2, b2] = borrowing_sub(a[i + 2], b[i + 2], b1);
        const auto [v3, b3] = borrowing_sub(a[i + 3], b[i + 3], b2);
        dst[i + 0]          = v0;
        dst[i + 1]          = v1;
        dst[i + 2]          = v2;
        dst[i + 3]          = v3;
        borrow              = b3;
    }
#endif
    for (; i < common; ++i) {
        const auto [v, br] = borrowing_sub(a[i], b[i], borrow);
        dst[i]             = v;
        borrow             = br;
    }
    while (i < total && borrow) {
        const auto [v, br] = borrowing_sub(a[i], uint_multiprecision_t{0}, true);
        dst[i]             = v;
        borrow             = br;
        ++i;
    }
    if (dst.data() != a.data()) {
        for (; i < total; ++i) {
            dst[i] = a[i];
        }
    }
    return borrow;
}

// dst[0..n) = src[0..n) << bits for bits in [1, limb_width), returning the bits shifted out of the top
// limb. Walks top-down, so it is valid when dst >= src (including dst == src) and the ranges do not
// otherwise overlap.
constexpr uint_multiprecision_t lshift_copy(uint_multiprecision_t* const       dst,
                                            const uint_multiprecision_t* const src,
                                            const std::size_t                  n,
                                            const unsigned                     bits) noexcept {
    constexpr unsigned limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(bits >= 1 && bits < limb_bits);
    if (n == 0) {
        return 0;
    }
    const unsigned back = limb_bits - bits;
#if defined(__GNUC__) && !defined(__clang__)
    // GCC reloads src[i - 1] each iteration when dst may alias src; carry it in a register.
    // Clang compiles the two-load form below better.
    uint_multiprecision_t       hi  = src[n - 1];
    const uint_multiprecision_t out = hi >> back;
    for (std::size_t i = n - 1; i > 0; --i) {
        const uint_multiprecision_t lo = src[i - 1];
        dst[i]                         = (hi << bits) | (lo >> back);
        hi                             = lo;
    }
    dst[0] = hi << bits;
#else
    const uint_multiprecision_t out = src[n - 1] >> back;
    for (std::size_t i = n - 1; i > 0; --i) {
        dst[i] = (src[i] << bits) | (src[i - 1] >> back);
    }
    dst[0] = src[0] << bits;
#endif
    return out;
}

// dst[0..n) = src[0..n) >> bits for bits in [1, limb_width), returning the bits shifted out of the bottom
// limb (left-aligned in the returned limb). Walks bottom-up, so it is valid when dst <= src (including
// dst == src) and the ranges do not otherwise overlap.
constexpr uint_multiprecision_t rshift_copy(uint_multiprecision_t* const       dst,
                                            const uint_multiprecision_t* const src,
                                            const std::size_t                  n,
                                            const unsigned                     bits) noexcept {
    constexpr unsigned limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(bits >= 1 && bits < limb_bits);
    if (n == 0) {
        return 0;
    }
    const unsigned back = limb_bits - bits;
#if defined(__GNUC__) && !defined(__clang__)
    // See lshift_copy.
    uint_multiprecision_t       lo  = src[0];
    const uint_multiprecision_t out = lo << back;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const uint_multiprecision_t hi = src[i + 1];
        dst[i]                         = (lo >> bits) | (hi << back);
        lo                             = hi;
    }
    dst[n - 1] = lo >> bits;
#else
    const uint_multiprecision_t out = src[0] << back;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        dst[i] = (src[i] >> bits) | (src[i + 1] << back);
    }
    dst[n - 1] = src[n - 1] >> bits;
#endif
    return out;
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_SPAN_OPS_HPP
