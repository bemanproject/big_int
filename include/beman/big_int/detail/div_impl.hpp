// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_DIV_IMPL_HPP
#define BEMAN_BIG_INT_DIV_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/mul_impl.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <bit>
    #include <compare>
    #include <cstddef>
    #include <limits>
    #include <span>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Selects whether a division routine should yield the quotient or the remainder.
enum class division_op : unsigned char {
    // Compute only the quotient.
    div,
    // Compute only the remainder.
    rem,
    // Compute quotient and remainder simultaneously.
    div_rem,
};

// Scratch limbs divide_unsigned needs internally: the shifted dividend copy
// (dividend.size() + 1) plus the shifted divisor copy (divisor.size();
// allocated only when the divisor is unnormalized, but budgeted always).
constexpr std::size_t divide_unsigned_storage_size(const std::size_t dividend_limbs,
                                                   const std::size_t divisor_limbs) noexcept {
    return dividend_limbs + 1 + divisor_limbs;
}

// ---------------------------------------------------------------------------
// Multi-limb schoolbook division: Knuth Algorithm D with the Moller-Granlund
// 3/2 quotient step. The divisor's top bit is normalized into place once,
// then each quotient digit costs one div_3by2_preinv on the top three
// remainder limbs plus one fused submul pass over the rest; the add-back
// correction triggers with probability ~1/B per digit.
//
// Writes the quotient to `quotient` and the remainder to `remainder`; both
// output spans are fully written (zero above the significant limbs).
// Preconditions: trimmed operands, divisor.size() >= 2, dividend.size() >=
// divisor.size(), quotient.size() >= dividend.size() - divisor.size() + 1,
// remainder.size() >= dividend.size() + 1, no aliasing, and `scratch`
// provides at least divide_unsigned_storage_size(...) limbs.
// ---------------------------------------------------------------------------
constexpr void divide_unsigned(const std::span<uint_multiprecision_t>       quotient,
                               const std::span<uint_multiprecision_t>       remainder,
                               const std::span<const uint_multiprecision_t> dividend,
                               const std::span<const uint_multiprecision_t> divisor,
                               scratch_allocator_base&                      scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.size() >= dividend.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != dividend.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != divisor.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.data() != dividend.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.data() != divisor.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != remainder.data());

    const std::size_t n = divisor.size();
    const std::size_t m = dividend.size();

    const unsigned shift = static_cast<unsigned>(std::countl_zero(divisor.back()));

    // Normalized divisor view (top bit set).
    std::span<const uint_multiprecision_t> d = divisor;
    if (shift != 0) {
        const std::span<uint_multiprecision_t> d_norm = scratch.allocate(n);
        std::ranges::copy(divisor, d_norm.begin());
        const std::size_t d_size = shift_left_n(d_norm, n, shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(d_size == n);
        d = d_norm.first(d_size);
    }
    const uint_multiprecision_t d1   = d[n - 1];
    const uint_multiprecision_t d0   = d[n - 2];
    const uint_multiprecision_t dinv = reciprocal_word_3by2(d1, d0);

    // Working copy of the dividend, shifted in step with the divisor and
    // extended by one limb; reduced in place to the remainder.
    const std::span<uint_multiprecision_t> u = scratch.allocate(m + 1);
    std::ranges::copy(dividend, u.begin());
    u[m] = 0;
    if (shift != 0) {
        const std::size_t u_size = shift_left_n(u, m, shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(u_size <= m + 1);
    }

    // Loop invariant entering digit j: the remainder so far is the n-limb
    // value <top : u[j+n-1 .. j+1]> and is strictly below the divisor (at
    // entry the extended top limb u[m] is below 2^shift <= B/2 <= d1).
    uint_multiprecision_t top = u[m];
    for (std::size_t j = m - n + 1; j-- > 0;) {
        uint_multiprecision_t q;
        if (top == d1 && u[j + n - 1] == d0) {
            // Saturated estimate: with the window's top pair equal to the
            // divisor's, q = B - 1 subtracts cleanly into [0, d) with no
            // correction (window < B*d and window >= (B-1)*d here).
            q                              = ~uint_multiprecision_t{0};
            const uint_multiprecision_t cy = submul_single_limb(u.subspan(j, n), d, q);
            BEMAN_BIG_INT_DEBUG_ASSERT(cy == top);
            top = u[j + n - 1];
        } else {
            const auto step = div_3by2_preinv(top, u[j + n - 1], u[j + n - 2], d1, d0, dinv);
            q               = step.quotient;

            uint_multiprecision_t r1 = step.remainder.high_bits;
            uint_multiprecision_t r0 = step.remainder.low_bits;

            if (n > 2) {
                // Subtract q times the divisor's low limbs; the borrow lands
                // at the position r0 tracks.
                const uint_multiprecision_t cy = submul_single_limb(u.subspan(j, n - 2), d.first(n - 2), q);
                const auto [s0, b0]            = borrowing_sub(r0, cy);
                r0                             = s0;
                const auto [s1, b1]            = borrowing_sub(r1, uint_multiprecision_t{0}, b0);
                r1                             = s1;
                if (b1) {
                    // The 3/2 estimate is one too large (probability ~1/B):
                    // add the divisor back once.
                    --q;
                    const bool carry    = add_unsigned_spans(u.subspan(j, n - 2), u.subspan(j, n - 2), d.first(n - 2));
                    const auto [a0, c0] = carrying_add(r0, d0, carry);
                    r0                  = a0;
                    const auto [a1, c1] = carrying_add(r1, d1, c0);
                    r1                  = a1;
                    BEMAN_BIG_INT_DEBUG_ASSERT(c1);
                }
            }
            u[j + n - 2] = r0;
            top          = r1;
        }
        quotient[j] = q;
    }

    // Remainder: materialize the in-register top limb, undo the
    // normalization shift, and copy out.
    u[n - 1] = top;
    if (shift != 0) {
        const uint_multiprecision_t dropped = shift_right_n(u.first(n), shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
    }
    std::ranges::copy(u.first(n), remainder.begin());
    std::ranges::fill(remainder.subspan(n), uint_multiprecision_t{0});
    std::ranges::fill(quotient.subspan(m - n + 1), uint_multiprecision_t{0});

    scratch.deallocate(m + 1);
    if (shift != 0) {
        scratch.deallocate(n);
    }
}

// ---------------------------------------------------------------------------
// Approximate schoolbook quotient (the GMP sbpi1_divappr_q idea): the same
// normalized Knuth-D / 3-by-2 loop as divide_unsigned, but the divisor is
// truncated up front to one limb more than the quotient length, and once
// the quotient has as many digits left as the truncated divisor the update
// window shrinks by a divisor limb per digit instead of marching -- the low
// remainder limbs that exact division maintains are simply never touched,
// making the tail triangular instead of rectangular. No remainder is
// produced.
//
// Both truncations only ever make the computed digits too LARGE (low
// divisor limbs that would have been subtracted are dropped), and once the
// running top limb can no longer prove a digit exact the remaining digits
// saturate to B - 1; the classical bound for this construction is
//   q <= q' <= q + 1
// which the differential suite asserts. Preconditions: as divide_unsigned,
// plus divisor.size() >= 3 and dividend.size() > divisor.size().
// ---------------------------------------------------------------------------
constexpr void divide_unsigned_approx(const std::span<uint_multiprecision_t>       quotient,
                                      const std::span<const uint_multiprecision_t> dividend,
                                      const std::span<const uint_multiprecision_t> divisor,
                                      scratch_allocator_base&                      scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 3);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() > divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != dividend.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != divisor.data());

    const std::size_t n = divisor.size();
    const std::size_t m = dividend.size();

    const unsigned shift = static_cast<unsigned>(std::countl_zero(divisor.back()));

    // Normalized divisor view (top bit set).
    std::span<const uint_multiprecision_t> d = divisor;
    if (shift != 0) {
        const std::span<uint_multiprecision_t> d_norm = scratch.allocate(n);
        std::ranges::copy(divisor, d_norm.begin());
        const std::size_t d_size = shift_left_n(d_norm, n, shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(d_size == n);
        d = d_norm.first(d_size);
    }
    const uint_multiprecision_t d1   = d[n - 1];
    const uint_multiprecision_t d0   = d[n - 2];
    const uint_multiprecision_t dinv = reciprocal_word_3by2(d1, d0);

    // Only the top t divisor limbs can influence the quotient by more than
    // one ulp. Every digit here is a full 3-by-2 step (there is no separate
    // 0/1 top-compare digit), so the divisor must keep one limb more than
    // the m - n + 1 quotient digits: the dropped tail E < B^(n-t) then
    // contributes q * E / D < 2 * B^((m-n+1) - t) <= 2/B to the quotient.
    // The digit windows below align so the effective divisor keeps its top
    // two limbs (d1, d0) at every step.
    const std::size_t t = std::min(n, m - n + 2);

    // Working copy of the dividend, shifted in step with the divisor and
    // extended by one limb. Only the limbs at and above the fixed tail base
    // n - 2 are ever updated.
    const std::span<uint_multiprecision_t> u = scratch.allocate(m + 1);
    std::ranges::copy(dividend, u.begin());
    u[m] = 0;
    if (shift != 0) {
        const std::size_t u_size = shift_left_n(u, m, shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(u_size <= m + 1);
    }

    uint_multiprecision_t top = u[m];

    // Full-width digits (j = m - n down to t - 1): the window
    // u[j + n - t .. j + n) still covers the whole effective divisor, so the
    // body is divide_unsigned's with the operands' low limbs cut off.
    for (std::size_t j = m - n; j + 1 >= t; --j) {
        const std::size_t     jb = j + (n - t); // window base
        uint_multiprecision_t q;
        if (top == d1 && u[j + n - 1] == d0) {
            q                              = ~uint_multiprecision_t{0};
            const uint_multiprecision_t cy = submul_single_limb(u.subspan(jb, t), d.subspan(n - t, t), q);
            BEMAN_BIG_INT_DEBUG_ASSERT(cy == top);
            top = u[j + n - 1];
        } else {
            const auto step = div_3by2_preinv(top, u[j + n - 1], u[j + n - 2], d1, d0, dinv);
            q               = step.quotient;

            uint_multiprecision_t r1 = step.remainder.high_bits;
            uint_multiprecision_t r0 = step.remainder.low_bits;

            if (t > 2) {
                const uint_multiprecision_t cy = submul_single_limb(u.subspan(jb, t - 2), d.subspan(n - t, t - 2), q);
                const auto [s0, b0]            = borrowing_sub(r0, cy);
                r0                             = s0;
                const auto [s1, b1]            = borrowing_sub(r1, uint_multiprecision_t{0}, b0);
                r1                             = s1;
                if (b1) {
                    --q;
                    const bool carry =
                        add_unsigned_spans(u.subspan(jb, t - 2), u.subspan(jb, t - 2), d.subspan(n - t, t - 2));
                    const auto [a0, c0] = carrying_add(r0, d0, carry);
                    r0                  = a0;
                    const auto [a1, c1] = carrying_add(r1, d1, c0);
                    r1                  = a1;
                    BEMAN_BIG_INT_DEBUG_ASSERT(c1);
                }
            }
            u[j + n - 2] = r0;
            top          = r1;
        }
        quotient[j] = q;
    }

    // Shrinking tail: digit j uses the top j + 2 divisor limbs over the
    // fixed-base window u[n - 2 .. j + n); each digit drops one more low
    // divisor limb. Once `exact` goes false the top limb can no longer
    // distinguish a correct digit from an overshoot and every remaining
    // digit saturates -- by then only the +1 final-quotient slack is left.
    bool exact = true;
    for (std::size_t j = t - 2; j != 0; --j) {
        uint_multiprecision_t q;
        if (!exact || top >= d1) {
            q = ~uint_multiprecision_t{0};
            const uint_multiprecision_t cy =
                submul_single_limb(u.subspan(n - 2, j + 2), d.subspan(n - 2 - j, j + 2), q);
            if (top != cy) {
                if (exact && top < cy) {
                    // Saturation overshot: one add-back restores the window.
                    --q;
                    const bool carry = add_unsigned_spans(
                        u.subspan(n - 2, j + 2), u.subspan(n - 2, j + 2), d.subspan(n - 2 - j, j + 2));
                    BEMAN_BIG_INT_DEBUG_ASSERT(carry);
                } else {
                    // The remainder no longer fits the window: every digit
                    // from here down is at least B - 1 up to the +1 slack.
                    exact = false;
                }
            }
            top = u[j + n - 1];
        } else {
            const auto step = div_3by2_preinv(top, u[j + n - 1], u[j + n - 2], d1, d0, dinv);
            q               = step.quotient;

            uint_multiprecision_t r1 = step.remainder.high_bits;
            uint_multiprecision_t r0 = step.remainder.low_bits;

            const uint_multiprecision_t cy = submul_single_limb(u.subspan(n - 2, j), d.subspan(n - 2 - j, j), q);
            const auto [s0, b0]            = borrowing_sub(r0, cy);
            r0                             = s0;
            const auto [s1, b1]            = borrowing_sub(r1, uint_multiprecision_t{0}, b0);
            r1                             = s1;
            if (b1) {
                --q;
                const bool carry =
                    add_unsigned_spans(u.subspan(n - 2, j), u.subspan(n - 2, j), d.subspan(n - 2 - j, j));
                const auto [a0, c0] = carrying_add(r0, d0, carry);
                r0                  = a0;
                const auto [a1, c1] = carrying_add(r1, d1, c0);
                r1                  = a1;
                BEMAN_BIG_INT_DEBUG_ASSERT(c1);
            }
            u[j + n - 2] = r0;
            top          = r1;
        }
        quotient[j] = q;
    }

    // Final digit: a bare 3-by-2 against (d1, d0).
    {
        uint_multiprecision_t q;
        if (!exact || top >= d1) {
            q                              = ~uint_multiprecision_t{0};
            const uint_multiprecision_t cy = submul_single_limb(u.subspan(n - 2, 2), d.subspan(n - 2, 2), q);
            if (top != cy && exact && top < cy) {
                --q;
            }
        } else {
            const auto step = div_3by2_preinv(top, u[n - 1], u[n - 2], d1, d0, dinv);
            q               = step.quotient;
        }
        quotient[0] = q;
    }

    std::ranges::fill(quotient.subspan(m - n + 1), uint_multiprecision_t{0});

    scratch.deallocate(m + 1);
    if (shift != 0) {
        scratch.deallocate(n);
    }
}

// ---------------------------------------------------------------------------
// Burnikel-Ziegler divide-and-conquer division.
// Reference: C. Burnikel, J. Ziegler, "Fast Recursive Division",
// MPI-I-98-1-022 (1998); structure mirrors OpenJDK's MutableBigInteger.
//
// Two mutually recursive routines (D_2n/n and D_3n/2n) reduce division to
// half-sized multiplications, so D(n) ~= 2*M(n) and the products ride the
// Karatsuba/Toom/FFT tiers through multiply_dispatch.
// ---------------------------------------------------------------------------

// Minimum divisor limbs for the divide-and-conquer path, and the minimum
// quotient length (dividend limbs - divisor limbs) for entering it; below
// either bound the schoolbook kernel wins. Java's BigInteger draws the same
// divisor line at 80 32-bit words. Tuned via division_stress_bench medians
// and direct probes, per architecture (2026-06-10):
//   - AArch64 (M4 Max): parity right at 40/10, re-confirmed 2026-09-30 (s = 8..384
//     plus large balanced shapes): cutoffs 20-32 speed divrem by 3-4% but slow
//     div by 26-31% at s = 48-144; offset 6 speeds div by 10.8% but slows divrem
//     by 4.6% (worst +75% at q = 6). Beyond the gates
//     divide-and-conquer pulls ahead monotonically: ~1.2x at a 2x-cutoff balanced
//     division, ~2.2x at 512 limbs, ~7.5x at 8192. Measured on M4 only, also used
//     for other AArch64 cores and MSVC ARM64.
//   - x86-64 (11900K): the preinv basecase rides the 1-instruction 64x64
//     mul and fast div so far that divide-and-conquer first wins a balanced
//     shape at ~144-160 divisor limbs (1.02-1.06; schoolbook wins 0.86-0.99
//     through 128) and needs ~64 quotient limbs at a 192-256-limb divisor
//     (0.94 at 48, 1.01 at 64). The same constant is the recursion leaf,
//     consistent with the same data.
// The 32-bit-limb values are scaled from the 64-bit AArch64 line (Java's
// precedent); no 32-bit hardware target has been measured.
#if BEMAN_BIG_INT_LIMB_WIDTH == 64
    #if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t burnikel_ziegler_cutoff = 40;
inline constexpr std::size_t burnikel_ziegler_offset = 10;
    #elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t burnikel_ziegler_cutoff = 160;
inline constexpr std::size_t burnikel_ziegler_offset = 64;
    #else
inline constexpr std::size_t burnikel_ziegler_cutoff = 40;
inline constexpr std::size_t burnikel_ziegler_offset = 10;
    #endif
#else
inline constexpr std::size_t burnikel_ziegler_cutoff = 80;
inline constexpr std::size_t burnikel_ziegler_offset = 20;
#endif

static_assert(burnikel_ziegler_cutoff >= 2,
              "burnikel_ziegler_cutoff < 2 lets the recursion reach leaves that break the "
              "divide_unsigned preconditions");

// Scratch upper bound for divide_burnikel_ziegler with padded divisor length
// `block_limbs` (n), `blocks` (t) dividend blocks, and recursion threshold
// `threshold`:
//   - normalized divisor:        n limbs
//   - shifted working dividend:  t*n limbs
//   - quotient blocks:           (t-1)*n limbs
//   - recursion high-water:      max(n, 6*threshold + 3) limbs
// The recursion term is exact by construction: a D_3n/2n frame allocates its
// single q*b2 product (2h <= n limbs) only after its recursive call has fully
// returned and rewound (LIFO), so product buffers never stack; a leaf needs
// q(j+1) + r(2j+1) + divide_unsigned's internal (2j+2) + j with
// j <= threshold. Unlike the multiplication heuristics this bound is
// deterministic (the allocation sizes depend only on operand sizes, never
// values); instrumented peaks in division_scratch_peak.test.cpp match the
// model exactly, with the +8 slack as the only headroom.
constexpr std::size_t burnikel_ziegler_storage_size(const std::size_t block_limbs,
                                                    const std::size_t blocks,
                                                    const std::size_t threshold) noexcept {
    return 2 * blocks * block_limbs + std::max(block_limbs, 6 * threshold + 3) + 8;
}

// ---------------------------------------------------------------------------
// Leaf of the Burnikel-Ziegler recursion: divide the 2n-limb window `a` by
// the n-limb divisor `b` (b.back() != 0) with the schoolbook kernel.
// Precondition: value(a) < beta^n * value(b), so the quotient fits n limbs.
// Postconditions: a.first(n) holds the remainder, a.subspan(n) is zero, and
// all n limbs of `q` are written.
// ---------------------------------------------------------------------------
void divide_dc_basecase(std::span<uint_multiprecision_t>       a,
                        std::span<const uint_multiprecision_t> b,
                        std::span<uint_multiprecision_t>       q,
                        scratch_allocator_base&                scratch) noexcept;

// ---------------------------------------------------------------------------
// Burnikel-Ziegler D_2n/n: divide the 2n-limb window `a` by the n-limb
// normalized divisor `b` (top bit set) via two D_3n/2n calls on half blocks.
// Precondition: value(a) < beta^n * value(b), so the quotient fits n limbs.
// Postconditions: a.first(n) holds the remainder, a.subspan(n) is zero, and
// all n limbs of `q` are written.
// `threshold` propagates through the recursion (unlike the multiplication
// kernels' top-only cutoff_override) so tests can force deep recursion on
// small operands; production callers pass burnikel_ziegler_cutoff.
// ---------------------------------------------------------------------------
void divide_dc_2n1n(std::span<uint_multiprecision_t>       a,
                    std::span<const uint_multiprecision_t> b,
                    std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                scratch,
                    std::size_t                            threshold);

// ---------------------------------------------------------------------------
// Burnikel-Ziegler D_3n/2n: divide the 3h-limb window `v` by the 2h-limb
// normalized divisor `b` (top bit set): estimate the quotient from the high
// 2h limbs of `v` against b's high half, fix up with one h x h product and
// at most two add-back corrections.
// Precondition: value(v) < beta^h * value(b), so the quotient fits h limbs.
// Postconditions: v.first(2h) holds the remainder, v.subspan(2h) is zero,
// and all h limbs of `q` are written.
// ---------------------------------------------------------------------------
void divide_dc_3n2n(std::span<uint_multiprecision_t>       v,
                    std::span<const uint_multiprecision_t> b,
                    std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                scratch,
                    std::size_t                            threshold);

// ---------------------------------------------------------------------------
// Approximate D_2n/n (the GMP dcpi1_divappr_q_n idea in Burnikel-Ziegler
// dress): the high quotient half comes from the exact D_3n/2n on the top
// three quarter-blocks -- its remainder is needed to continue -- but the low
// half recurses approximately on that remainder's top 2h limbs against the
// divisor's high half only. The window's low quarter and the divisor's low
// half are never read below the top level, so the whole low subtree skips
// its remainder multiplies; that is the divappr saving.
//
// Same window contract as divide_dc_2n1n (a is 2n limbs with its high half
// strictly below b; all n quotient limbs written; `a` is consumed as
// workspace -- unlike the exact routine it does NOT leave a remainder).
// The computed quotient satisfies
//   q <= q' <= q + divappr_quotient_slack(n, threshold):
// each halving level divides by a divisor whose low half was dropped,
// contributing at most one quotient ulp on top of the recursion below it
// (GMP's flat +1 bound for this construction relies on threading an extra
// fraction limb through the recursion, which this shape does not carry).
// The differential suite measures the accumulation: observed maxima track
// the level count minus one, so the +2 here is honest headroom.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::size_t divappr_quotient_slack(const std::size_t n, const std::size_t threshold) noexcept {
    std::size_t levels = 0;
    std::size_t m      = n;
    while (m >= threshold && (m % 2) == 0) {
        m >>= 1;
        ++levels;
    }
    return levels + 2;
}

void divide_dc_divappr(std::span<uint_multiprecision_t>       a,
                       std::span<const uint_multiprecision_t> b,
                       std::span<uint_multiprecision_t>       q,
                       scratch_allocator_base&                scratch,
                       std::size_t                            threshold);

// Block-decomposition parameters of the divide_burnikel_ziegler driver,
// exposed so the scratch instrumentation test can size and probe the same
// workspace the production entry point uses.
struct burnikel_ziegler_params {
    std::size_t block_limbs; // n: divisor length padded to j * 2^k
    std::size_t blocks;      // t: dividend blocks marched by the driver
    std::size_t threshold;   // recursion leaf threshold in effect
};

// Computes (n, t, thr) for a trimmed dividend/divisor pair:
//   - n pads the divisor length to j * m_pow2 with m_pow2 the smallest power
//     of two for which j = ceil(s / m_pow2) is at or below the threshold, so
//     halving bottoms out at j-limb leaves;
//   - t is the dividend block count; its +1 keeps the shifted dividend's top
//     block strictly below beta^n / 2 <= b_hat, establishing the D_2n/n
//     precondition for the first window (remainder < b_hat maintains it for
//     every later window).
[[nodiscard]] constexpr burnikel_ziegler_params
burnikel_ziegler_plan_bits(const std::size_t                            dividend_bits,
                           const std::span<const uint_multiprecision_t> divisor,
                           const std::size_t                            threshold_override = 0) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t s   = divisor.size();
    const std::size_t thr = threshold_override != 0 ? threshold_override : burnikel_ziegler_cutoff;
    BEMAN_BIG_INT_DEBUG_ASSERT(thr >= 2);

    const std::size_t m_pow2 = std::size_t{1} << std::bit_width(s / thr);
    const std::size_t j      = (s + m_pow2 - 1) / m_pow2;
    const std::size_t n      = j * m_pow2;

    const std::size_t sigma =
        n * limb_bits - ((s - 1) * limb_bits + static_cast<std::size_t>(std::bit_width(divisor.back())));
    const std::size_t t = std::max<std::size_t>(2, (dividend_bits + sigma) / (n * limb_bits) + 1);

    return {.block_limbs = n, .blocks = t, .threshold = thr};
}

[[nodiscard]] constexpr burnikel_ziegler_params
burnikel_ziegler_plan(const std::span<const uint_multiprecision_t> dividend,
                      const std::span<const uint_multiprecision_t> divisor,
                      const std::size_t                            threshold_override = 0) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    return burnikel_ziegler_plan_bits((dividend.size() - 1) * limb_bits +
                                          static_cast<std::size_t>(std::bit_width(dividend.back())),
                                      divisor,
                                      threshold_override);
}

// ---------------------------------------------------------------------------
// Burnikel-Ziegler division driver (the paper's Algorithm 3): pad the divisor
// length to n = j * 2^k so halving bottoms out at j-limb leaves, normalize
// both operands by sigma = n * limb_bits - bitlen(divisor), then march
// fixed-size dividend blocks through divide_dc_2n1n from the top down, each
// window picking up the previous remainder as its high half.
// Same outer contract as divide_unsigned: trimmed inputs, divisor.size() >= 2,
// dividend.size() >= divisor.size(), quotient.size() >= dividend.size() -
// divisor.size() + 1, remainder.size() >= dividend.size() + 1, no aliasing;
// both output spans are fully written (zero above the significant limbs).
// This overload works in caller-provided scratch, which must hold at least
// burnikel_ziegler_storage_size(plan...) limbs for `plan` as computed by
// burnikel_ziegler_plan on the same operands.
// ---------------------------------------------------------------------------
void divide_burnikel_ziegler(std::span<uint_multiprecision_t>       quotient,
                             std::span<uint_multiprecision_t>       remainder,
                             std::span<const uint_multiprecision_t> dividend,
                             std::span<const uint_multiprecision_t> divisor,
                             scratch_allocator_base&                scratch,
                             burnikel_ziegler_params                plan);

// Convenience overload: sizes and owns the workspace, then forwards to the
// scratch-based driver above.
// `threshold_override` is a benchmark/test-only escape hatch; unlike the
// multiplication kernels' cutoff_override it propagates through the
// recursion. Production callers omit it.
template <class Allocator>
    requires(!std::is_base_of_v<scratch_allocator_base, Allocator>)
void divide_burnikel_ziegler(const std::span<uint_multiprecision_t>       quotient,
                             const std::span<uint_multiprecision_t>       remainder,
                             const std::span<const uint_multiprecision_t> dividend,
                             const std::span<const uint_multiprecision_t> divisor,
                             Allocator&                                   alloc,
                             const std::size_t                            threshold_override = 0) {
    const burnikel_ziegler_params plan = burnikel_ziegler_plan(dividend, divisor, threshold_override);
    scratch_allocator<Allocator>  scratch(burnikel_ziegler_storage_size(plan.block_limbs, plan.blocks, plan.threshold),
                                          alloc);
    divide_burnikel_ziegler(
        quotient, remainder, dividend, divisor, static_cast<scratch_allocator_base&>(scratch), plan);
}

// ---------------------------------------------------------------------------
// Approximate quotient driver: identical plan, normalization, and window
// march to divide_burnikel_ziegler, but the lowest window -- the only one
// whose remainder nobody needs -- runs divide_dc_divappr, and no remainder
// is materialized. The blocks above it stay exact (each remainder feeds the
// next window), so the whole quotient's slack is the last block's:
//   q <= q' <= q + divappr_quotient_slack(plan.block_limbs, plan.threshold).
// The normalization scaling multiplies both operands by 2^sigma and leaves
// the quotient invariant. Scratch: burnikel_ziegler_storage_size on the
// same plan (the approximate leaf path allocates strictly less than the
// exact one).
// ---------------------------------------------------------------------------
void divide_quotient_appr(std::span<uint_multiprecision_t>       quotient,
                          std::span<const uint_multiprecision_t> dividend,
                          std::span<const uint_multiprecision_t> divisor,
                          scratch_allocator_base&                scratch,
                          burnikel_ziegler_params                plan);

// ---------------------------------------------------------------------------
// Exact quotient-only division (the GMP dcpi1_div_q idea): one low zero pad
// limb gives the approximate driver a fraction digit below the true
// quotient. Writing Q = floor(dividend * B / divisor) = q * B + frac, the
// driver returns Q' in [Q, Q + slack]; its bottom limb f proves the rest:
//   f >= slack  =>  frac + e did not wrap past B, so the integer part is q;
//   f <  slack  =>  the integer part is q or q + 1 (e <= slack < B means
//                   the wrap, if any, carried exactly one), and a single
//                   multiply-and-compare settles it.
// For random operands the ambiguous branch fires with probability about
// slack / B; exact multiples land in it roughly half the time by
// construction, costing one extra multiplication there.
// Same contract as divide_unsigned minus the remainder; owns its workspace.
//
// Measured (division_kernel_bench div_q_over_div_qr, balanced 2n/n at
// n = 512..32768, release, 2026-06-11): 0.77-0.83x of the full divmod on
// the M4 Max and 0.72-0.81x on the i9-11900K (both NTT configs) -- the
// telescoped recursion model's predicted 18-24%.
// ---------------------------------------------------------------------------
// Scratch upper bound for divide_quotient: the padded numerator, the
// extended quotient, the verify product, and the approximate driver's
// workspace on the padded plan (one extra limb and exactly limb_bits more
// significant bits than the dividend).
[[nodiscard]] constexpr std::size_t divide_quotient_storage_size(const std::span<const uint_multiprecision_t> dividend,
                                                                 const std::span<const uint_multiprecision_t> divisor,
                                                                 const std::size_t threshold_override = 0) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t             m    = dividend.size();
    const std::size_t             qn1  = m - divisor.size() + 1;
    const burnikel_ziegler_params plan = burnikel_ziegler_plan_bits(
        m * limb_bits + static_cast<std::size_t>(std::bit_width(dividend.back())), divisor, threshold_override);
    return (m + 1) + (qn1 + 2) + (m + 2) +
           burnikel_ziegler_storage_size(plan.block_limbs, plan.blocks, plan.threshold) + 8;
}

// Compiled in src/divide.cpp; `scratch` must provide
// divide_quotient_storage_size(...) limbs and carry the heap hooks.
void divide_quotient(std::span<uint_multiprecision_t>       quotient,
                     std::span<const uint_multiprecision_t> dividend,
                     std::span<const uint_multiprecision_t> divisor,
                     scratch_allocator_base&                scratch,
                     std::size_t                            threshold_override);

// Convenience overload: sizes and owns the workspace.
template <class Allocator>
    requires(!std::is_base_of_v<scratch_allocator_base, Allocator>)
void divide_quotient(const std::span<uint_multiprecision_t>       quotient,
                     const std::span<const uint_multiprecision_t> dividend,
                     const std::span<const uint_multiprecision_t> divisor,
                     Allocator&                                   alloc,
                     const std::size_t                            threshold_override = 0) {
    scratch_allocator<Allocator> scratch(divide_quotient_storage_size(dividend, divisor, threshold_override), alloc);
    divide_quotient(quotient, dividend, divisor, static_cast<scratch_allocator_base&>(scratch), threshold_override);
}

// ---------------------------------------------------------------------------
// Block-wise Barrett division (the GMP mu_div_qr lineage).
// The divisor's exact scaled reciprocal X = B^n + I, with
// I = floor((B^{2n} - 1) / D) - B^n, is computed once by Newton iteration;
// each n-limb quotient block then costs two multiplications instead of a
// divide-and-conquer division.
//
// With the q_hat * d_hat subtrahend and the reciprocal's residual checks
// going through multiply_mod_bnm1 wraparound products -- which ride the
// cyclic NTT tier above fft_cyclic_cutoff -- Barrett beats
// divide_burnikel_ziegler on deep block marches everywhere, and on
// progressively shallower shapes as the operands grow. The win regions vary
// strongly by architecture and NTT configuration (the x86-64 scalar integer
// NTT in particular cannot carry the near-balanced shapes), so the gates
// are per-arch below. reciprocal_span also serves future radix-conversion
// and invariant-divisor work.
// ---------------------------------------------------------------------------

// Gates for the Barrett tier. divide_dispatch routes to Barrett when any of
// the four shape rules pass; each rule's constants come from
// division_stress_bench plus direct divide_barrett-vs-divide_burnikel_ziegler
// probes (release builds, min-of-reps, M4 Max and i9-11900K, 2026-06-10):
//
//   march    (m/16 >= s, s >= march_cutoff):    x86-64: 0.81-0.95x from
//            512-limb divisors up (phase C tuning). AArch64 (M4 Max,
//            2026-09-30, s = 2..13 x m/s 16..256): Barrett already wins from a
//            9-limb divisor, the one-row division basecase being slow per
//            limb-product.
//   march8   (m/8 >= s, s >= march8_cutoff):    pays once the divisor wrap
//            rides the cyclic NTT (AArch64: 0.93 at 16384/2048) or, on
//            x86-64, once the reciprocal amortizes anyway (0.94-0.97 at
//            32768/4096, 0.81-0.95 at 65536/8192, ~1.00 at 16384/2048).
//   quarter  (m/4 >= s, m >= quarter_cutoff):   0.57-0.67x (M4) and
//            0.62-0.82x (11900K FP/AVX2) from m = 49152; break-even or worse
//            at m = 32768 on both. The x86-64 integer NTT loses these
//            shapes (1.04-1.29) -- disabled there. AArch64 integer moved to
//            24576 on 2026-09-30 (m 8192..49152, m/s 4..7.5: div 1.0220 ->
//            1.0112); AArch64 SIMD keeps 49152.
//   balanced (m - s >= s, m >= balanced_cutoff): M4 crosses between m =
//            98304 (1.00) and 131072 (0.89, then 0.79/0.64 at 262144/524288).
//            11900K FP/AVX2 is break-even-to-winning at 131072 (1.00 probe,
//            0.94 bench) and agreed-winning from 196608 (0.97/0.88; 0.84 at
//            262144) -- gated at the agreed point. The 11900K integer NTT
//            loses 1.29 at 262144 and first wins at 524288 (0.95).
//
// A GMP mu_div-style PARTIAL reciprocal (inverse of in < s limbs, blocks of
// in quotient limbs) was evaluated and REJECTED on measurement (2026-06-11,
// both machines, all three configs). Two findings: (a) in the m/s in
// [4, 16) band the reciprocal setup is 13-44% of divide_barrett, but GMP's
// own choose_in picks in = s whenever the quotient is near a block multiple
// of s, so there is no setup to recover -- the shapes where Barrett loses
// to divide-and-conquer there (1.07-1.41 at m/s = 4) lose by the setup
// share, and the dispatch gates above already route them to the recursion;
// (b) short quotients (m - s < s, which no gate routes to Barrett) model a
// win only in a narrow corner -- qn in (~0.4s, s) at s >= ~65536, peaking
// ~24% (M4) / ~18% (11900K AVX2) at qn = s/2 and LOSING on the 11900K
// integer NTT, at every s <= 16384, and at every qn <= s/4 (the per-block
// q*D subtrahend costs a full mulmod(s) no matter how small the inverse
// is). Revisit only if those mid-short-quotient shapes at scale become a
// measured workload.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// M4 Max, appleclang-release, 2026-09-30, end to end via division_stress_bench and shape_sweep div/divrem; measured
// on M4 only, also used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t barrett_march_cutoff  = 9;
inline constexpr std::size_t barrett_march8_cutoff = 2048;
    #if defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t barrett_quarter_cutoff = 49152;
    #else
inline constexpr std::size_t barrett_quarter_cutoff = 24576;
    #endif
inline constexpr std::size_t barrett_balanced_cutoff = 131072;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t barrett_march_cutoff  = 512;
inline constexpr std::size_t barrett_march8_cutoff = 4096;
    #if defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t barrett_quarter_cutoff  = 49152;
inline constexpr std::size_t barrett_balanced_cutoff = 196608;
    #else
inline constexpr std::size_t barrett_quarter_cutoff  = std::numeric_limits<std::size_t>::max();
inline constexpr std::size_t barrett_balanced_cutoff = 524288;
    #endif
#else
inline constexpr std::size_t barrett_march_cutoff    = 512;
inline constexpr std::size_t barrett_march8_cutoff   = 2048;
inline constexpr std::size_t barrett_quarter_cutoff  = 49152;
inline constexpr std::size_t barrett_balanced_cutoff = 131072;
#endif

// Barrett may sit below the divide-and-conquer gate (AArch64 marches from 9 limbs); it only needs a real divisor.
static_assert(barrett_march_cutoff >= 2, "divide_barrett needs a divisor of at least 2 limbs");
static_assert(barrett_march8_cutoff >= barrett_march_cutoff,
              "the half-depth march rule must not undercut the full-depth one");

// Below this divisor size the reciprocal comes straight from the schoolbook
// division of B^{2n} - 1; above it one Newton level halves the problem.
// Leaf-threshold probes (2026-06-10, {32..1024} at n = 256..4096): AArch64
// is flat within ~2% from 32 to 128 with 64 never worse than 1% off best;
// x86-64's strong schoolbook prefers 512 (7-8% faster whole-reciprocal at
// n = 512-1024, the common Barrett divisor sizes; pure schoolbook only
// loses from n = 1024 up).
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t reciprocal_span_cutoff = 64; // M4 Max, 2026-09-30: kept (ties)
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t reciprocal_span_cutoff = 512;
#else
inline constexpr std::size_t reciprocal_span_cutoff = 64;
#endif

static_assert(reciprocal_span_cutoff >= 2, "the reciprocal basecase divides by at least 2 limbs");

// Scratch upper bound for reciprocal_span at divisor size n with leaf
// threshold `threshold`: one Newton level holds T(n+h+1) plus the correction
// product (n+h+2) with h = ceil(n/2), then frees both before the
// wrapped-residual stage (two wrap-size buffers plus multiply_mod_bnm1's
// needs, about 5 * wv + 16 with wv slightly above n); deeper levels are
// fully rewound first (LIFO), and the basecase needs ones(2t) + q(t+1) +
// r(2t+1) + the schoolbook's internal (3t+1) with t <= threshold.
constexpr std::size_t reciprocal_span_storage_size(const std::size_t n, const std::size_t threshold) noexcept {
    return 6 * n + 8 * threshold + 32;
}

// ---------------------------------------------------------------------------
// Exact scaled reciprocal of a normalized divisor (the span counterpart of
// reciprocal_word): writes the n limbs of I = floor((B^{2n} - 1) / d) - B^n
// into `inverse`.
// Newton iteration in the shape of MCA algorithm 3.5: the top-half
// reciprocal seeds the candidate
//   X ~= X_h * B^{n-h} + X_h * (B^{n+h} - d * X_h) / B^{2h},
// which lands within a few ulps of the target; the exact residual
// B^{2n} - 1 - d * X then pins it down. The fix loop is the correctness
// argument, so no delicate per-level error analysis is load-bearing.
// Preconditions: inverse.size() == d.size() >= 2, d.back()'s top bit set, no
// aliasing; `scratch` provides reciprocal_span_storage_size(...) limbs.
// `threshold_override` is a test-only escape hatch forcing deep recursion.
// ---------------------------------------------------------------------------
void reciprocal_span(std::span<uint_multiprecision_t>       inverse,
                     std::span<const uint_multiprecision_t> d,
                     scratch_allocator_base&                scratch,
                     std::size_t                            threshold_override = 0);

// Dividend block count of the Barrett driver (no limb padding: block size is
// the divisor size; the +1 keeps the shifted dividend's top block strictly
// below B^n / 2 <= d_hat, the same window precondition the
// Burnikel-Ziegler driver establishes).
[[nodiscard]] constexpr std::size_t barrett_blocks(const std::span<const uint_multiprecision_t> dividend,
                                                   const std::span<const uint_multiprecision_t> divisor) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t n = divisor.size();
    const std::size_t m = dividend.size();
    const std::size_t bit_off =
        static_cast<std::size_t>(limb_bits) - static_cast<std::size_t>(std::bit_width(divisor.back()));
    const std::size_t dividend_bits = (m - 1) * limb_bits + static_cast<std::size_t>(std::bit_width(dividend.back()));
    return std::max<std::size_t>(2, (dividend_bits + bit_off) / (n * limb_bits) + 1);
}

// Scratch upper bound for divide_barrett with divisor size n, `blocks`
// dividend blocks, and reciprocal leaf threshold `invert_threshold`:
//   - normalized divisor:        n limbs
//   - shifted working dividend:  blocks*n limbs
//   - quotient blocks:           (blocks-1)*n limbs
//   - reciprocal:                n limbs
//   - max(reciprocal_span scratch, the block stage: the 2n-limb estimate
//     product, two wrap-size buffers, and multiply_mod_bnm1's own needs)
// The reciprocal scratch is fully rewound before the block buffers are
// allocated (LIFO), so the two never coexist. Deterministic like the
// Burnikel-Ziegler bound; validated in division_scratch_peak.test.cpp.
constexpr std::size_t barrett_storage_size(const std::size_t block_limbs,
                                           const std::size_t blocks,
                                           const std::size_t invert_threshold) noexcept {
    const std::size_t w = multiply_mod_bnm1_next_size(block_limbs + 1, multiply_mod_bnm1_cutoff);
    return (2 * blocks + 1) * block_limbs +
           std::max(reciprocal_span_storage_size(block_limbs, invert_threshold),
                    2 * block_limbs + 2 * w + multiply_mod_bnm1_storage_size(w)) +
           8;
}

// ---------------------------------------------------------------------------
// Block-wise Barrett division driver. Same outer contract as divide_unsigned
// (trimmed inputs, divisor.size() >= 2, quotient/remainder spans fully
// written). Normalizes by the divisor's leading-zero count, computes the
// exact reciprocal X = B^n + I once, then marches n-limb dividend windows
// from the top down exactly like divide_burnikel_ziegler -- but each
// window's quotient block costs two multiplications:
//   q_hat = U_hi + high_half(U_hi * I),   R = U - q_hat * d_hat.
// With the exact reciprocal, q_hat never overestimates and undershoots by at
// most 4: q_hat = floor(U_hi*X/B^n) <= floor(U*X/B^2n) <= floor(U/d); and
// q_hat > U*X/B^2n - X/B^n - 1 > U/d - 4 since X < 2*B^n and
// X > B^2n/d - 1. The correction loop adds d back accordingly.
// `invert_override` forwards to reciprocal_span (test-only escape hatch).
// ---------------------------------------------------------------------------
void divide_barrett(std::span<uint_multiprecision_t>       quotient,
                    std::span<uint_multiprecision_t>       remainder,
                    std::span<const uint_multiprecision_t> dividend,
                    std::span<const uint_multiprecision_t> divisor,
                    scratch_allocator_base&                scratch,
                    std::size_t                            invert_override = 0);

template <class Allocator>
    requires(!std::is_base_of_v<scratch_allocator_base, Allocator>)
void divide_barrett(const std::span<uint_multiprecision_t>       quotient,
                    const std::span<uint_multiprecision_t>       remainder,
                    const std::span<const uint_multiprecision_t> dividend,
                    const std::span<const uint_multiprecision_t> divisor,
                    Allocator&                                   alloc,
                    const std::size_t                            invert_override = 0) {
    const std::size_t            thr = invert_override != 0 ? invert_override : reciprocal_span_cutoff;
    scratch_allocator<Allocator> scratch(barrett_storage_size(divisor.size(), barrett_blocks(dividend, divisor), thr),
                                         alloc);
    divide_barrett(
        quotient, remainder, dividend, divisor, static_cast<scratch_allocator_base&>(scratch), invert_override);
}

// Scratch upper bound for divide_barrett_preinv: barrett_storage_size minus
// the divisor/reciprocal slots and the reciprocal computation arm - the
// caller holds those once per invariant divisor.
[[nodiscard]] constexpr std::size_t barrett_preinv_storage_size(const std::size_t block_limbs,
                                                                const std::size_t blocks) noexcept {
    const std::size_t w = multiply_mod_bnm1_next_size(block_limbs + 1, multiply_mod_bnm1_cutoff);
    return (2 * blocks - 1) * block_limbs + 2 * block_limbs + 2 * w + multiply_mod_bnm1_storage_size(w) + 8;
}

// ---------------------------------------------------------------------------
// Invariant-divisor Barrett division (the radix-conversion entry the
// reciprocal_span notes anticipate): same outer contract as divide_barrett,
// but the divisor arrives pre-normalized (`d_norm`, top bit set; the
// original divisor is d_norm >> shift) with its exact scaled reciprocal
// precomputed by reciprocal_span(inv, d_norm, ...). One reciprocal then
// serves any number of divisions by the same divisor. Fully rewinds its
// scratch (barrett_preinv_storage_size(d_norm.size(), barrett_blocks(...))
// limbs) before returning, so repeated calls share one arena.
// ---------------------------------------------------------------------------
void divide_barrett_preinv(std::span<uint_multiprecision_t>       quotient,
                           std::span<uint_multiprecision_t>       remainder,
                           std::span<const uint_multiprecision_t> dividend,
                           std::span<const uint_multiprecision_t> d_norm,
                           unsigned                               shift,
                           std::span<const uint_multiprecision_t> inv,
                           scratch_allocator_base&                scratch);

// ---------------------------------------------------------------------------
// Top-level division dispatcher (counterpart of multiply_dispatch): the Barrett
// gates above are checked first, then the divide-and-conquer path, which needs
// both a large divisor and a long quotient to pay off; everything else takes
// the schoolbook kernel. Constant evaluation
// always takes the schoolbook kernel for the same reason multiply_dispatch
// avoids its recursive tiers there (consteval step limits).
// Same contract as divide_unsigned. `scratch` must provide at least
// divide_unsigned_storage_size(...) limbs for the schoolbook path; the
// divide-and-conquer path sizes and owns its own workspace through `alloc`.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void divide_dispatch(const std::span<uint_multiprecision_t>       quotient,
                               const std::span<uint_multiprecision_t>       remainder,
                               const std::span<const uint_multiprecision_t> dividend,
                               const std::span<const uint_multiprecision_t> divisor,
                               scratch_allocator_base&                      scratch,
                               Allocator&                                   alloc) {
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        const std::size_t m = dividend.size();
        const std::size_t s = divisor.size();

        const bool barrett_march    = s >= barrett_march_cutoff && m / 16 >= s;
        const bool barrett_march8   = s >= barrett_march8_cutoff && m / 8 >= s;
        const bool barrett_quarter  = m >= barrett_quarter_cutoff && m / 4 >= s;
        const bool barrett_balanced = m >= barrett_balanced_cutoff && m - s >= s;
        if (barrett_march || barrett_march8 || barrett_quarter || barrett_balanced) {
            divide_barrett(quotient, remainder, dividend, divisor, alloc);
            return;
        }
        if (s >= burnikel_ziegler_cutoff && m - s >= burnikel_ziegler_offset) {
            divide_burnikel_ziegler(quotient, remainder, dividend, divisor, alloc);
            return;
        }
    }

    divide_unsigned(quotient, remainder, dividend, divisor, scratch);
}

// ---------------------------------------------------------------------------
// Quotient-only dispatcher: the same tier gates as divide_dispatch, but the
// divide-and-conquer band takes divide_quotient, whose approximate low
// recursion skips the remainder work the caller is about to discard. The
// Barrett and schoolbook tiers produce their remainder essentially for free
// (it lives in their working buffers), so they run unchanged into scratch.
// `scratch` must provide divide_unsigned_storage_size(m, s) plus m + 1
// limbs for the fallback remainder; constant evaluation always takes the
// schoolbook kernel.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void divide_dispatch_q(const std::span<uint_multiprecision_t>       quotient,
                                 const std::span<const uint_multiprecision_t> dividend,
                                 const std::span<const uint_multiprecision_t> divisor,
                                 scratch_allocator_base&                      scratch,
                                 Allocator&                                   alloc) {
    const std::size_t m = dividend.size();
    const std::size_t s = divisor.size();

    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        const bool barrett_march    = s >= barrett_march_cutoff && m / 16 >= s;
        const bool barrett_march8   = s >= barrett_march8_cutoff && m / 8 >= s;
        const bool barrett_quarter  = m >= barrett_quarter_cutoff && m / 4 >= s;
        const bool barrett_balanced = m >= barrett_balanced_cutoff && m - s >= s;
        if (barrett_march || barrett_march8 || barrett_quarter || barrett_balanced) {
            const std::span<uint_multiprecision_t> r = scratch.allocate(m + 1);
            divide_barrett(quotient, r, dividend, divisor, alloc);
            scratch.deallocate(m + 1);
            return;
        }
        if (s >= burnikel_ziegler_cutoff && m - s >= burnikel_ziegler_offset) {
            divide_quotient(quotient, dividend, divisor, alloc);
            return;
        }
    }

    const std::span<uint_multiprecision_t> r = scratch.allocate(m + 1);
    divide_unsigned(quotient, r, dividend, divisor, scratch);
    scratch.deallocate(m + 1);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_DIV_IMPL_HPP
