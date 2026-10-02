// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/div_impl.hpp>

#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

// The division tiers above the constexpr schoolbook kernels, compiled once:
// the Burnikel-Ziegler recursion, the divappr quotient-only chain, the
// exact Newton reciprocal, and the block Barrett driver. All work in the
// caller-sized type-erased scratch; internal products and FFT-tier
// workspaces come from its heap hooks, so a single compiled definition
// serves every allocator. Contracts live at the declarations in
// div_impl.hpp.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// ---------------------------------------------------------------------------
// Leaf of the Burnikel-Ziegler recursion: divide the 2n-limb window `a` by
// the n-limb divisor `b` (b.back() != 0) with the schoolbook kernel.
// Precondition: value(a) < beta^n * value(b), so the quotient fits n limbs.
// Postconditions: a.first(n) holds the remainder, a.subspan(n) is zero, and
// all n limbs of `q` are written.
// ---------------------------------------------------------------------------
inline void divide_dc_basecase(const std::span<uint_multiprecision_t>       a,
                               const std::span<const uint_multiprecision_t> b,
                               const std::span<uint_multiprecision_t>       q,
                               scratch_allocator_base&                      scratch) noexcept {
    const std::size_t n = b.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == 2 * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(q.size() == n);
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(b.back() != 0);

    const std::size_t a_size = trimmed_size_span(a);
    const auto        a_view = std::span<const uint_multiprecision_t>{a.data(), a_size};

    // value(a) < value(b): quotient 0, remainder already in place (a's limbs
    // above its trimmed size are zero by definition).
    if (compare_unsigned_spans(a_view, b) == std::strong_ordering::less) {
        std::ranges::fill(q, uint_multiprecision_t{0});
        return;
    }

    // Single-limb divisor, reachable only under small threshold overrides.
    if (n == 1) {
        const std::span<uint_multiprecision_t> q_tmp = scratch.allocate(a_size);
        const uint_multiprecision_t            rem   = divide_unsigned_short(q_tmp, a_view, b[0]);
        BEMAN_BIG_INT_DEBUG_ASSERT(a_size < 2 || q_tmp[1] == 0);
        q[0] = q_tmp[0];
        a[0] = rem;
        a[1] = 0;
        scratch.deallocate(a_size);
        return;
    }

    // value(a) >= value(b) and b.back() != 0 force a_size >= n, satisfying
    // the divide_unsigned preconditions.
    BEMAN_BIG_INT_DEBUG_ASSERT(a_size >= n);
    const std::size_t                      q_size = a_size - n + 1;
    const std::span<uint_multiprecision_t> q_tmp  = scratch.allocate(q_size);
    const std::span<uint_multiprecision_t> r_tmp  = scratch.allocate(a_size + 1);

    divide_unsigned(q_tmp, r_tmp, a_view, b, scratch);

    // The 2n/n precondition keeps quotient and remainder below beta^n; copy
    // them back into the window layout.
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= n + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= n || q_tmp[n] == 0);
    const std::size_t q_copy = std::min(q_size, n);
    std::ranges::copy(q_tmp.first(q_copy), q.begin());
    std::ranges::fill(q.subspan(q_copy), uint_multiprecision_t{0});

    BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(std::span<const uint_multiprecision_t>{r_tmp.data() + n, a_size + 1 - n}));
    std::ranges::copy(r_tmp.first(n), a.begin());
    std::ranges::fill(a.subspan(n), uint_multiprecision_t{0});

    scratch.deallocate(a_size + 1);
    scratch.deallocate(q_size);
}

void divide_dc_3n2n(std::span<uint_multiprecision_t>       v,
                    std::span<const uint_multiprecision_t> b,
                    std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                scratch,
                    std::size_t                            threshold);

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
void divide_dc_2n1n(const std::span<uint_multiprecision_t>       a,
                    const std::span<const uint_multiprecision_t> b,
                    const std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                      scratch,
                    const std::size_t                            threshold) {
    const std::size_t n = b.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == 2 * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(q.size() == n);
    // value(a) < beta^n * value(b) is exactly "high half of a < b".
    BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(a.subspan(n), b) == std::strong_ordering::less);

    if ((n % 2) != 0 || n < threshold) {
        divide_dc_basecase(a, b, q, scratch);
        return;
    }

    const std::size_t h = n / 2;

    // High quotient half from the top three quarter-blocks; the remainder
    // lands in a[h..3h) and a[3h..4h) is zeroed.
    divide_dc_3n2n(a.subspan(h, 3 * h), b, q.subspan(h, h), scratch, threshold);

    // Low quotient half: [low quarter-block | remainder] is contiguous.
    divide_dc_3n2n(a.first(3 * h), b, q.first(h), scratch, threshold);
}

// ---------------------------------------------------------------------------
// Burnikel-Ziegler D_3n/2n: divide the 3h-limb window `v` by the 2h-limb
// normalized divisor `b` (top bit set): estimate the quotient from the high
// 2h limbs of `v` against b's high half, fix up with one h x h product and
// at most two add-back corrections.
// Precondition: value(v) < beta^h * value(b), so the quotient fits h limbs.
// Postconditions: v.first(2h) holds the remainder, v.subspan(2h) is zero,
// and all h limbs of `q` are written.
// ---------------------------------------------------------------------------
void divide_dc_3n2n(const std::span<uint_multiprecision_t>       v,
                    const std::span<const uint_multiprecision_t> b,
                    const std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                      scratch,
                    const std::size_t                            threshold) {
    constexpr uint_multiprecision_t max_limb = ~uint_multiprecision_t{0};

    const std::size_t h = q.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(v.size() == 3 * h);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() == 2 * h);
    BEMAN_BIG_INT_DEBUG_ASSERT((b.back() >> (width_v<uint_multiprecision_t> - 1)) == 1);

    const auto b1 = b.subspan(h, h); // high half of the divisor
    const auto b2 = b.first(h);      // low half of the divisor

    const bool top_below_b1 = compare_unsigned_spans(v.subspan(2 * h, h), b1) == std::strong_ordering::less;
    bool       r1_carry     = false;
    if (top_below_b1) {
        // Recursive estimate q = floor(v_high2h / b1); R1 is left in v[h..2h)
        // and v[2h..3h) is zeroed.
        divide_dc_2n1n(v.subspan(h, 2 * h), b1, q, scratch, threshold);
    } else {
        // The precondition forces v's top h limbs to equal b1 exactly and the
        // true quotient to within 2 of beta^h - 1:
        //   q = beta^h - 1,  R1 = v_mid + b1  (fits h limbs plus a carry bit).
        BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(v.subspan(2 * h, h), b1) == std::strong_ordering::equal);
        std::ranges::fill(q, max_limb);
        r1_carry = add_unsigned_spans(v.subspan(h, h), v.subspan(h, h), b1);
        std::ranges::fill(v.subspan(2 * h), uint_multiprecision_t{0});
    }

    // d := q * b2 (2h limbs). The all-ones branch needs no multiply:
    // (beta^h - 1) * b2 == (b2 << h limbs) - b2.
    const std::span<uint_multiprecision_t> d = scratch.allocate(2 * h);
    std::ranges::fill(d, uint_multiprecision_t{0});
    if (top_below_b1) {
        multiply_runtime_any(d, q, b2, scratch.heap());
    } else {
        std::ranges::copy(b2, d.begin() + static_cast<std::ptrdiff_t>(h));
        subtract_unsigned_spans(d, d, b2);
    }

    // Combine: r_hat = R1 * beta^h + v_low - d over the 2h-limb window; the
    // carry/borrow pair tracks the limb at position 2h. net is in {-1, 0}:
    // d < beta^2h bounds r_hat > -beta^2h, and r_hat < value(b) < beta^2h.
    const bool borrow = subtract_unsigned_spans_borrow_out(v.first(2 * h), v.first(2 * h), d);
    int        net    = static_cast<int>(r1_carry) - static_cast<int>(borrow);
    BEMAN_BIG_INT_DEBUG_ASSERT(net <= 0);

    // While r_hat < 0: q -= 1, r_hat += b. Normalization (2 * value(b) >=
    // beta^2h) bounds the loop at two iterations.
    [[maybe_unused]] int corrections = 0;
    while (net < 0) {
        const bool q_borrow = decrement_span(q);
        BEMAN_BIG_INT_DEBUG_ASSERT(!q_borrow);
        net += static_cast<int>(add_unsigned_spans(v.first(2 * h), v.first(2 * h), b));
        ++corrections;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(corrections <= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(v.first(2 * h), b) == std::strong_ordering::less);

    scratch.deallocate(2 * h);
}

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
void divide_dc_divappr(const std::span<uint_multiprecision_t>       a,
                       const std::span<const uint_multiprecision_t> b,
                       const std::span<uint_multiprecision_t>       q,
                       scratch_allocator_base&                      scratch,
                       const std::size_t                            threshold) {
    constexpr uint_multiprecision_t max_limb = ~uint_multiprecision_t{0};

    const std::size_t n = b.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == 2 * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(q.size() == n);
    BEMAN_BIG_INT_DEBUG_ASSERT((b.back() >> (width_v<uint_multiprecision_t> - 1)) == 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(a.subspan(n), b) == std::strong_ordering::less);

    if ((n % 2) != 0 || n < threshold) {
        // Degenerate leaves (deep threshold overrides only): exact division
        // satisfies the approximate contract trivially.
        if (n < 3) {
            divide_dc_basecase(a, b, q, scratch);
            return;
        }

        const std::size_t a_size = trimmed_size_span(a);
        const auto        a_view = std::span<const uint_multiprecision_t>{a.data(), a_size};

        // Window value below (or one limb of) the divisor: the quotient is
        // 0 or 1 by comparison; no division needed.
        if (a_size <= n) {
            std::ranges::fill(q, uint_multiprecision_t{0});
            if (compare_unsigned_spans(a_view, b) != std::strong_ordering::less) {
                q[0] = 1;
            }
            return;
        }

        // The approximate basecase produces a_size - n + 1 <= n + 1 digits;
        // a top overflow digit means the true quotient is within the slack
        // of beta^n - 1, so the window saturates.
        const std::size_t                      q_len = a_size - n + 1;
        const std::span<uint_multiprecision_t> q_tmp = scratch.allocate(q_len);
        divide_unsigned_approx(q_tmp, a_view, b, scratch);
        BEMAN_BIG_INT_DEBUG_ASSERT(q_len <= n || q_tmp[n] <= 1);
        if (q_len > n && q_tmp[n] != 0) {
            std::ranges::fill(q, max_limb);
        } else {
            const std::size_t q_copy = std::min(q_len, n);
            std::ranges::copy(q_tmp.first(q_copy), q.begin());
            std::ranges::fill(q.subspan(q_copy), uint_multiprecision_t{0});
        }
        scratch.deallocate(q_len);
        return;
    }

    const std::size_t h = n / 2;

    // High quotient half, exactly as divide_dc_2n1n: remainder (< b) lands
    // in a[h..3h) and a[3h..4h) is zeroed.
    divide_dc_3n2n(a.subspan(h, 3 * h), b, q.subspan(h, h), scratch, threshold);

    // Low quotient half approximately: the remainder's top 2h limbs against
    // the divisor's high half. remainder < b only bounds the inner window's
    // high half by b1 inclusively; equality saturates (the true low half is
    // then within the slack of beta^h - 1).
    const auto b1 = b.subspan(h, h);
    if (compare_unsigned_spans(a.subspan(2 * h, h), b1) == std::strong_ordering::less) {
        divide_dc_divappr(a.subspan(h, 2 * h), b1, q.first(h), scratch, threshold);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(a.subspan(2 * h, h), b1) == std::strong_ordering::equal);
        std::ranges::fill(q.first(h), max_limb);
    }
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
void divide_burnikel_ziegler(const std::span<uint_multiprecision_t>       quotient,
                             const std::span<uint_multiprecision_t>       remainder,
                             const std::span<const uint_multiprecision_t> dividend,
                             const std::span<const uint_multiprecision_t> divisor,
                             scratch_allocator_base&                      scratch,
                             const burnikel_ziegler_params                plan) {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.size() >= dividend.size() + 1);

    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t s   = divisor.size();
    const std::size_t m   = dividend.size();
    const std::size_t thr = plan.threshold;
    const std::size_t n   = plan.block_limbs;
    const std::size_t t   = plan.blocks;

    // Normalization shift: whole limbs from the padding plus the bits that
    // bring the divisor's top bit to the top. limb_off == n - s exactly
    // because the trimmed divisor has (s-1)*limb_bits < bitlen <= s*limb_bits.
    const std::size_t limb_off = n - s;
    const unsigned bit_off = static_cast<unsigned>(limb_bits) - static_cast<unsigned>(std::bit_width(divisor.back()));

    // b_hat = divisor << sigma: n limbs, top bit set.
    const std::span<uint_multiprecision_t> b_hat = scratch.allocate(n);
    std::ranges::fill(b_hat, uint_multiprecision_t{0});
    std::ranges::copy(divisor, b_hat.begin() + static_cast<std::ptrdiff_t>(limb_off));
    if (bit_off != 0) {
        const std::size_t b_hat_size = shift_left_n(b_hat, n, bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(b_hat_size == n);
    }
    BEMAN_BIG_INT_DEBUG_ASSERT((b_hat.back() >> (limb_bits - 1)) == 1);

    // w = dividend << sigma, zero-extended to t blocks of n limbs.
    const std::span<uint_multiprecision_t> w = scratch.allocate(t * n);
    std::ranges::fill(w, uint_multiprecision_t{0});
    std::ranges::copy(dividend, w.begin() + static_cast<std::ptrdiff_t>(limb_off));
    if (bit_off != 0) {
        const std::size_t w_size = shift_left_n(w, limb_off + m, bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(w_size <= t * n);
    }

    // March the windows from the top down; window i leaves its remainder in
    // w[i*n..(i+1)*n), the high half of window i-1.
    const std::span<uint_multiprecision_t> q_work = scratch.allocate((t - 1) * n);
    for (std::size_t i = t - 1; i-- > 0;) {
        divide_dc_2n1n(w.subspan(i * n, 2 * n), b_hat, q_work.subspan(i * n, n), scratch, thr);
    }

    // Quotient blocks concatenate exactly (each is below beta^n); trim and
    // copy out.
    const std::size_t q_size = trimmed_size_span(q_work);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= quotient.size());
    std::ranges::copy(q_work.first(q_size), quotient.begin());
    std::ranges::fill(quotient.subspan(q_size), uint_multiprecision_t{0});

    // Remainder: undo the normalization shift on the final window's low half.
    if (bit_off != 0) {
        const uint_multiprecision_t dropped = shift_right_n(w.first(n), bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(limb_off == 0 ||
                               is_span_zero(std::span<const uint_multiprecision_t>{w.data(), limb_off}));
    std::ranges::copy(w.subspan(limb_off, n - limb_off), remainder.begin());
    std::ranges::fill(remainder.subspan(n - limb_off), uint_multiprecision_t{0});
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
void divide_quotient_appr(const std::span<uint_multiprecision_t>       quotient,
                          const std::span<const uint_multiprecision_t> dividend,
                          const std::span<const uint_multiprecision_t> divisor,
                          scratch_allocator_base&                      scratch,
                          const burnikel_ziegler_params                plan) {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);

    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t s   = divisor.size();
    const std::size_t m   = dividend.size();
    const std::size_t thr = plan.threshold;
    const std::size_t n   = plan.block_limbs;
    const std::size_t t   = plan.blocks;

    const std::size_t limb_off = n - s;
    const unsigned bit_off = static_cast<unsigned>(limb_bits) - static_cast<unsigned>(std::bit_width(divisor.back()));

    // b_hat = divisor << sigma: n limbs, top bit set.
    const std::span<uint_multiprecision_t> b_hat = scratch.allocate(n);
    std::ranges::fill(b_hat, uint_multiprecision_t{0});
    std::ranges::copy(divisor, b_hat.begin() + static_cast<std::ptrdiff_t>(limb_off));
    if (bit_off != 0) {
        const std::size_t b_hat_size = shift_left_n(b_hat, n, bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(b_hat_size == n);
    }
    BEMAN_BIG_INT_DEBUG_ASSERT((b_hat.back() >> (limb_bits - 1)) == 1);

    // w = dividend << sigma, zero-extended to t blocks of n limbs.
    const std::span<uint_multiprecision_t> w = scratch.allocate(t * n);
    std::ranges::fill(w, uint_multiprecision_t{0});
    std::ranges::copy(dividend, w.begin() + static_cast<std::ptrdiff_t>(limb_off));
    if (bit_off != 0) {
        const std::size_t w_size = shift_left_n(w, limb_off + m, bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(w_size <= t * n);
    }

    // Exact windows from the top down, then the approximate lowest window.
    const std::span<uint_multiprecision_t> q_work = scratch.allocate((t - 1) * n);
    for (std::size_t i = t - 1; i-- > 1;) {
        divide_dc_2n1n(w.subspan(i * n, 2 * n), b_hat, q_work.subspan(i * n, n), scratch, thr);
    }
    divide_dc_divappr(w.first(2 * n), b_hat, q_work.first(n), scratch, thr);

    const std::size_t q_size = trimmed_size_span(q_work);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= quotient.size());
    std::ranges::copy(q_work.first(q_size), quotient.begin());
    std::ranges::fill(quotient.subspan(q_size), uint_multiprecision_t{0});

    scratch.deallocate((t - 1) * n);
    scratch.deallocate(t * n);
    scratch.deallocate(n);
}

// ---------------------------------------------------------------------------
// Exact quotient-only division: contract and the fraction-limb argument in
// div_impl.hpp at the declaration. Works entirely in `scratch` (sized by
// divide_quotient_storage_size), including the padded numerator.
// ---------------------------------------------------------------------------
void divide_quotient(const std::span<uint_multiprecision_t>       quotient,
                     const std::span<const uint_multiprecision_t> dividend,
                     const std::span<const uint_multiprecision_t> divisor,
                     scratch_allocator_base&                      scratch,
                     const std::size_t                            threshold_override) {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);

    const std::size_t m   = dividend.size();
    const std::size_t s   = divisor.size();
    const std::size_t qn1 = m - s + 1;

    // Padded numerator dividend * B.
    const std::span<uint_multiprecision_t> padded = scratch.allocate(m + 1);
    padded[0]                                     = 0;
    std::ranges::copy(dividend, padded.begin() + 1);
    const auto padded_view = std::span<const uint_multiprecision_t>{padded.data(), m + 1};

    const burnikel_ziegler_params plan  = burnikel_ziegler_plan(padded_view, divisor, threshold_override);
    const std::size_t             slack = divappr_quotient_slack(plan.block_limbs, plan.threshold);

    const std::span<uint_multiprecision_t> q_ext = scratch.allocate(qn1 + 2);
    divide_quotient_appr(q_ext, padded_view, divisor, scratch, plan);

    // The extended quotient can spill one digit past q * B only when the
    // true quotient is exactly beta^qn1 - 1 and the fraction sits within
    // the slack of B: the answer is all-ones, no verification needed.
    if (q_ext[qn1 + 1] != 0) {
        std::ranges::fill(quotient.first(qn1), ~uint_multiprecision_t{0});
        std::ranges::fill(quotient.subspan(qn1), uint_multiprecision_t{0});
        scratch.deallocate(qn1 + 2);
        scratch.deallocate(m + 1);
        return;
    }

    const auto integer_part = std::span<const uint_multiprecision_t>{q_ext.data() + 1, qn1};
    if (q_ext[0] >= slack) {
        std::ranges::copy(integer_part, quotient.begin());
        std::ranges::fill(quotient.subspan(qn1), uint_multiprecision_t{0});
        scratch.deallocate(qn1 + 2);
        scratch.deallocate(m + 1);
        return;
    }

    // Ambiguous fraction: the integer part is q or q + 1. One product
    // decides; the subtraction never borrows because q >= 1 here
    // (dividend >= divisor).
    const std::size_t                      i_size = trimmed_size_span(integer_part);
    const std::span<uint_multiprecision_t> prod   = scratch.allocate(m + 2);
    std::ranges::fill(prod, uint_multiprecision_t{0});
    if (i_size != 0) {
        multiply_runtime_any(prod, integer_part.first(i_size), divisor, scratch.heap());
    }
    std::ranges::copy(integer_part, quotient.begin());
    std::ranges::fill(quotient.subspan(qn1), uint_multiprecision_t{0});
    if (compare_unsigned_spans(std::span<const uint_multiprecision_t>{prod.data(), m + 2}, dividend) ==
        std::strong_ordering::greater) {
        const bool borrow = decrement_span(quotient.first(qn1));
        BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    }
    scratch.deallocate(m + 2);
    scratch.deallocate(qn1 + 2);
    scratch.deallocate(m + 1);
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
void reciprocal_span(const std::span<uint_multiprecision_t>       inverse,
                     const std::span<const uint_multiprecision_t> d,
                     scratch_allocator_base&                      scratch,
                     const std::size_t                            threshold_override) {
    constexpr uint_multiprecision_t max_limb = ~uint_multiprecision_t{0};

    const std::size_t n = d.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(inverse.size() == n);
    BEMAN_BIG_INT_DEBUG_ASSERT(n >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT((d.back() >> (width_v<uint_multiprecision_t> - 1)) == 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(inverse.data() != d.data());

    const std::size_t thr = threshold_override != 0 ? threshold_override : reciprocal_span_cutoff;
    BEMAN_BIG_INT_DEBUG_ASSERT(thr >= 2);

    if (n <= thr) {
        // I is the low half of floor((B^{2n} - 1) / d); the quotient's top
        // limb is exactly 1 because normalization keeps X in [B^n, 2B^n).
        const std::span<uint_multiprecision_t> ones = scratch.allocate(2 * n);
        std::ranges::fill(ones, max_limb);
        const std::span<uint_multiprecision_t> q = scratch.allocate(n + 1);
        const std::span<uint_multiprecision_t> r = scratch.allocate(2 * n + 1);
        divide_unsigned(q, r, ones, d, scratch);
        BEMAN_BIG_INT_DEBUG_ASSERT(q[n] == 1);
        std::ranges::copy(q.first(n), inverse.begin());
        scratch.deallocate(2 * n + 1);
        scratch.deallocate(n + 1);
        scratch.deallocate(2 * n);
        return;
    }

    // Recursive top-half reciprocal, built directly into the high limbs of
    // the output: the candidate is X_h * B^{n-h} plus a low correction.
    const std::size_t h = (n + 1) / 2;
    const std::size_t l = n - h;
    reciprocal_span(inverse.subspan(l), d.subspan(l), scratch, threshold_override);
    std::ranges::fill(inverse.first(l), uint_multiprecision_t{0});

    // T = d * X_h = d * I_h + d * B^h over n + h + 1 limbs.
    const std::size_t                      t_len = n + h + 1;
    const std::span<uint_multiprecision_t> t     = scratch.allocate(t_len);
    std::ranges::fill(t, uint_multiprecision_t{0});
    multiply_runtime_any(t.first(n + h), d, inverse.subspan(l), scratch.heap());
    add_shifted(t, h, d);

    // E = B^{n+h} - T, |E| < 2 * B^n; T's limb n+h is 0 or 1, deciding the
    // sign. Reuse t's buffer for |E|.
    BEMAN_BIG_INT_DEBUG_ASSERT(t[n + h] <= 1);
    const bool e_negative = t[n + h] != 0;
    if (e_negative) {
        // |E| = T - B^{n+h}: dropping the top limb is the whole subtraction.
        t[n + h] = 0;
    } else {
        // |E| = B^{n+h} - T = (all-ones - T) + 1; T > 0 because d != 0.
        for (std::size_t i = 0; i < n + h; ++i) {
            t[i] = max_limb - t[i];
        }
        const bool carry = increment_span(t.first(n + h));
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    }
    const std::size_t e_size = trimmed_size_span(t.first(n + h));
    BEMAN_BIG_INT_DEBUG_ASSERT(e_size <= n + 1);
    const auto e_view = std::span<const uint_multiprecision_t>{t.data(), e_size};

    // Correction = floor(X_h * |E| / B^{2h}) = (I_h * E + E * B^h) >> 2h limbs.
    const std::size_t                      c_len = e_size + h + 1;
    const std::span<uint_multiprecision_t> c     = scratch.allocate(c_len);
    std::ranges::fill(c, uint_multiprecision_t{0});
    multiply_runtime_any(c.first(e_size + h), e_view, inverse.subspan(l), scratch.heap());
    add_shifted(c, h, e_view);
    const auto corr = c_len > 2 * h ? std::span<const uint_multiprecision_t>{c.data() + 2 * h, c_len - 2 * h}
                                    : std::span<const uint_multiprecision_t>{};

    if (e_negative) {
        // A borrow past the top would push X below B^n; clamp to the bottom
        // of the range and let the residual fix recover.
        if (subtract_unsigned_spans_borrow_out(inverse, inverse, corr)) {
            std::ranges::fill(inverse, uint_multiprecision_t{0});
        }
    } else {
        if (add_unsigned_spans(inverse, inverse, corr)) {
            std::ranges::fill(inverse, max_limb);
        }
    }
    scratch.deallocate(c_len);
    scratch.deallocate(t_len);

    // Exact residual fix, carried out on residues mod B^wv - 1: the true
    // residual R' = B^{2n} - 1 - d * X lies in (-9 * B^n, B^n), far below
    // the modulus, so its residue identifies its value and sign exactly,
    // and the wrapped product costs well under a full multiplication.
    const std::size_t                      wv  = multiply_mod_bnm1_next_size(n + 1, multiply_mod_bnm1_cutoff);
    const std::span<uint_multiprecision_t> v   = scratch.allocate(wv);
    const std::span<uint_multiprecision_t> res = scratch.allocate(wv);

    // v = d * X mod (B^wv - 1) = d * I + d * B^n; the shifted part is a
    // carry-free rotation of d's limbs within the wrap.
    multiply_mod_bnm1(v, d, std::span<const uint_multiprecision_t>{inverse.data(), n}, scratch);
    std::ranges::fill(res, uint_multiprecision_t{0});
    for (std::size_t i = 0; i < n; ++i) {
        res[(n + i) % wv] = d[i];
    }
    add_mod_bnm1(v, std::span<const uint_multiprecision_t>{res.data(), wv});

    // target = (B^{2n} - 1) mod (B^wv - 1) = B^{(2n) mod wv} - 1: a run of
    // all-ones limbs.
    const std::size_t r2 = (2 * n) % wv;
    std::ranges::fill(res.first(r2), max_limb);
    std::ranges::fill(res.subspan(r2), uint_multiprecision_t{0});

    // res = (target - v) mod (B^wv - 1), congruent to R'.
    if (subtract_unsigned_spans_borrow_out(res, res, std::span<const uint_multiprecision_t>{v.data(), wv})) {
        // Wrapped past zero: -B^wv == -1 (mod B^wv - 1).
        [[maybe_unused]] const bool all_zero = decrement_span(res);
    }

    // Sign disambiguation by range: an undershooting candidate leaves a
    // non-negative residual of at most ~11 * B^n (limb n at most 11), while
    // an overshooting one maps to at least B^wv - 1 - 10 * B^n, whose limbs
    // above n are saturated (or, when wv == n + 1, whose limb n is within
    // 11 of the limb maximum). 64 splits the gap with room to spare.
    const auto is_negative = [&]() { return !is_span_zero(res.subspan(n + 1)) || res[n] > 64; };

    [[maybe_unused]] int fixes = 0;
    while (is_negative()) {
        const bool borrow = decrement_span(inverse);
        BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
        add_mod_bnm1(res, d);
        ++fixes;
        BEMAN_BIG_INT_DEBUG_ASSERT(fixes <= 16);
    }
    while (compare_unsigned_spans(res, d) != std::strong_ordering::less) {
        const bool carry = increment_span(inverse);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
        subtract_unsigned_spans(res, res, d);
        ++fixes;
        BEMAN_BIG_INT_DEBUG_ASSERT(fixes <= 16);
    }
    scratch.deallocate(wv);
    scratch.deallocate(wv);
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
// ---------------------------------------------------------------------------
// Shared block march of the Barrett drivers: the dividend arrives pre-shifted
// into t = w.size() / d.size() blocks (`w`), the divisor pre-normalized
// (`d`, top bit set) with its exact scaled reciprocal (`inv`), and `shift`
// is the normalization undone on the final remainder window. Allocates only
// the march products (rewound before returning); `w` holds the remainder
// chain and `q_work` the concatenated quotient blocks.
// ---------------------------------------------------------------------------
namespace {

void barrett_march(const std::span<uint_multiprecision_t>       quotient,
                   const std::span<uint_multiprecision_t>       remainder,
                   const std::span<uint_multiprecision_t>       w,
                   const std::span<uint_multiprecision_t>       q_work,
                   const std::span<const uint_multiprecision_t> d,
                   const std::span<const uint_multiprecision_t> inv,
                   const unsigned                               shift,
                   scratch_allocator_base&                      scratch) {
    const std::size_t n = d.size();
    const std::size_t t = w.size() / n;
    BEMAN_BIG_INT_DEBUG_ASSERT(w.size() == t * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_work.size() == (t - 1) * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(inv.size() == n);

    // The estimate product stays full (its high half is needed exactly); the
    // q_hat * d_hat subtrahend only matters mod B^wrap - 1 because the true
    // R = U - q_hat * d_hat is known to be below 5 * B^n < B^wrap - 1.
    const std::size_t                      wrap     = multiply_mod_bnm1_next_size(n + 1, multiply_mod_bnm1_cutoff);
    const std::span<uint_multiprecision_t> p        = scratch.allocate(2 * n);
    const std::span<uint_multiprecision_t> tw       = scratch.allocate(wrap);
    const std::span<uint_multiprecision_t> uf       = scratch.allocate(wrap);
    constexpr uint_multiprecision_t        max_limb = ~uint_multiprecision_t{0};

    // March the windows from the top down; window i leaves its remainder in
    // w[i*n..(i+1)*n), the high half of window i-1.
    for (std::size_t i = t - 1; i-- > 0;) {
        const std::span<uint_multiprecision_t>       window = w.subspan(i * n, 2 * n);
        const std::span<const uint_multiprecision_t> u_hi{window.data() + n, n};
        const std::span<uint_multiprecision_t>       q_block = q_work.subspan(i * n, n);

        // q_hat = U_hi + high_half(U_hi * X) where X = B^n + I.
        std::ranges::fill(p, uint_multiprecision_t{0});
        multiply_runtime_any(p, u_hi, inv, scratch.heap());
        const bool q_carry =
            add_unsigned_spans(q_block, u_hi, std::span<const uint_multiprecision_t>{p.data() + n, n});
        BEMAN_BIG_INT_DEBUG_ASSERT(!q_carry);

        // R = (U - q_hat * d_hat) mod (B^wrap - 1), recovered exactly from
        // its residue: tw = the wrapped subtrahend, uf = the folded window.
        multiply_mod_bnm1(tw, std::span<const uint_multiprecision_t>{q_block.data(), n}, d, scratch);
        fold_mod_bnm1(uf, std::span<const uint_multiprecision_t>{window.data(), 2 * n});
        if (subtract_unsigned_spans_borrow_out(uf, uf, std::span<const uint_multiprecision_t>{tw.data(), wrap})) {
            // Wrapped past zero: -B^wrap == -1 (mod B^wrap - 1).
            [[maybe_unused]] const bool all_zero = decrement_span(uf);
        }
        if (uf.front() == max_limb &&
            std::ranges::all_of(uf, [](const uint_multiprecision_t x) { return x == max_limb; })) {
            // The all-ones pattern equals the modulus and means R = 0.
            std::ranges::fill(uf, uint_multiprecision_t{0});
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(wrap >= n + 1 && wrap <= 2 * n);
        BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(uf.subspan(n + 1)));
        BEMAN_BIG_INT_DEBUG_ASSERT(uf[n] <= 4);

        // Write R back into the window (it fits n + 1 limbs with the top at
        // most 4 before corrections).
        std::ranges::copy(uf, window.begin());
        std::ranges::fill(window.subspan(wrap), uint_multiprecision_t{0});

        // q - q_hat <= 4: add the divisor back accordingly.
        [[maybe_unused]] int corrections = 0;
        while (compare_unsigned_spans(window, d) != std::strong_ordering::less) {
            const bool carry = increment_span(q_block);
            BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
            subtract_unsigned_spans(window, window, d);
            ++corrections;
            BEMAN_BIG_INT_DEBUG_ASSERT(corrections <= 4);
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(window.subspan(n)));
    }

    scratch.deallocate(wrap);
    scratch.deallocate(wrap);
    scratch.deallocate(2 * n);

    // Quotient blocks concatenate exactly; trim and copy out.
    const std::size_t q_size = trimmed_size_span(q_work);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= quotient.size());
    std::ranges::copy(q_work.first(q_size), quotient.begin());
    std::ranges::fill(quotient.subspan(q_size), uint_multiprecision_t{0});

    // Remainder: undo the normalization shift on the final window's low half.
    if (shift != 0) {
        const uint_multiprecision_t dropped = shift_right_n(w.first(n), shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
    }
    std::ranges::copy(w.first(n), remainder.begin());
    std::ranges::fill(remainder.subspan(n), uint_multiprecision_t{0});
}

} // namespace

void divide_barrett(const std::span<uint_multiprecision_t>       quotient,
                    const std::span<uint_multiprecision_t>       remainder,
                    const std::span<const uint_multiprecision_t> dividend,
                    const std::span<const uint_multiprecision_t> divisor,
                    scratch_allocator_base&                      scratch,
                    const std::size_t                            invert_override) {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.size() >= dividend.size() + 1);

    const std::size_t n = divisor.size();
    const std::size_t m = dividend.size();
    const std::size_t t = barrett_blocks(dividend, divisor);

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

    // w = dividend << shift, zero-extended to t blocks of n limbs.
    const std::span<uint_multiprecision_t> w = scratch.allocate(t * n);
    std::ranges::fill(w, uint_multiprecision_t{0});
    std::ranges::copy(dividend, w.begin());
    if (shift != 0) {
        const std::size_t w_size = shift_left_n(w, m, shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(w_size <= t * n);
    }

    const std::span<uint_multiprecision_t> q_work = scratch.allocate((t - 1) * n);

    // The exact reciprocal of the normalized divisor; its computation scratch
    // rewinds before the standing block products below are allocated.
    const std::span<uint_multiprecision_t> inv = scratch.allocate(n);
    reciprocal_span(inv, d, scratch, invert_override);

    barrett_march(quotient, remainder, w, q_work, d, std::span<const uint_multiprecision_t>{inv}, shift, scratch);
}

void divide_barrett_preinv(const std::span<uint_multiprecision_t>       quotient,
                           const std::span<uint_multiprecision_t>       remainder,
                           const std::span<const uint_multiprecision_t> dividend,
                           const std::span<const uint_multiprecision_t> d_norm,
                           const unsigned                               shift,
                           const std::span<const uint_multiprecision_t> inv,
                           scratch_allocator_base&                      scratch) {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(d_norm.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(d_norm.back() >> (limb_bits - 1) == 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(inv.size() == d_norm.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(shift < limb_bits);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= d_norm.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - d_norm.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.size() >= dividend.size() + 1);

    const std::size_t n = d_norm.size();
    const std::size_t m = dividend.size();

    // The block count of barrett_blocks on the unshifted divisor: its
    // leading-zero count is exactly `shift`.
    const std::size_t dividend_bits = (m - 1) * limb_bits + static_cast<std::size_t>(std::bit_width(dividend.back()));
    const std::size_t t             = std::max<std::size_t>(2, (dividend_bits + shift) / (n * limb_bits) + 1);

    const std::span<uint_multiprecision_t> w = scratch.allocate(t * n);
    std::ranges::fill(w, uint_multiprecision_t{0});
    std::ranges::copy(dividend, w.begin());
    if (shift != 0) {
        const std::size_t w_size = shift_left_n(w, m, shift);
        BEMAN_BIG_INT_DEBUG_ASSERT(w_size <= t * n);
    }

    const std::span<uint_multiprecision_t> q_work = scratch.allocate((t - 1) * n);

    barrett_march(quotient, remainder, w, q_work, d_norm, inv, shift, scratch);

    // Fully rewind: unlike divide_barrett (one shot per owned workspace),
    // this entry runs many times against one long-lived scratch.
    scratch.deallocate((t - 1) * n);
    scratch.deallocate(t * n);
}

// Convenience overload: sizes and owns the workspace, then forwards to the
// scratch-based driver above.

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
