// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_GCD_IMPL_HPP
#define BEMAN_BIG_INT_GCD_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <bit>
    #include <compare>
    #include <cstddef>
    #include <span>
    #include <utility>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Greatest common divisor of two single limbs, by the binary (Stein) algorithm:
// pull out the common power of two, then keep both operands odd and replace the
// larger with their difference. Division-free, so this is the fast path for
// every pair of operands that fits a limb -- including the tail of the
// multi-limb algorithm below.
// Both operands must be nonzero.
[[nodiscard]] constexpr uint_multiprecision_t gcd_limbs(uint_multiprecision_t u, uint_multiprecision_t v) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(u != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(v != 0);

    const auto common = static_cast<unsigned>(std::countr_zero(u | v));
    u >>= static_cast<unsigned>(std::countr_zero(u));
    do {
        v >>= static_cast<unsigned>(std::countr_zero(v));
        if (u > v) {
            std::swap(u, v);
        }
        v -= u; // Even: both operands were odd.
    } while (v != 0);

    // The result divides both operands, so restoring the common power of two
    // cannot overflow the limb.
    return u << common;
}

// Greatest common divisor of a multi-limb magnitude and a single nonzero limb:
// gcd(x, d) == gcd(x mod d, d), and a zero remainder means d divides x.
[[nodiscard]] constexpr uint_multiprecision_t gcd_short(const std::span<const uint_multiprecision_t> x,
                                                        const uint_multiprecision_t                  d) noexcept {
    const uint_multiprecision_t r = mod_unsigned_short(x, d);
    return r == 0 ? d : gcd_limbs(r, d);
}

// One of the two magnitudes the multi-limb algorithm works on, paired with the
// buffer that holds it. The buffers are interchangeable, so a reduction step can
// hand back its results by swapping whole operands rather than copying limbs.
struct gcd_operand {
    std::span<uint_multiprecision_t> buf;
    std::size_t                      size;

    [[nodiscard]] constexpr std::span<uint_multiprecision_t> value() const noexcept { return buf.first(size); }
};

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT

// Cofactors of the Euclidean steps a Lehmer simulation was able to certify.
// Only magnitudes are tracked: the signs alternate with the step count, so
// `steps` tells the multi-precision update which way each pair subtracts.
// `steps == 1` means not even the first quotient could be certified.
struct lehmer_cofactors {
    uint_multiprecision_t x0, x1; // cofactors of the larger operand
    uint_multiprecision_t y0, y1; // cofactors of the smaller operand
    std::size_t           steps;
};

// Runs the Euclidean algorithm on the leading 2 * limb_bits bits of a pair of
// magnitudes and accumulates the cofactors of every step whose quotient is
// provably the quotient of the full-precision pair.
//
// This is Lehmer's algorithm in Jebelean's double-digit form: the quotient of a
// simulated step is found by repeated subtraction, because consecutive
// remainders differ by a small factor far more often than not, and Jebelean's
// exact test decides when the leading bits stop determining the quotient. The
// cofactors are capped at a single limb so that the caller's update over the
// full operands stays a single-limb multiply.
//   T. Jebelean, "A Double-Digit Lehmer-Euclid Algorithm for Finding the GCD of
//   Long Integers", J. Symbolic Computation 19 (1995) 145.
//   J. Sorenson, "An Analysis of Lehmer's Euclidean GCD Algorithm" (1995),
//   doi:10.1145/220346.220378.
//
// Preconditions: u >= v > 0. Leading bits that are equal certify nothing, so
// that case simply reports back a single (uncertified) step.
[[nodiscard]] constexpr lehmer_cofactors lehmer_simulate(uint_wide_t u, uint_wide_t v) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(u >= v);
    BEMAN_BIG_INT_DEBUG_ASSERT(v > 0);

    // Past this many subtractions the quotient is large enough to be worth one
    // double-limb division.
    constexpr unsigned subtract_limit = 30;

    uint_multiprecision_t x0 = 1, x1 = 0;
    uint_multiprecision_t y0 = 0, y1 = 1;
    std::size_t           steps = 0;

    for (;;) {
        // u mod v, with the quotient accumulated as it goes. A handful of
        // subtractions beats a double-limb division, which is a library call on
        // most targets; a wide quotient falls back to one division.
        uint_wide_t q = 1;
        u -= v;
        while (u >= v) {
            u -= v;
            if (++q > uint_wide_t{subtract_limit}) {
                const uint_wide_t rest = u / v;
                u -= rest * v;
                q += rest;
            }
        }
        std::swap(u, v);
        ++steps;

        // x1 <= y1 always, so overflowing y is the binding constraint.
        const uint_wide_t y2 = static_cast<uint_wide_t>(y0) + q * static_cast<uint_wide_t>(y1);
        if ((y2 >> limb_bits) != 0) {
            break;
        }
        const auto x2 =
            static_cast<uint_multiprecision_t>(static_cast<uint_wide_t>(x0) + q * static_cast<uint_wide_t>(x1));
        const auto y2_limb = static_cast<uint_multiprecision_t>(y2);

        // Jebelean's exact termination conditions. The cofactor pair whose sign
        // is positive alternates with the step count, hence the two forms.
        if ((steps & 1U) == 0) {
            if (v < static_cast<uint_wide_t>(x2) ||
                (u - v) < static_cast<uint_wide_t>(y2_limb) + static_cast<uint_wide_t>(y1)) {
                break;
            }
        } else {
            if (v < static_cast<uint_wide_t>(y2_limb) ||
                (u - v) < static_cast<uint_wide_t>(x2) + static_cast<uint_wide_t>(x1)) {
                break;
            }
        }

        x0 = x1;
        x1 = x2;
        y0 = y1;
        y1 = y2_limb;
    }

    return {.x0 = x0, .x1 = x1, .y0 = y0, .y1 = y1, .steps = steps};
}

// Replaces (u, v) with the pair that the Euclidean steps certified by a Lehmer
// simulation of their leading bits would produce, at the cost of two
// single-limb multiplies over the full operands instead of one division per
// step. Returns false when the leading bits certify nothing, in which case the
// operands are untouched and the caller falls back to a binary step.
//
// `t` is scratch space. On success the three buffers rotate: whichever buffer
// the new smaller operand did not land in becomes the new scratch.
//
// Preconditions: u > v > 0, u.size >= 3, and all three buffers hold at least
// u.size + 1 limbs.
[[nodiscard]] constexpr bool gcd_lehmer_step(gcd_operand& u, gcd_operand& v, gcd_operand& t) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(u.size >= 3);
    BEMAN_BIG_INT_DEBUG_ASSERT(u.buf.size() > u.size);
    BEMAN_BIG_INT_DEBUG_ASSERT(v.buf.size() > u.size);
    BEMAN_BIG_INT_DEBUG_ASSERT(t.buf.size() > u.size);

    const std::size_t n     = u.size;
    const auto        align = static_cast<std::size_t>(std::countl_zero(u.buf[n - 1]));

    // The leading 2 * limb_bits bits of each operand, both read at the larger
    // operand's limb positions so that the two windows keep their relative
    // magnitude. `v` may be shorter, in which case its window is padded.
    const auto window = [n, align](const gcd_operand& x) -> uint_wide_t {
        const auto  limbs = std::span<const uint_multiprecision_t>{x.value()};
        uint_wide_t w     = (static_cast<uint_wide_t>(limb_or_zero(limbs, n - 1)) << limb_bits) |
                            static_cast<uint_wide_t>(limb_or_zero(limbs, n - 2));
        if (align != 0) {
            w = (w << align) | static_cast<uint_wide_t>(limb_or_zero(limbs, n - 3) >> (limb_bits - align));
        }
        return w;
    };

    const uint_wide_t u_window = window(u);
    const uint_wide_t v_window = window(v);
    if (v_window == 0) {
        return false; // The operands are too far apart for their leading bits to say anything.
    }

    const lehmer_cofactors cof = lehmer_simulate(u_window, v_window);
    if (cof.steps <= 1) {
        return false;
    }

    // With `steps` even the certified cofactor signs make the new larger operand
    // y0 * v - x0 * u and the new smaller one x1 * u - y1 * v; with `steps` odd
    // the roles of the two pairs swap. Either way one result has the form
    // (multiple of u) - (multiple of v), which is computed in u's own buffer,
    // and the other is its mirror, which goes to the scratch buffer.
    const bool even  = (cof.steps & 1U) == 0;
    const auto x_own = even ? cof.x1 : cof.x0; // u's coefficient in u's buffer
    const auto y_own = even ? cof.y1 : cof.y0;
    const auto y_t   = even ? cof.y0 : cof.y1; // v's coefficient in the scratch buffer
    const auto x_t   = even ? cof.x0 : cof.x1;
    BEMAN_BIG_INT_DEBUG_ASSERT(x_own != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(y_t != 0);

    const std::size_t width = n + 1;

    // Scratch first: it reads both operands, while the in-place update below
    // overwrites u.
    const std::size_t nt = multiply_single_limb(t.buf, v.value(), y_t);
    std::fill(t.buf.begin() + static_cast<std::ptrdiff_t>(nt),
              t.buf.begin() + static_cast<std::ptrdiff_t>(width),
              uint_multiprecision_t{0});
    if (x_t != 0) { // Zero on the first certified step, where the new operand is just v.
        submul_single_limb_wide(t.buf.first(width), u.value(), x_t);
    }
    t.size = trimmed_size_span(t.buf.first(width));

    const std::size_t nu = mul_add_single_limb_in_place(u.buf, n, x_own, 0);
    std::fill(u.buf.begin() + static_cast<std::ptrdiff_t>(nu),
              u.buf.begin() + static_cast<std::ptrdiff_t>(width),
              uint_multiprecision_t{0});
    submul_single_limb_wide(u.buf.first(width), v.value(), y_own);
    u.size = trimmed_size_span(u.buf.first(width));

    // u's buffer now holds the larger of the two new operands when `steps` is
    // odd, and the smaller when it is even; v's buffer is free either way.
    if (even) {
        std::swap(u, t); // u <- scratch (the new larger operand)
        std::swap(v, t); // v <- the old u buffer, t <- the free v buffer
    } else {
        std::swap(v, t); // v <- scratch, t <- the free v buffer
    }
    return true;
}

#endif // BEMAN_BIG_INT_HAS_WIDE_INT

// ---------------------------------------------------------------------------
// Greatest common divisor of two multi-limb magnitudes, computed in place in the
// operands' own buffers.
//
// Each pass reduces the pair with the best step available: Lehmer's algorithm
// turns the leading bits into a run of Euclidean steps that costs two
// single-limb multiplies over the operands, and a binary (Stein) step -- one
// subtraction and one shift -- carries the reduction whenever the leading bits
// certify nothing. Once the smaller operand fits a limb, one short division plus
// the scalar loop finish the job.
//
// The result is left in `u`; its limb count is returned.
//
// Preconditions:
//   - u[0..nu) and v[0..nv) are nonzero magnitudes with nu <= u.size() and
//     nv <= v.size(), and the two buffers do not overlap
//   - both buffers can hold the result, which never exceeds min(u, v) -- so any
//     buffer that held one of the operands is large enough
//   - `scratch` either is empty, which limits the reduction to binary steps, or
//     does not overlap the operands and holds at least max(nu, nv) + 1 limbs,
//     as must the operands' own buffers
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::size_t gcd_unsigned_spans(const std::span<uint_multiprecision_t> u_buf,
                                                       const std::size_t                      nu,
                                                       const std::span<uint_multiprecision_t> v_buf,
                                                       const std::size_t                      nv,
                                                       const std::span<uint_multiprecision_t> scratch = {}) noexcept {
    const uint_multiprecision_t* const result_buf = u_buf.data();

    gcd_operand                  u{.buf = u_buf, .size = nu};
    gcd_operand                  v{.buf = v_buf, .size = nv};
    [[maybe_unused]] gcd_operand t{.buf = scratch, .size = 0}; // Unused without Lehmer's reduction.

    // Only the binary step needs odd operands, so factors of two are shifted out
    // there rather than up front. The part common to both operands is restored at
    // the end: gcd(2^i * a, 2^j * b) == 2^min(i,j) * gcd(a, b) for odd a and b,
    // because the leftover factor of two on one side is coprime to the odd other
    // side. Lehmer's steps preserve the gcd exactly and so need no accounting.
    std::size_t common = 0;

    for (;;) {
        const std::strong_ordering cmp = compare_unsigned_spans(u.value(), v.value());
        if (cmp == std::strong_ordering::equal) {
            break; // Equal operands: their common value is the result.
        }
        if (cmp == std::strong_ordering::less) {
            std::swap(u, v);
        }

        if (v.size == 1) {
            u.buf[0] = gcd_short(u.value(), v.buf[0]);
            u.size   = 1;
            break;
        }

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT
        if (u.size >= 3 && !t.buf.empty() && gcd_lehmer_step(u, v, t)) {
            if (is_span_zero(v.value())) {
                break; // The reduction divided out exactly.
            }
            continue;
        }
#endif // BEMAN_BIG_INT_HAS_WIDE_INT

        // Binary step: make both operands odd, then subtract. Their difference is
        // even, so the next pass shifts out at least one bit.
        const std::size_t u_twos = trailing_zero_bits_span(u.value());
        const std::size_t v_twos = trailing_zero_bits_span(v.value());
        common += std::min(u_twos, v_twos);
        u.size = shift_right_bits(u.buf, u.size, u_twos);
        v.size = shift_right_bits(v.buf, v.size, v_twos);

        const std::strong_ordering odd_cmp = compare_unsigned_spans(u.value(), v.value());
        if (odd_cmp == std::strong_ordering::equal) {
            break;
        }
        if (odd_cmp == std::strong_ordering::less) {
            std::swap(u, v);
        }
        u.size = subtract_unsigned_spans(u.value(), u.value(), v.value());
    }

    u.size = shift_left_bits(u.buf, u.size, common);
    if (u.buf.data() != result_buf) {
        std::copy_n(u.buf.begin(), u.size, u_buf.begin());
    }
    return u.size;
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_GCD_IMPL_HPP
