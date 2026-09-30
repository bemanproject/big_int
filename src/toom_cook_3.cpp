// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

namespace beman::big_int::detail {

void multiply_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          const std::span<const uint_multiprecision_t> b_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Partition at k = ceil(max(an, bn) / 3).
    const std::size_t min_size         = std::min(a.size(), b.size());
    const std::size_t max_size         = std::max(a.size(), b.size());
    const std::size_t k                = (max_size + 2) / 3;
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_3_cutoff : cutoff_override;

    // Fall through to Karatsuba (and on through to schoolbook) when the smaller
    // operand is below the performance cutoff or below the algorithm's 2*k
    // invariant (Toom-Cook 3 needs both a2 and b2 non-empty).
    if (min_size < effective_cutoff || toom_cook_3_refuses_shape(min_size, max_size)) {
        multiply_karatsuba(result, a, b, scratch);
        return;
    }

    // Split each operand into three pieces. Empty pieces represent zero.
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 = a.size() > 2 * k ? a.subspan(2 * k) : std::span<const uint_multiprecision_t>{};

    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 = b.size() > 2 * k ? b.subspan(2 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each
    //   v1, vm1, v2: 2k+2 limbs each, hold three product values that survive through interpolation
    //   v0 = a0*b0 and vinf = a2*b2 live directly in result.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 3 * prod_cap;

    auto block = scratch.allocate(total_scratch);
    auto tmpa  = block.first(tmp_cap);
    auto tmpb  = block.subspan(tmp_cap, tmp_cap);
    auto v1    = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1   = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2    = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);

    // ---- v0 = a0*b0, written into result[0..2k) ----
    // result is pre-zeroed by the caller convention, so result.first(2k) is too.
    multiply_toom_cook_3(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- vinf = a2*b2, written into result[4k..) ----
    if (!a2.empty() && !b2.empty()) {
        multiply_toom_cook_3(result.subspan(4 * k, a2.size() + b2.size()), a2, b2, scratch);
    }

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2; tmpb = b0 + b1 + b2 ----
    std::size_t tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2});
    std::size_t tmpb_size = add_many_into_tmp(tmpb, {b0, b1, b2});

    // v1 = tmpa * tmpb. v1 must be fully zero on entry because the recursive
    // multiply_toom_cook_3 (when not falling back to Karatsuba) expects its
    // result region to be pre-zeroed for its recompose step.
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_3(v1,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -1: tmpa = (a0 + a2) - a1 (signed); tmpb similarly ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a2});
    const auto sub_a =
        subtract_unsigned_spans_signed(tmpa, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, a1);
    tmpa_size         = sub_a.size;
    const bool sign_a = sub_a.negative;

    tmpb_size = add_many_into_tmp(tmpb, {b0, b2});
    const auto sub_b =
        subtract_unsigned_spans_signed(tmpb, std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size}, b1);
    tmpb_size         = sub_b.size;
    const bool sign_b = sub_b.negative;

    // Sign of vm1 = sign of (p(-1)*q(-1)). Magnitude = |p(-1)| * |q(-1)|.
    const bool sign_vm1 = sign_a ^ sign_b;

    // vm1 = |p(-1)| * |q(-1)|
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size == 0 || tmpb_size == 0) {
        // One of the evaluations is zero, so vm1 = 0.
    } else {
        multiply_toom_cook_3(vm1,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 4*a2 + 2*a1 + a0 (Horner: ((a2<<1)+a1)<<1+a0); tmpb similarly ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a2, a1, a0}, 1u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b2, b1, b0}, 1u);

    // v2 = p(2) * q(2)
    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_3(v2,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Bodrato interpolation (in-place; full 2k+2 buffers throughout) ----
    // After this sequence: v0=c0 (in result), vm1=c1, v1=c2, v2=c3, vinf=c4 (in result).
    //
    // Clip vinf_view to its semantic size of 2k limbs: c4 = a2*b2 with a2.size() <= k
    // and b2.size() <= k yields a product of at most 2k limbs. The result span may
    // extend past 4k+2k when called recursively from a parent whose prod_cap buffer
    // is larger than the actual product (slack = caller-pre-zeroed). Subtracting
    // those slack zeros is correct, but the size assertion in subtract_unsigned_spans
    // would fail without clipping.
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 4 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 4 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};

    // Step 1: v2 <- (v2 - vm1) / 3 (sign-aware: add if vm1 was negative).
    sign_vm1 ? add_unsigned_spans_no_carry(v2, v2_view, vm1_view)
             : subtract_unsigned_spans_no_borrow(v2, v2_view, vm1_view);
    {
        const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 2: vm1 <- (v1 - vm1) / 2 (sign-aware). After this, vm1 is non-negative.
    {
        const auto rem = sign_vm1 ? add_unsigned_spans_and_shift_right_one(vm1, v1_view, vm1_view)
                                  : subtract_unsigned_spans_and_shift_right_one(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 3: v1 <- v1 - v0 (sizes: v1 is 2k+2, v0_view is 2k; v1 >= v0 numerically).
    subtract_unsigned_spans(v1, v1_view, v0_view);

    // Step 4: v2 <- (v2 - v1) / 2.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(v2, v2_view, v1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 5: v1 <- v1 - vm1 - vinf.
    subtract_unsigned_spans(v1, v1_view, vm1_view);
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 6: v2 <- v2 - 2*vinf (subtract twice, no doubling helper needed).
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    subtract_unsigned_spans(v2, v2_view, vinf_view);

    // Step 7: vm1 <- vm1 - v2.
    subtract_unsigned_spans(vm1, vm1_view, v2_view);

    // ---- Recompose: result += vm1*B^k + v1*B^(2k) + v2*B^(3k) ----
    // c0 already at result[0..2k); c4 already at result[4k..). Add the three middle
    // coefficients with carry chains spanning to result.size() so carries can
    // propagate into the c4 region.
    recompose(result, k, {vm1_view, v1_view, v2_view});

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    scratch.deallocate(total_scratch);
}

void square_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));

    // Partition at k = ceil(an / 3).
    const std::size_t k = (a.size() + 2) / 3;

    const std::size_t effective_cutoff = cutoff_override == 0 ? square_toom_cook_3_cutoff : cutoff_override;

    // Fall through to the Karatsuba squaring variant below the performance
    // cutoff or below the algorithm's 2*k invariant (a2 must be non-empty).
    if (a.size() < effective_cutoff || a.size() <= 2 * k) {
        square_karatsuba(result, a, scratch);
        return;
    }

    // Split into three pieces; the 2*k gate guarantees a2 is non-empty.
    const auto a0 = a.first(k);
    const auto a1 = a.subspan(k, k);
    const auto a2 = a.subspan(2 * k);

    // Carve scratch: one evaluation buffer (the general kernel needs two):
    //   tmpa: k+2 limbs
    //   v1, vm1, v2: 2k+2 limbs each, hold three squares through interpolation
    //   v0 = a0^2 and vinf = a2^2 live directly in result.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = tmp_cap + 3 * prod_cap;

    auto block = scratch.allocate(total_scratch);
    auto tmpa  = block.first(tmp_cap);
    auto v1    = block.subspan(tmp_cap, prod_cap);
    auto vm1   = block.subspan(tmp_cap + prod_cap, prod_cap);
    auto v2    = block.subspan(tmp_cap + 2 * prod_cap, prod_cap);

    // ---- v0 = a0^2, written into result[0..2k) (caller pre-zeroed). ----
    square_toom_cook_3(result.first(2 * a0.size()), a0, scratch);

    // ---- vinf = a2^2, written into result[4k..). ----
    square_toom_cook_3(result.subspan(4 * k, 2 * a2.size()), a2, scratch);

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2. ----
    std::size_t tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2});

    // v1 = p(1)^2. Pre-zeroed for the recursive recompose step.
    std::ranges::fill(v1, uint_multiprecision_t{0});
    square_toom_cook_3(v1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Evaluate at x = -1: tmpa = |(a0 + a2) - a1|; squaring erases the sign,
    // so the sign-aware interpolation branches of the general kernel collapse
    // to their non-negative arms below. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a2});
    const auto sub_a =
        subtract_unsigned_spans_signed(tmpa, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, a1);
    tmpa_size = sub_a.size;

    // vm1 = p(-1)^2
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_3(vm1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 4*a2 + 2*a1 + a0 (Horner). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a2, a1, a0}, 1u);

    // v2 = p(2)^2
    std::ranges::fill(v2, uint_multiprecision_t{0});
    square_toom_cook_3(v2, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Bodrato interpolation (as the general kernel with sign_vm1 == false) ----
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 4 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 4 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};

    // Step 1: v2 <- (v2 - vm1) / 3.
    subtract_unsigned_spans_no_borrow(v2, v2_view, vm1_view);
    {
        const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 2: vm1 <- (v1 - vm1) / 2.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 3: v1 <- v1 - v0.
    subtract_unsigned_spans(v1, v1_view, v0_view);

    // Step 4: v2 <- (v2 - v1) / 2.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(v2, v2_view, v1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 5: v1 <- v1 - vm1 - vinf.
    subtract_unsigned_spans(v1, v1_view, vm1_view);
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 6: v2 <- v2 - 2*vinf.
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    subtract_unsigned_spans(v2, v2_view, vinf_view);

    // Step 7: vm1 <- vm1 - v2.
    subtract_unsigned_spans(vm1, vm1_view, v2_view);

    // ---- Recompose: result += vm1*B^k + v1*B^(2k) + v2*B^(3k) ----
    recompose(result, k, {vm1_view, v1_view, v2_view});

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    scratch.deallocate(total_scratch);
}

} // namespace beman::big_int::detail
