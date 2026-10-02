// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

void multiply_toom_cook_4(const std::span<uint_multiprecision_t>       result,
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

    // Partition at k = ceil(max(an, bn) / 4).
    const std::size_t min_size         = std::min(a.size(), b.size());
    const std::size_t k                = (std::max(a.size(), b.size()) + 3) / 4;
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_4_cutoff : cutoff_override;

    // Fall through to Toom-Cook 3 (and on through Karatsuba and schoolbook) when the
    // smaller operand is below the performance cutoff or below the algorithm's 3*k
    // invariant (Toom-Cook 4 needs both a3 and b3 non-empty).
    if (min_size < effective_cutoff || min_size <= 3 * k) {
        multiply_toom_cook_3(result, a, b, scratch);
        return;
    }

    // Split each operand into four pieces.
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 =
        a.size() > 2 * k ? a.subspan(2 * k, std::min(k, a.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a3 = a.size() > 3 * k ? a.subspan(3 * k) : std::span<const uint_multiprecision_t>{};

    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 =
        b.size() > 2 * k ? b.subspan(2 * k, std::min(k, b.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b3 = b.size() > 3 * k ? b.subspan(3 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each (evaluation buffers, hold p(x) and q(x) at each point)
    //   v1, vm1, v2, vm2, vh: 2k+2 limbs each (five interpolation buffers)
    //   tmp_double: 2k+2 limbs (used for in-place bit shifts to replace doubling with mul)
    // c0 = a0*b0 lives in result[0..2k); c6 = a3*b3 lives in result[6k..).
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 6 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto tmpb       = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);

    // ---- c0 = a0*b0, written into result[0..2k) (caller pre-zeroed). ----
    multiply_toom_cook_4(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- c6 = a3*b3, written into result[6k..). ----
    // The min > 3*k gate above guarantees both a3 and b3 are non-empty.
    multiply_toom_cook_4(result.subspan(6 * k, a3.size() + b3.size()), a3, b3, scratch);

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2 + a3; tmpb similarly. ----
    std::size_t tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2, a3});
    std::size_t tmpb_size = add_many_into_tmp(tmpb, {b0, b1, b2, b3});

    // v1 = tmpa * tmpb
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_4(v1,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -1: tmpa = (a0 + a2) - (a1 + a3) signed; tmpb similarly. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a2});
    // tmpb temporarily used to hold a1 + a3 before subtraction.
    std::size_t aux_size = add_many_into_tmp(tmpb, {a1, a3});
    const auto  sub_a_m1 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{tmpb.data(), aux_size});
    tmpa_size            = sub_a_m1.size;
    const bool sign_a_m1 = sub_a_m1.negative;

    tmpb_size = add_many_into_tmp(tmpb, {b0, b2});
    // tmpa now holds (a0 + a2) - (a1 + a3); reuse tail of vm1 as a scratch slot for (b1 + b3).
    aux_size = add_many_into_tmp(vm1, {b1, b3});
    const auto sub_b_m1 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpb_size            = sub_b_m1.size;
    const bool sign_b_m1 = sub_b_m1.negative;
    const bool sign_vm1  = sign_a_m1 ^ sign_b_m1;

    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_4(vm1,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 2: tmpa = ((a3*2 + a2)*2 + a1)*2 + a0 (Horner); tmpb similarly. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a3, a2, a1, a0}, 1u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b3, b2, b1, b0}, 1u);

    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_4(v2,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -2: tmpa = (a0 + 4*a2) - (2*a1 + 8*a3) signed; tmpb similarly.
    // Build positive = a0 + 4*a2 in tmpa, negative = 2*a1 + 8*a3 in tmpb, then signed-sub.
    tmpa_size = horner_eval_into_tmp(tmpa, {a2, a0}, 2u);

    // tmpb holds 8*a3 + a1; trailing add_into_tmp doubles a1 to yield 8*a3 + 2*a1.
    aux_size = horner_eval_into_tmp(tmpb, {a3, a1}, 3u);
    aux_size = add_into_tmp(tmpb, aux_size, a1);

    const auto sub_a_m2 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{tmpb.data(), aux_size});
    tmpa_size            = sub_a_m2.size;
    const bool sign_a_m2 = sub_a_m2.negative;

    tmpb_size = horner_eval_into_tmp(tmpb, {b2, b0}, 2u);

    // Use vm2 as a scratch slot for 8*b3 + 2*b1.
    aux_size = horner_eval_into_tmp(vm2, {b3, b1}, 3u);
    aux_size = add_into_tmp(vm2, aux_size, b1);

    const auto sub_b_m2 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpb_size            = sub_b_m2.size;
    const bool sign_b_m2 = sub_b_m2.negative;
    const bool sign_vm2  = sign_a_m2 ^ sign_b_m2;

    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_4(vm2,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled by 8): tmpa = ((a0*2 + a1)*2 + a2)*2 + a3 (Horner);
    // this equals 8*a(1/2), so the resulting product is 64*c(1/2). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3}, 1u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b0, b1, b2, b3}, 1u);

    std::ranges::fill(vh, uint_multiprecision_t{0});
    multiply_toom_cook_4(vh,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Bodrato-style interpolation (in-place; full 2k+2 buffers throughout) ----
    // After this sequence the c-coefficients live as:
    //   c0 in result[0..2k), c1 in vh, c2 in v1, c3 in vm1, c4 in v2, c5 in vm2, c6 in result[6k..).
    //
    // v0_view and vinf_view bound the c0 and c6 regions in result. Clip vinf_view because
    // the result span may be larger than 8k when the caller's prod_cap exceeds the actual
    // product (same trick as Toom-3 at line 642).
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 6 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 6 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view  = std::span<const uint_multiprecision_t>{vm2};
    const auto vh_view   = std::span<const uint_multiprecision_t>{vh};
    const auto td_view   = std::span<const uint_multiprecision_t>{tmp_double};

    // Phase 1: Symmetrize {v1, vm1} into {E1, D1} and {v2, vm2} into {E2, D2}.
    //   E1 = (v1 + vm1)/2 = c0 + c2 + c4 + c6
    //   D1 = (v1 - vm1)/2 = c1 + c3 + c5
    //   E2 = (v2 + vm2)/2 = c0 + 4c2 + 16c4 + 64c6
    //   D2 = (v2 - vm2)/4 = c1 + 4c3 + 16c5

    // Step 1: v1 <- (v1 + vm1) / 2 algebraic (sign-aware on sign_vm1) = E1.
    {
        const auto rem = sign_vm1 ? subtract_unsigned_spans_and_shift_right_one(v1, v1_view, vm1_view)
                                  : add_unsigned_spans_and_shift_right_one(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 1: v1 = E1 = c0 + c2 + c4 + c6.

    // Step 2: vm1 <- v1 - vm1 algebraic = (v1_orig - vm1_orig)/2 = D1.
    sign_vm1 ? add_unsigned_spans_no_carry(vm1, v1_view, vm1_view)
             : subtract_unsigned_spans_no_borrow(vm1, v1_view, vm1_view);
    // After Step 2: vm1 = D1 = c1 + c3 + c5. sign_vm1 is no longer needed.

    // Step 3: v2 <- (v2 + vm2) / 2 algebraic = E2.
    {
        const auto rem = sign_vm2 ? subtract_unsigned_spans_and_shift_right_one(v2, v2_view, vm2_view)
                                  : add_unsigned_spans_and_shift_right_one(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 3: v2 = E2 = c0 + 4c2 + 16c4 + 64c6.

    // Step 4: vm2 <- (v2 - vm2) / 2 algebraic = (v2_orig - vm2_orig)/4 = c1 + 4c3 + 16c5.
    {
        const auto rem = sign_vm2 ? add_unsigned_spans_and_shift_right_one(vm2, v2_view, vm2_view)
                                  : subtract_unsigned_spans_and_shift_right_one(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 4: vm2 = D2 = c1 + 4c3 + 16c5. sign_vm2 is no longer needed.

    // Phase 2: Solve even system for c2 (into v1) and c4 (into v2).

    // Step 5: v2 -= v1.  v2 = (c0+4c2+16c4+64c6) - (c0+c2+c4+c6) = 3*(c2 + 5c4 + 21c6).
    subtract_unsigned_spans(v2, v2_view, v1_view);
    // Step 6: v2 /= 3.  v2 = c2 + 5c4 + 21c6.
    {
        const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 7: v1 -= c0.  v1 = c2 + c4 + c6.
    subtract_unsigned_spans(v1, v1_view, v0_view);
    // Step 8: v1 -= c6.  v1 = c2 + c4.
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 9: v2 -= v1.  v2 = 4c4 + 21c6.
    subtract_unsigned_spans(v2, v2_view, v1_view);

    // Step 10-11: subtract 21*c6 from v2 using tmp_double for the doublings, then
    // halve twice in one fused pass.
    //   v2 -= 1*c6, then v2 -= 4*c6, then v2 = (v2 - 16*c6) / 4.  Net: v2 = c4.
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vinf_view, tmp_double.begin());
    std::size_t td_size = vinf_view.size();
    td_size             = shift_left_n(tmp_double, td_size, 2u);
    subtract_unsigned_spans(v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    td_size = shift_left_n(tmp_double, td_size, 2u);
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(
            v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size}, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 12: v1 -= v2.  v1 = c2.
    subtract_unsigned_spans(v1, v1_view, v2_view);

    // Phase 3: Reduce vh to T_odd = (vh - 64c0 - 16c2 - 4c4 - c6) / 2 = 16c1 + 4c3 + c5.

    // Step 13: vh -= 64*c0.
    subtract_shifted_unsigned(vh, vh_view, v0_view.first(trimmed_size_span(v0_view)), 6u);

    // Step 14: vh -= 16*c2 (c2 lives in v1 now).
    subtract_shifted_unsigned(vh, vh_view, v1_view.first(trimmed_size_span(v1_view)), 4u);

    // Step 15: vh -= 4*c4 (c4 lives in v2 now).
    subtract_shifted_unsigned(vh, vh_view, v2_view.first(trimmed_size_span(v2_view)), 2u);

    // Step 16-17: vh = (vh - c6) / 2.  vh = 16c1 + 4c3 + c5 = T_odd.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(vh, vh_view, vinf_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 4: Solve odd system using vm1 = D1, vm2 = D2, vh = T_odd.
    //   vm2 -= D1:  3c3 + 15c5
    //   vh  -= D1: 15c1 +  3c3
    //   then divide both by 3.

    // Step 18: vm2 -= vm1.  vm2 = 3*(c3 + 5c5).
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    // Step 19: vh -= vm1.  vh = 3*(5c1 + c3).
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    // Step 20: vm2 /= 3.  vm2 = alpha = c3 + 5c5.
    {
        const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // Step 21: vh /= 3.  vh = beta = 5c1 + c3.
    {
        const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Now vm1 = D1, vm2 = alpha, vh = beta. Recover c3 = (5*D1 - alpha - beta) / 3 into vm1.
    // Step 22: tmp_double = D1; vm1 *= 4; vm1 += tmp_double  (-> 5*D1).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vm1_view, tmp_double.begin());
    const auto sz_4D1 = shift_left_n(vm1, vm1.size(), 2u); // 4*D1 in one pass
    BEMAN_BIG_INT_DEBUG_ASSERT(sz_4D1 == vm1.size());
    {
        const bool carry = add_unsigned_spans(vm1, vm1_view, td_view); // 5*D1
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    }
    // Step 23: vm1 -= alpha; vm1 -= beta.  vm1 = 3*c3.
    subtract_unsigned_spans(vm1, vm1_view, vm2_view);
    subtract_unsigned_spans(vm1, vm1_view, vh_view);
    // Step 24: vm1 /= 3.  vm1 = c3.
    {
        const auto rem = divide_unsigned_short(vm1, vm1_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 25: vh -= c3 -> 5*c1; vh /= 5.  vh = c1.
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    {
        const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 26: vm2 -= c3 -> 5*c5; vm2 /= 5.  vm2 = c5.
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    {
        const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 5: Recompose.
    //   result += vh * B^k        (c1)
    //   result += v1 * B^(2k)     (c2)
    //   result += vm1 * B^(3k)    (c3)
    //   result += v2 * B^(4k)     (c4)
    //   result += vm2 * B^(5k)    (c5)
    // c0 and c6 are already in result.
    // Carries propagate up to result.size().
    recompose(result, k, {vh_view, v1_view, vm1_view, v2_view, vm2_view});

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
}

void square_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));

    // Partition at k = ceil(an / 4).
    const std::size_t k                = (a.size() + 3) / 4;
    const std::size_t effective_cutoff = cutoff_override == 0 ? square_toom_cook_4_cutoff : cutoff_override;

    // Fall through to the Toom-3 squaring variant below the performance cutoff
    // or below the algorithm's 3*k invariant (a3 must be non-empty).
    if (a.size() < effective_cutoff || a.size() <= 3 * k) {
        square_toom_cook_3(result, a, scratch);
        return;
    }

    // Split into four pieces; the 3*k gate guarantees a3 is non-empty.
    const auto a0 = a.first(k);
    const auto a1 = a.subspan(k, k);
    const auto a2 = a.subspan(2 * k, k);
    const auto a3 = a.subspan(3 * k);

    // Carve scratch: one evaluation buffer tmpa plus one aux buffer for the
    // negative halves of the signed points (the general kernel needs tmpa and
    // tmpb); five squares and the tmp_double shift scratch.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 6 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto aux        = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);

    // ---- c0 = a0^2, written into result[0..2k) (caller pre-zeroed). ----
    square_toom_cook_4(result.first(2 * a0.size()), a0, scratch);

    // ---- c6 = a3^2, written into result[6k..). ----
    square_toom_cook_4(result.subspan(6 * k, 2 * a3.size()), a3, scratch);

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2 + a3. ----
    std::size_t tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2, a3});
    std::size_t aux_size  = 0;

    // v1 = p(1)^2
    std::ranges::fill(v1, uint_multiprecision_t{0});
    square_toom_cook_4(v1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Evaluate at x = -1: tmpa = |(a0 + a2) - (a1 + a3)|; squaring erases
    // the sign, so the interpolation below uses the non-negative arms of the
    // general kernel's sign-aware steps. ----
    tmpa_size         = add_many_into_tmp(tmpa, {a0, a2});
    aux_size          = add_many_into_tmp(aux, {a1, a3});
    const auto sub_m1 = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{aux.data(), aux_size});
    tmpa_size         = sub_m1.size;

    // vm1 = p(-1)^2
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_4(vm1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // ---- Evaluate at x = 2: tmpa = ((a3*2 + a2)*2 + a1)*2 + a0 (Horner). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a3, a2, a1, a0}, 1u);

    // v2 = p(2)^2
    std::ranges::fill(v2, uint_multiprecision_t{0});
    square_toom_cook_4(v2, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Evaluate at x = -2: tmpa = |(a0 + 4*a2) - (2*a1 + 8*a3)|. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a2, a0}, 2u);

    // aux holds 8*a3 + a1; trailing add_into_tmp doubles a1 to yield 8*a3 + 2*a1.
    aux_size = horner_eval_into_tmp(aux, {a3, a1}, 3u);
    aux_size = add_into_tmp(aux, aux_size, a1);

    const auto sub_m2 = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{aux.data(), aux_size});
    tmpa_size         = sub_m2.size;

    // vm2 = p(-2)^2
    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_4(vm2, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled by 8): tmpa = ((a0*2 + a1)*2 + a2)*2 + a3;
    // this equals 8*a(1/2), so the resulting square is 64*c(1/2). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3}, 1u);

    // vh = (8*p(1/2))^2
    std::ranges::fill(vh, uint_multiprecision_t{0});
    square_toom_cook_4(vh, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Interpolation: the general kernel's sequence with sign_vm1 and
    // sign_vm2 pinned to false (squares are non-negative). ----
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 6 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 6 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view  = std::span<const uint_multiprecision_t>{vm2};
    const auto vh_view   = std::span<const uint_multiprecision_t>{vh};
    const auto td_view   = std::span<const uint_multiprecision_t>{tmp_double};

    // Step 1: v1 <- (v1 + vm1) / 2 = E1 = c0 + c2 + c4 + c6.
    {
        const auto rem = add_unsigned_spans_and_shift_right_one(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 2: vm1 <- v1 - vm1 = D1 = c1 + c3 + c5.
    subtract_unsigned_spans_no_borrow(vm1, v1_view, vm1_view);

    // Step 3: v2 <- (v2 + vm2) / 2 = E2 = c0 + 4c2 + 16c4 + 64c6.
    {
        const auto rem = add_unsigned_spans_and_shift_right_one(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 4: vm2 <- (v2 - vm2) / 2 = D2 = c1 + 4c3 + 16c5.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 2: Solve even system for c2 (into v1) and c4 (into v2).

    // Step 5: v2 -= v1.  v2 = 3*(c2 + 5c4 + 21c6).
    subtract_unsigned_spans(v2, v2_view, v1_view);
    // Step 6: v2 /= 3.  v2 = c2 + 5c4 + 21c6.
    {
        const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 7: v1 -= c0.  v1 = c2 + c4 + c6.
    subtract_unsigned_spans(v1, v1_view, v0_view);
    // Step 8: v1 -= c6.  v1 = c2 + c4.
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 9: v2 -= v1.  v2 = 4c4 + 21c6.
    subtract_unsigned_spans(v2, v2_view, v1_view);

    // Step 10-11: subtract 21*c6 from v2 using tmp_double for the doublings,
    // then halve twice in one fused pass.  Net: v2 = c4.
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vinf_view, tmp_double.begin());
    std::size_t td_size = vinf_view.size();
    td_size             = shift_left_n(tmp_double, td_size, 2u);
    subtract_unsigned_spans(v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    td_size = shift_left_n(tmp_double, td_size, 2u);
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(
            v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size}, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 12: v1 -= v2.  v1 = c2.
    subtract_unsigned_spans(v1, v1_view, v2_view);

    // Phase 3: Reduce vh to T_odd = (vh - 64c0 - 16c2 - 4c4 - c6) / 2 = 16c1 + 4c3 + c5.

    // Step 13: vh -= 64*c0.
    subtract_shifted_unsigned(vh, vh_view, v0_view.first(trimmed_size_span(v0_view)), 6u);

    // Step 14: vh -= 16*c2 (c2 lives in v1 now).
    subtract_shifted_unsigned(vh, vh_view, v1_view.first(trimmed_size_span(v1_view)), 4u);

    // Step 15: vh -= 4*c4 (c4 lives in v2 now).
    subtract_shifted_unsigned(vh, vh_view, v2_view.first(trimmed_size_span(v2_view)), 2u);

    // Step 16-17: vh = (vh - c6) / 2.  vh = 16c1 + 4c3 + c5 = T_odd.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(vh, vh_view, vinf_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 4: Solve odd system using vm1 = D1, vm2 = D2, vh = T_odd.

    // Step 18: vm2 -= vm1.  vm2 = 3*(c3 + 5c5).
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    // Step 19: vh -= vm1.  vh = 3*(5c1 + c3).
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    // Step 20: vm2 /= 3.  vm2 = alpha = c3 + 5c5.
    {
        const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // Step 21: vh /= 3.  vh = beta = 5c1 + c3.
    {
        const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 22: tmp_double = D1; vm1 *= 4; vm1 += tmp_double  (-> 5*D1).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vm1_view, tmp_double.begin());
    const auto sz_4d1 = shift_left_n(vm1, vm1.size(), 2u); // 4*D1 in one pass
    BEMAN_BIG_INT_DEBUG_ASSERT(sz_4d1 == vm1.size());
    {
        const bool carry = add_unsigned_spans(vm1, vm1_view, td_view); // 5*D1
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    }
    // Step 23: vm1 -= alpha; vm1 -= beta.  vm1 = 3*c3.
    subtract_unsigned_spans(vm1, vm1_view, vm2_view);
    subtract_unsigned_spans(vm1, vm1_view, vh_view);
    // Step 24: vm1 /= 3.  vm1 = c3.
    {
        const auto rem = divide_unsigned_short(vm1, vm1_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 25: vh -= c3 -> 5*c1; vh /= 5.  vh = c1.
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    {
        const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 26: vm2 -= c3 -> 5*c5; vm2 /= 5.  vm2 = c5.
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    {
        const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 5: Recompose (c0 and c6 are already in result).
    recompose(result, k, {vh_view, v1_view, vm1_view, v2_view, vm2_view});

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
