// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Solves one palindromic 5x5 subsystem of the Toom-6.5 interpolation (shared
// by the general and squaring kernels; works on a, b, c, d, e where
// a=row-of-ones buffer, b/c=row-2/row-4 buffers, d=row-h buffer, e=row-q buffer).
// After return, a holds the middle coeff, b holds |d_inner| with returned sign_d_inner,
// c holds |d_outer| with returned sign_d_outer, d holds s_inner, e holds s_outer.
// `tmp_double` is the caller's 2k+2-limb shift scratch.
static subsystem_signs solve_subsystem(const std::span<uint_multiprecision_t> a_buf,
                                       const std::span<uint_multiprecision_t> b_buf,
                                       const std::span<uint_multiprecision_t> c_buf,
                                       const std::span<uint_multiprecision_t> d_buf,
                                       const std::span<uint_multiprecision_t> e_buf,
                                       const std::span<uint_multiprecision_t> tmp_double) noexcept {
    const auto a_v = std::span<const uint_multiprecision_t>{a_buf};
    const auto b_v = std::span<const uint_multiprecision_t>{b_buf};
    const auto c_v = std::span<const uint_multiprecision_t>{c_buf};
    const auto d_v = std::span<const uint_multiprecision_t>{d_buf};
    const auto e_v = std::span<const uint_multiprecision_t>{e_buf};

    // Compute 4*d in tmp_double. d here is 5x palindromic row, value fits in 2k+~10 bits.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(d_buf, tmp_double.begin());
    std::size_t s = trimmed_size_span(d_v);
    s             = shift_left_n(tmp_double, s, 2u);

    // d_buf <- b + 4*d = S_2.
    const bool s2_carry = add_unsigned_spans(d_buf, b_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
    BEMAN_BIG_INT_DEBUG_ASSERT(!s2_carry);
    // b_buf <- |b - 4*d| with sign.
    const auto sub_d2 =
        subtract_unsigned_spans_signed(b_buf, b_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
    const bool sign_D2 = sub_d2.negative;

    // Compute 16*e in tmp_double.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(e_buf, tmp_double.begin());
    s = trimmed_size_span(e_v);
    s = shift_left_n(tmp_double, s, 4u);

    // e_buf <- c + 16*e = S_4.
    const bool s4_carry = add_unsigned_spans(e_buf, c_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
    BEMAN_BIG_INT_DEBUG_ASSERT(!s4_carry);
    // c_buf <- |c - 16*e| with sign.
    const auto sub_d4 =
        subtract_unsigned_spans_signed(c_buf, c_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
    const bool sign_D4 = sub_d4.negative;

    // Eliminate the middle coefficient m from S_2 and S_4 using a (the all-ones row).
    const auto a_trim_first = a_v.first(trimmed_size_span(a_v));

    // d_buf -= 128*a.
    subtract_shifted_unsigned(d_buf, d_v, a_trim_first, 7u);

    // e_buf -= 8192*a.
    subtract_shifted_unsigned(e_buf, e_v, a_trim_first, 13u);

    // After elimination:
    //   d_buf = 900*s_o + 144*s_i
    //   e_buf = 1040400*s_o + 57600*s_i

    // Eliminate s_inner from e_buf: e_buf -= 400 * d_buf.
    // (Trim to satisfy multiply_single_limb's `result.size() >= a.size() + 1` precondition.)
    {
        const auto d_trim = d_v.first(trimmed_size_span(d_v));
        const auto m400   = multiply_single_limb(tmp_double, d_trim, uint_multiprecision_t{400});
        subtract_unsigned_spans(e_buf, e_v, std::span<const uint_multiprecision_t>{tmp_double.data(), m400});
    }
    // e_buf = 680400 * s_o.
    {
        const auto rem = divide_unsigned_short(e_buf, e_v, uint_multiprecision_t{680400});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // e_buf = s_o now.

    // Recover s_inner: d_buf = 900*s_o + 144*s_i -> d_buf -= 900*s_o; d_buf /= 144.
    {
        const auto e_trim = e_v.first(trimmed_size_span(e_v));
        const auto m900   = multiply_single_limb(tmp_double, e_trim, uint_multiprecision_t{900});
        subtract_unsigned_spans(d_buf, d_v, std::span<const uint_multiprecision_t>{tmp_double.data(), m900});
    }
    {
        const auto rem = divide_unsigned_short(d_buf, d_v, uint_multiprecision_t{144});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // d_buf = s_i.

    // Recover middle: a_buf -= s_o; a_buf -= s_i.
    subtract_unsigned_spans(a_buf, a_v, e_v);
    subtract_unsigned_spans(a_buf, a_v, d_v);
    // a_buf = m.

    // Now solve the 2x2 for d_outer/d_inner from D_2 (b_buf, sign_D2) and D_4 (c_buf, sign_D4):
    //   -D_2 = 1020 d_o + 240 d_i
    //   -D_4 = 1048560 d_o + 65280 d_i
    // Goal: c_buf <- |d_outer| with sign_d_outer; b_buf <- |d_inner| with sign_d_inner.
    //
    // Eliminate d_inner: compute X = -D_4 - 272*(-D_2) = -D_4 + 272*D_2 = 771120 * d_outer.
    // In signed form: X_signed = 272 * b_signed - c_signed, where b_signed = D_2_signed = (sign_D2 ? -|b| : +|b|)
    // and c_signed = D_4_signed similarly.
    //
    // Implementation: compute 272*|b| into tmp_double. Then combine with |c| based on sign relations.

    const auto b_diff_trim = b_v.first(trimmed_size_span(b_v));
    const auto m272        = multiply_single_limb(tmp_double, b_diff_trim, uint_multiprecision_t{272});
    const auto td272       = std::span<const uint_multiprecision_t>{tmp_double.data(), m272};
    // X_signed = (sign_D2 ? -272|b| : +272|b|) - (sign_D4 ? -|c| : +|c|)
    //          = sign_D2 ? (-272|b| - (sign_D4 ? -|c| : +|c|)) : (272|b| - (sign_D4 ? -|c| : +|c|))
    // Cases:
    //   sign_D2=false, sign_D4=false: X = 272|b| - |c|.  Use subtract_signed.
    //   sign_D2=false, sign_D4=true:  X = 272|b| + |c|.  Add (positive).
    //   sign_D2=true,  sign_D4=false: X = -272|b| - |c| = -(272|b|+|c|).  Add, sign true.
    //   sign_D2=true,  sign_D4=true:  X = -272|b| + |c| = |c| - 272|b|.  Use subtract_signed (swap).
    bool sign_X = false;
    if (sign_D2 == sign_D4) {
        // Same sign: result magnitude = |272|b| - |c||; sign = sign_D2 XOR (sub_negative).
        const auto sx = subtract_unsigned_spans_signed(c_buf, td272, c_v);
        sign_X        = sign_D2 ^ sx.negative;
    } else {
        // Different signs: result magnitude = 272|b| + |c|; sign = sign_D2.
        const bool carry = add_unsigned_spans(c_buf, td272, c_v);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
        sign_X = sign_D2;
    }
    // c_buf now holds |X|; X_signed = 771120 * d_outer; therefore
    // sign of d_outer = sign_X (771120 > 0); magnitude |d_outer| = |X|/771120.
    {
        const auto rem =
            divide_unsigned_short(c_buf, std::span<const uint_multiprecision_t>{c_buf}, uint_multiprecision_t{771120});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    const bool sign_d_outer = sign_X;
    // c_buf = |d_outer|.

    // Recover d_inner from -D_2 = 1020*d_outer + 240*d_inner, i.e.,
    //   240 * d_inner_signed = (-D_2) - 1020 * d_outer
    //                        = -(sign_D2 ? -|b| : +|b|) - 1020 * (sign_d_outer ? -|c| : +|c|)
    // Compute 1020 * |c| into tmp_double, sign = sign_d_outer.
    const auto c_buf_view = std::span<const uint_multiprecision_t>{c_buf};
    const auto c_trim     = c_buf_view.first(trimmed_size_span(c_buf_view));
    const auto m1020      = multiply_single_limb(tmp_double, c_trim, uint_multiprecision_t{1020});
    const auto td1020     = std::span<const uint_multiprecision_t>{tmp_double.data(), m1020};
    // -D_2 has sign !sign_D2. So we compute (-D_2) + (-(1020*d_outer)) signed-result.
    // = (-D_2) - 1020*d_outer signed.
    // sign of -D_2 = !sign_D2 (we ADD its magnitude to nothing yet; first put it).
    // Let's compute Y = (-D_2) - 1020 * d_outer in signed form.
    //   first_term: |b| with sign !sign_D2 (since first_term = -D_2 = -(sign_D2 ? -|b| : +|b|))
    //   second_term: 1020 * (sign_d_outer ? -|c| : +|c|) = sign_d_outer ? -1020|c| : +1020|c|
    //   Y = first_term - second_term
    const bool sign_first  = !sign_D2;
    const bool sign_second = sign_d_outer;
    bool       sign_Y      = false;
    if (sign_first == sign_second) {
        const auto sy = subtract_unsigned_spans_signed(b_buf, b_v, td1020);
        sign_Y        = sign_first ^ sy.negative;
    } else {
        const bool carry = add_unsigned_spans(b_buf, b_v, td1020);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
        sign_Y = sign_first;
    }
    // Y = 240 * d_inner. Therefore |d_inner| = |Y|/240, sign_d_inner = sign_Y.
    {
        const auto rem =
            divide_unsigned_short(b_buf, std::span<const uint_multiprecision_t>{b_buf}, uint_multiprecision_t{240});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    const bool sign_d_inner = sign_Y;

    return {sign_d_outer, sign_d_inner};
}

void multiply_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a_untrimmed,
                            const std::span<const uint_multiprecision_t> b_untrimmed,
                            scratch_allocator_base&                      scratch,
                            const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a_trim = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b_trim = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Orient: a is the smaller (6 pieces), b is the larger (up to 7 pieces).
    // Multiplication is commutative so we can freely reorder the operands here.
    const auto a = a_trim.size() <= b_trim.size() ? a_trim : b_trim;
    const auto b = a_trim.size() <= b_trim.size() ? b_trim : a_trim;

    const std::size_t min_size         = a.size();
    const std::size_t max_size         = b.size();
    const std::size_t k                = (min_size + 5) / 6; // ceil(min/6)
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_6_5_cutoff : cutoff_override;

    // Fallback to Toom-Cook 4 when:
    //   - the smaller operand is below the performance cutoff, OR
    //   - the algorithm's 5*k invariant would leave a5 empty, OR
    //   - the size ratio exceeds 7:6 so b doesn't fit in seven pieces.
    if (min_size < effective_cutoff || min_size <= 5 * k || max_size > 7 * k) {
        multiply_toom_cook_4(result, a_trim, b_trim, scratch);
        return;
    }

    // Split span a into six pieces of size k (a5 may be partial).
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 =
        a.size() > 2 * k ? a.subspan(2 * k, std::min(k, a.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a3 =
        a.size() > 3 * k ? a.subspan(3 * k, std::min(k, a.size() - 3 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a4 =
        a.size() > 4 * k ? a.subspan(4 * k, std::min(k, a.size() - 4 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a5 = a.size() > 5 * k ? a.subspan(5 * k) : std::span<const uint_multiprecision_t>{};

    // Split b into up to seven pieces of size k (b5, b6 may be partial; b6 may be empty).
    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 =
        b.size() > 2 * k ? b.subspan(2 * k, std::min(k, b.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b3 =
        b.size() > 3 * k ? b.subspan(3 * k, std::min(k, b.size() - 3 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b4 =
        b.size() > 4 * k ? b.subspan(4 * k, std::min(k, b.size() - 4 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b5 =
        b.size() > 5 * k ? b.subspan(5 * k, std::min(k, b.size() - 5 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b6 = b.size() > 6 * k ? b.subspan(6 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each (evaluation buffers; +2 limbs for growth from
    //               scaling factors up to 4^6 = 12 bits in the q-side reverse Horner).
    //   v1, vm1, v2, vm2, v4, vm4, vh, vmh, vq, vmq: 2k+2 limbs each (ten product buffers).
    //   tmp_double: 2k+2 limbs (scratch for in-place scaling during interpolation).
    // c0 = a0*b0 lives in result[0..2k); c11 = a5*b6 (when b6 non-empty) lives in result[11k..).
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 11 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto tmpb       = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap + 0 * prod_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + 1 * prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto v4         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto vm4        = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 6 * prod_cap, prod_cap);
    auto vmh        = block.subspan(2 * tmp_cap + 7 * prod_cap, prod_cap);
    auto vq         = block.subspan(2 * tmp_cap + 8 * prod_cap, prod_cap);
    auto vmq        = block.subspan(2 * tmp_cap + 9 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 10 * prod_cap, prod_cap);

    // ---- c0 = a0*b0, written into result[0..2k). Caller pre-zeroed result. ----
    multiply_toom_cook_6_5(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- c11 = a5*b6, written into result[11k..). Skipped when b6 is empty
    // (balanced inputs): result[11k..) is already zero, so c11 = 0 is correct.
    if (!b6.empty()) {
        multiply_toom_cook_6_5(result.subspan(11 * k, a5.size() + b6.size()), a5, b6, scratch);
    }

    std::size_t tmpa_size = 0;
    std::size_t tmpb_size = 0;
    std::size_t aux_size  = 0;

    // ---- Evaluate at x = 1: tmpa = a0+a1+a2+a3+a4+a5; tmpb = b0+b1+...+b6. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5});
    tmpb_size = add_many_into_tmp(tmpb, {b0, b1, b2, b3, b4, b5, b6});

    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(v1,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -1 (signed):
    //   tmpa = (a0+a2+a4) - (a1+a3+a5);  tmpb = (b0+b2+b4+b6) - (b1+b3+b5). ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a2, a4});
    // Use vm1 (currently zero) as scratch to hold (a1+a3+a5).
    aux_size = add_many_into_tmp(vm1, {a1, a3, a5});
    const auto sub_a_m1 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpa_size            = sub_a_m1.size;
    const bool sign_a_m1 = sub_a_m1.negative;

    tmpb_size = add_many_into_tmp(tmpb, {b0, b2, b4, b6});
    // Reuse vm1 as scratch for (b1+b3+b5).
    aux_size = add_many_into_tmp(vm1, {b1, b3, b5});
    const auto sub_b_m1 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpb_size            = sub_b_m1.size;
    const bool sign_b_m1 = sub_b_m1.negative;
    const bool sign_vm1  = sign_a_m1 ^ sign_b_m1;

    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vm1,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 32a5+16a4+8a3+4a2+2a1+a0 (Horner from a5);
    //                         tmpb = 64b6+32b5+...+b0 (Horner from b6). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a5, a4, a3, a2, a1, a0}, 1u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b6, b5, b4, b3, b2, b1, b0}, 1u);

    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(v2,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -2 (signed):
    //   tmpa = (a0+4a2+16a4) - (2a1+8a3+32a5);
    //   tmpb = (b0+4b2+16b4+64b6) - (2b1+8b3+32b5). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a4, a2, a0}, 2u);
    // negative part 2a1+8a3+32a5 into vm2 (currently zero).
    aux_size = horner_eval_into_tmp(vm2, {a5, a3, a1}, 2u);
    aux_size = shift_left_one(vm2, aux_size);
    const auto sub_a_m2 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpa_size            = sub_a_m2.size;
    const bool sign_a_m2 = sub_a_m2.negative;

    // b-side: positive part b0+4b2+16b4+64b6 into tmpb.
    tmpb_size = horner_eval_into_tmp(tmpb, {b6, b4, b2, b0}, 2u);
    // negative part 2b1+8b3+32b5 into vm2 (reusing as scratch).
    aux_size = horner_eval_into_tmp(vm2, {b5, b3, b1}, 2u);
    aux_size = shift_left_one(vm2, aux_size);
    const auto sub_b_m2 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpb_size            = sub_b_m2.size;
    const bool sign_b_m2 = sub_b_m2.negative;
    const bool sign_vm2  = sign_a_m2 ^ sign_b_m2;

    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vm2,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 4: tmpa = 1024a5 + 256a4 + 64a3 + 16a2 + 4a1 + a0
    //                         (Horner with two shifts per step). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a5, a4, a3, a2, a1, a0}, 2u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b6, b5, b4, b3, b2, b1, b0}, 2u);

    std::ranges::fill(v4, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(v4,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -4 (signed):
    //   tmpa = (a0+16a2+256a4) - (4a1+64a3+1024a5);
    //   tmpb = (b0+16b2+256b4+4096b6) - (4b1+64b3+1024b5). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a4, a2, a0}, 4u);
    // negative part 4a1+64a3+1024a5 into vm4.
    aux_size = horner_eval_into_tmp(vm4, {a5, a3, a1}, 4u);
    aux_size = shift_left_n(vm4, aux_size, 2u);
    const auto sub_a_m4 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm4.data(), aux_size});
    tmpa_size            = sub_a_m4.size;
    const bool sign_a_m4 = sub_a_m4.negative;

    tmpb_size = horner_eval_into_tmp(tmpb, {b6, b4, b2, b0}, 4u);
    // negative part 4b1+64b3+1024b5 into vm4 (reusing).
    aux_size = horner_eval_into_tmp(vm4, {b5, b3, b1}, 4u);
    aux_size = shift_left_n(vm4, aux_size, 2u);
    const auto sub_b_m4 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm4.data(), aux_size});
    tmpb_size            = sub_b_m4.size;
    const bool sign_b_m4 = sub_b_m4.negative;
    const bool sign_vm4  = sign_a_m4 ^ sign_b_m4;

    std::ranges::fill(vm4, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vm4,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled by 2^11): reverse Horner from a0/b0.
    //   tmpa = 32a0+16a1+8a2+4a3+2a4+a5 = 32*p(1/2);
    //   tmpb = 64b0+32b1+16b2+8b3+4b4+2b5+b6 = 64*q(1/2);
    //   vh = tmpa*tmpb = 2048*r(1/2). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5}, 1u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b0, b1, b2, b3, b4, b5, b6}, 1u);

    std::ranges::fill(vh, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(vh,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -1/2 (scaled by 2^11), signed.
    //   |32*p(-1/2)| = |(32a0+8a2+2a4) - (16a1+4a3+a5)|;
    //   |64*q(-1/2)| = |(64b0+16b2+4b4+b6) - (32b1+8b3+2b5)|.
    // Note: p has odd degree (5), so p_rev(-2) = -32*p(-1/2). We flip the raw
    // XOR sign at the end so sign_vmh tracks sign(2048*r(-1/2)) directly. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a2, a4}, 2u);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    // tmpa = 32a0+8a2+2a4
    // negative: 16a1+4a3+a5 in vmh.
    aux_size = horner_eval_into_tmp(vmh, {a1, a3, a5}, 2u);
    const auto sub_a_mh =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vmh.data(), aux_size});
    tmpa_size            = sub_a_mh.size;
    const bool sign_a_mh = sub_a_mh.negative;

    tmpb_size = horner_eval_into_tmp(tmpb, {b0, b2, b4, b6}, 2u);
    // negative: 32b1+8b3+2b5 in vmh (reusing).
    aux_size = horner_eval_into_tmp(vmh, {b1, b3, b5}, 2u);
    aux_size = shift_left_one(vmh, aux_size);
    const auto sub_b_mh =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vmh.data(), aux_size});
    tmpb_size            = sub_b_mh.size;
    const bool sign_b_mh = sub_b_mh.negative;
    // tmpa stores |32*p(-1/2)| with sign_a_mh; tmpb stores |64*q(-1/2)| with sign_b_mh.
    // Their product is 2048*r(-1/2) signed, so sign_vmh tracks sign(r(-1/2)) directly.
    const bool sign_vmh = sign_a_mh ^ sign_b_mh;

    std::ranges::fill(vmh, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vmh,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 1/4 (scaled by 4^11): reverse Horner with x4 per step.
    //   tmpa = 1024a0+256a1+64a2+16a3+4a4+a5;
    //   tmpb = 4096b0+1024b1+256b2+64b3+16b4+4b5+b6. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5}, 2u);
    tmpb_size = horner_eval_into_tmp(tmpb, {b0, b1, b2, b3, b4, b5, b6}, 2u);

    std::ranges::fill(vq, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(vq,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -1/4 (scaled by 4^11), signed.
    //   |1024a0+64a2+4a4 - (256a1+16a3+a5)|;
    //   |4096b0+256b2+16b4+b6 - (1024b1+64b3+4b5)|.
    // As with vmh, the (-4)^5 factor in p_rev introduces a sign flip; absorb at the end. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a2, a4}, 4u);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    // tmpa = 1024a0 + 64a2 + 4a4
    aux_size = horner_eval_into_tmp(vmq, {a1, a3, a5}, 4u);
    // vmq = 256a1 + 16a3 + a5
    const auto sub_a_mq =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vmq.data(), aux_size});
    tmpa_size            = sub_a_mq.size;
    const bool sign_a_mq = sub_a_mq.negative;

    tmpb_size = horner_eval_into_tmp(tmpb, {b0, b2, b4, b6}, 4u);
    // tmpb = 4096b0 + 256b2 + 16b4 + b6
    aux_size = horner_eval_into_tmp(vmq, {b1, b3, b5}, 4u);
    aux_size = shift_left_n(vmq, aux_size, 2u);
    // vmq = 1024b1 + 64b3 + 4b5
    const auto sub_b_mq =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vmq.data(), aux_size});
    tmpb_size            = sub_b_mq.size;
    const bool sign_b_mq = sub_b_mq.negative;
    // Same convention as vmh: signed tmpa*tmpb = 4194304*r(-1/4), no flip needed.
    const bool sign_vmq = sign_a_mq ^ sign_b_mq;

    std::ranges::fill(vmq, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vmq,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Interpolation ----
    // After 12 evaluations and the sign-aware folds below, each pair (v_x, vm_x)
    // collapses into (E_x, D_x) holding the even/odd parts of r(x). Five even
    // equations + c0 (known) recover c2..c10; five odd equations + c11 (known)
    // recover c1..c9. The same 5x5 matrix appears in both subsystems so the
    // elimination sequence below applies to v's and vm's in parallel.

    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = result.size() > 11 * k ? std::min(result.size() - 11 * k, 2 * k) : std::size_t{0};
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 11 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view  = std::span<const uint_multiprecision_t>{vm2};
    const auto v4_view   = std::span<const uint_multiprecision_t>{v4};
    const auto vm4_view  = std::span<const uint_multiprecision_t>{vm4};
    const auto vh_view   = std::span<const uint_multiprecision_t>{vh};
    const auto vmh_view  = std::span<const uint_multiprecision_t>{vmh};
    const auto vq_view   = std::span<const uint_multiprecision_t>{vq};
    const auto vmq_view  = std::span<const uint_multiprecision_t>{vmq};

    // -- Phase 1: symmetrize each pair. v_x <- E_x; vm_x <- D_x (with the
    // /(2x) factor absorbed for integer-x points). Sign flags vanish after this. --

    // Pair (v1, vm1):  E_1 = (v1+vm1)/2 = c0+c2+c4+c6+c8+c10;  D_1 = (v1-vm1)/2.
    {
        const auto rem = sign_vm1 ? subtract_unsigned_spans_and_shift_right_one(v1, v1_view, vm1_view)
                                  : add_unsigned_spans_and_shift_right_one(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    sign_vm1 ? add_unsigned_spans_no_carry(vm1, v1_view, vm1_view)
             : subtract_unsigned_spans_no_borrow(vm1, v1_view, vm1_view);

    // Pair (v2, vm2):  E_2 = (v2+vm2)/2;  D_2 = (v2-vm2)/4.
    {
        const auto rem = sign_vm2 ? subtract_unsigned_spans_and_shift_right_one(v2, v2_view, vm2_view)
                                  : add_unsigned_spans_and_shift_right_one(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    {
        const auto rem = sign_vm2 ? add_unsigned_spans_and_shift_right_one(vm2, v2_view, vm2_view)
                                  : subtract_unsigned_spans_and_shift_right_one(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Pair (v4, vm4):  E_4 = (v4+vm4)/2;  D_4 = (v4-vm4)/8.
    {
        const auto rem = sign_vm4 ? subtract_unsigned_spans_and_shift_right_one(v4, v4_view, vm4_view)
                                  : add_unsigned_spans_and_shift_right_one(v4, v4_view, vm4_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    {
        const auto rem = sign_vm4 ? add_unsigned_spans_and_shift_right_n(vm4, v4_view, vm4_view, 2u)
                                  : subtract_unsigned_spans_and_shift_right_n(vm4, v4_view, vm4_view, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Pair (vh, vmh):  E_h = (vh+vmh_signed)/2 = 2048c0+512c2+128c4+32c6+8c8+2c10;
    //                  D_h = (vh-vmh_signed)/2 = 1024c1+256c3+64c5+16c7+4c9+c11.
    {
        const auto rem = sign_vmh ? subtract_unsigned_spans_and_shift_right_one(vh, vh_view, vmh_view)
                                  : add_unsigned_spans_and_shift_right_one(vh, vh_view, vmh_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    sign_vmh ? add_unsigned_spans_no_carry(vmh, vh_view, vmh_view)
             : subtract_unsigned_spans_no_borrow(vmh, vh_view, vmh_view);

    // Pair (vq, vmq):  E_q = (vq+vmq_signed)/2 = 4194304c0+262144c2+16384c4+1024c6+64c8+4c10;
    //                  D_q = (vq-vmq_signed)/2 = 1048576c1+65536c3+4096c5+256c7+16c9+c11.
    {
        const auto rem = sign_vmq ? subtract_unsigned_spans_and_shift_right_one(vq, vq_view, vmq_view)
                                  : add_unsigned_spans_and_shift_right_one(vq, vq_view, vmq_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    sign_vmq ? add_unsigned_spans_no_carry(vmq, vq_view, vmq_view)
             : subtract_unsigned_spans_no_borrow(vmq, vq_view, vmq_view);

    // -- Phase 2: subtract c0 contributions from {v1,v2,v4,vh,vq}; subtract c11
    // contributions from {vm1,vm2,vm4,vmh,vmq}; normalize vh,vq,vmh,vmq so all
    // five rows of each subsystem share the same 5x5 matrix M:
    //
    //   M = [   1      1      1       1         1     ;
    //          4     16     64     256      1024    ;
    //         16    256   4096   65536   1048576    ;
    //        256     64     16       4         1    ;
    //      65536   4096    256      16         1    ]
    //
    // (rows = v1, v2, v4, vh-normalized, vq-normalized; cols = c2 c4 c6 c8 c10
    // or c1 c3 c5 c7 c9 in the odd system). --

    // Even side: subtract c0 and powers-of-c0.
    subtract_unsigned_spans(v1, v1_view, v0_view);
    subtract_unsigned_spans(v2, v2_view, v0_view);
    subtract_unsigned_spans(v4, v4_view, v0_view);

    // vh = (vh - 2048*c0) / 2.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    std::size_t td_size = trimmed_size_span(v0_view);
    td_size             = shift_left_n(tmp_double, td_size, 11u);
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(
            vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // vq = (vq - 4194304*c0) / 4.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    td_size = trimmed_size_span(v0_view);
    td_size = shift_left_n(tmp_double, td_size, 22u);
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(
            vq, vq_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size}, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Odd side: subtract c11 and powers-of-c11. (c11 may be zero when b6 is empty;
    // the subtractions and shifts are then no-ops.)
    subtract_unsigned_spans(vm1, vm1_view, vinf_view);

    // vm2 -= 1024*c11.
    {
        const auto vinf_trim = vinf_view.first(vinf_size != 0 ? trimmed_size_span(vinf_view) : std::size_t{0});
        subtract_shifted_unsigned(vm2, vm2_view, vinf_trim, 10u);
    }

    // vm4 -= 1048576*c11.
    {
        const auto vinf_trim = vinf_view.first(vinf_size != 0 ? trimmed_size_span(vinf_view) : std::size_t{0});
        subtract_shifted_unsigned(vm4, vm4_view, vinf_trim, 20u);
    }

    // vmh = (vmh - c11) / 4.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(vmh, vmh_view, vinf_view, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // vmq = (vmq - c11) / 16.
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(vmq, vmq_view, vinf_view, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // The odd rows D_2' and D_4' have coefficients [1, 4, ...] and [1, 16, ...]
    // respectively, one factor of x^2 less than the corresponding E_2' and E_4'
    // rows ([4, 16, ...], [16, 256, ...]). To reuse the same 5x5 elimination
    // chain for the odd subsystem, we pre-scale vm2 by 4 and vm4 by 16 so the
    // odd matrix matches the even matrix shape. The recovered c1, c3, c5, c7,
    // c9 are unaffected by this scaling (they are inputs to the matrix, not
    // outputs of it; only the right-hand side scales accordingly).
    {
        // vm2 *= 4.
        std::size_t s = trimmed_size_span(vm2_view);
        s             = shift_left_n(vm2, s, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= vm2.size());
    }
    {
        // vm4 *= 16.
        std::size_t s = trimmed_size_span(vm4_view);
        s             = shift_left_n(vm4, s, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= vm4.size());
    }

    // -- Phase 3: solve the two 5x5 systems via the same elimination chain.
    // The two diagonals fold into sum/diff palindromic combinations:
    //   S_2 = row(v_2) + 4*row(v_h_n) = 1028 s_o + 272 s_i + 128 m
    //   S_4 = row(v_4) + 16*row(v_q_n) = 1048592 s_o + 65792 s_i + 8192 m
    //   D_2 = row(v_2) - 4*row(v_h_n) = -1020 d_o - 240 d_i  (signed)
    //   D_4 = row(v_4) - 16*row(v_q_n) = -1048560 d_o - 65280 d_i  (signed)
    // where s_o = c_outer_sum, s_i = c_inner_sum, m = middle coefficient (c6 or c5),
    // and d_o = c_outer_diff, d_i = c_inner_diff. After elimination:
    //   middle:   v1 holds m.
    //   sums:     vq holds s_o (= c2+c10 or c1+c9), vh holds s_i (= c4+c8 or c3+c7).
    //   diffs:    v4 holds |d_o| with sign_d_outer, v2 holds |d_i| with sign_d_inner.
    // Finally recover c2,c10 (or c1,c9) and c4,c8 (or c3,c7) using sign-aware
    // half-sum-and-difference in v2/vh and v4/vq respectively. --

    // Solve even subsystem in {v1, v2, v4, vh, vq}.
    const auto [sign_d_outer_e, sign_d_inner_e] = solve_subsystem(v1, v2, v4, vh, vq, tmp_double);
    // Now:  v1 = c6;  vh = c4+c8;  vq = c2+c10;  v4 = |c2 - c10|, sign_d_outer_e;  v2 = |c4 - c8|, sign_d_inner_e.

    // Solve odd subsystem in {vm1, vm2, vm4, vmh, vmq}. The odd buffers were
    // pre-scaled in Phase 2 (vm2*=4, vm4*=16) so the same elimination chain
    // applies.
    const auto [sign_d_outer_o, sign_d_inner_o] = solve_subsystem(vm1, vm2, vm4, vmh, vmq, tmp_double);
    // Now:  vm1 = c5;  vmh = c3+c7;  vmq = c1+c9;  vm4 = |c1 - c9|, sign_d_outer_o;  vm2 = |c3 - c7|, sign_d_inner_o.

    // -- Recover individual c-values from sum/diff pairs --
    //
    //   c_outer  = (s_outer + d_outer) / 2
    //   c_outer' = (s_outer - d_outer) / 2
    // (with sign-aware diff). The lower-index coefficient (c2, c4 / c1, c3) gets
    // s+d when d_outer is positive (c_lower > c_higher), otherwise s-d.
    //
    // We're going to place:
    //   c2 in v4  (overwriting |d_outer|), c10 in vq (overwriting s_outer)
    //   c4 in v2  (overwriting |d_inner|), c8 in vh (overwriting s_inner)
    //   c1 in vm4 (overwriting |d_outer|), c9 in vmq
    //   c3 in vm2 (overwriting |d_inner|), c7 in vmh
    //
    // For each pair (s, d) -> (lower, higher):
    //   if sign_d:   lower = (s - |d|)/2;  higher = (s + |d|)/2
    //   else:        lower = (s + |d|)/2;  higher = (s - |d|)/2

    // Even outer pair: s_outer in vq, d_outer in v4 (sign_d_outer_e), lower = c2, higher = c10.
    // We need to place c2 somewhere distinct from s_outer/d_outer to avoid aliasing.
    // Use v4 for c2 and vq for c10 (overwrite both, since s_outer and d_outer are no longer needed
    // independently after this step). But recover_pair reads s_view (vq) and d_view (v4), and
    // writes to lower_dst and higher_dst. If lower_dst == v4 and higher_dst == vq, then writes
    // overlap with reads. To avoid aliasing, recover_pair uses tmp_double for the plus computation.
    // The minus computation overwrites the destination, and it reads s_view (separate). So aliasing
    // s_view with one of the destinations is OK as long as the minus computation finishes before
    // any subsequent read of s_view. Since recover_pair does minus first then copies plus, OK.
    recover_pair(v4,
                 vq,
                 std::span<const uint_multiprecision_t>{vq}, // s_outer
                 std::span<const uint_multiprecision_t>{v4}, // |d_outer|
                 sign_d_outer_e,
                 tmp_double);
    // After: v4 = c2, vq = c10.

    // Even inner pair: s_inner in vh, d_inner in v2 (sign_d_inner_e), lower = c4, higher = c8.
    recover_pair(v2,
                 vh,
                 std::span<const uint_multiprecision_t>{vh},
                 std::span<const uint_multiprecision_t>{v2},
                 sign_d_inner_e,
                 tmp_double);
    // After: v2 = c4, vh = c8.

    // Odd outer pair: s_outer in vmq, d_outer in vm4 (sign_d_outer_o), lower = c1, higher = c9.
    recover_pair(vm4,
                 vmq,
                 std::span<const uint_multiprecision_t>{vmq},
                 std::span<const uint_multiprecision_t>{vm4},
                 sign_d_outer_o,
                 tmp_double);
    // After: vm4 = c1, vmq = c9.

    // Odd inner pair: s_inner in vmh, d_inner in vm2 (sign_d_inner_o), lower = c3, higher = c7.
    recover_pair(vm2,
                 vmh,
                 std::span<const uint_multiprecision_t>{vmh},
                 std::span<const uint_multiprecision_t>{vm2},
                 sign_d_inner_o,
                 tmp_double);
    // After: vm2 = c3, vmh = c7.

    // -- Recompose: place each c_i at offset i*k in result. c0 (in result[0..2k))
    // and c11 (in result[11k..)) are already in position.
    //   c1  in vm4 -> offset  1*k
    //   c2  in v4  -> offset  2*k
    //   c3  in vm2 -> offset  3*k
    //   c4  in v2  -> offset  4*k
    //   c5  in vm1 -> offset  5*k
    //   c6  in v1  -> offset  6*k
    //   c7  in vmh -> offset  7*k
    //   c8  in vh  -> offset  8*k
    //   c9  in vmq -> offset  9*k
    //   c10 in vq  -> offset 10*k
    recompose(
        result, k, {vm4_view, v4_view, vm2_view, v2_view, vm1_view, v1_view, vmh_view, vh_view, vmq_view, vq_view});

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
}

void square_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));

    const std::size_t k                = (a.size() + 5) / 6; // ceil(n/6)
    const std::size_t effective_cutoff = cutoff_override == 0 ? square_toom_cook_6_5_cutoff : cutoff_override;

    // Fall through to the Toom-4 squaring variant below the performance cutoff
    // or below the algorithm's 5*k invariant (a5 must be non-empty). Squaring
    // is always balanced, so the general kernel's 7:6 ratio gate cannot trip.
    if (a.size() < effective_cutoff || a.size() <= 5 * k) {
        square_toom_cook_4(result, a, scratch);
        return;
    }

    // Split into six pieces of size k (a5 may be partial).
    const auto a0 = a.first(k);
    const auto a1 = a.subspan(k, k);
    const auto a2 = a.subspan(2 * k, k);
    const auto a3 = a.subspan(3 * k, k);
    const auto a4 = a.subspan(4 * k, k);
    const auto a5 = a.subspan(5 * k);

    // Carve scratch: one evaluation buffer tmpa (the general kernel needs
    // tmpa and tmpb); ten squares; tmp_double for in-place scaling. The
    // negative halves of the signed points stage in the corresponding vmX
    // buffer before it receives its square, as in the general kernel.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = tmp_cap + 11 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto v1         = block.subspan(tmp_cap + 0 * prod_cap, prod_cap);
    auto vm1        = block.subspan(tmp_cap + 1 * prod_cap, prod_cap);
    auto v2         = block.subspan(tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(tmp_cap + 3 * prod_cap, prod_cap);
    auto v4         = block.subspan(tmp_cap + 4 * prod_cap, prod_cap);
    auto vm4        = block.subspan(tmp_cap + 5 * prod_cap, prod_cap);
    auto vh         = block.subspan(tmp_cap + 6 * prod_cap, prod_cap);
    auto vmh        = block.subspan(tmp_cap + 7 * prod_cap, prod_cap);
    auto vq         = block.subspan(tmp_cap + 8 * prod_cap, prod_cap);
    auto vmq        = block.subspan(tmp_cap + 9 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(tmp_cap + 10 * prod_cap, prod_cap);

    // ---- c0 = a0^2, written into result[0..2k). Caller pre-zeroed result.
    // c11 = 0 for balanced squaring (the general kernel's b6 is empty), so
    // result[11k..) correctly stays zero. ----
    square_toom_cook_6_5(result.first(2 * a0.size()), a0, scratch);

    std::size_t tmpa_size = 0;
    std::size_t aux_size  = 0;

    // ---- Evaluate at x = 1: tmpa = a0+a1+a2+a3+a4+a5. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5});

    std::ranges::fill(v1, uint_multiprecision_t{0});
    square_toom_cook_6_5(v1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Evaluate at x = -1: tmpa = |(a0+a2+a4) - (a1+a3+a5)|. The square
    // erases the sign, here and at every signed point below, so the
    // interpolation uses the non-negative arms of the general kernel. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a2, a4});
    // Use vm1 (currently unused) as scratch for (a1+a3+a5).
    aux_size          = add_many_into_tmp(vm1, {a1, a3, a5});
    const auto sub_m1 = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpa_size         = sub_m1.size;

    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_6_5(vm1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 32a5+16a4+8a3+4a2+2a1+a0 (Horner). ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a5, a4, a3, a2, a1, a0}, 1u);

    std::ranges::fill(v2, uint_multiprecision_t{0});
    square_toom_cook_6_5(v2, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Evaluate at x = -2: tmpa = |(a0+4a2+16a4) - (2a1+8a3+32a5)|. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a4, a2, a0}, 2u);
    // negative part 2a1+8a3+32a5 into vm2 (currently unused).
    aux_size          = horner_eval_into_tmp(vm2, {a5, a3, a1}, 2u);
    aux_size          = shift_left_one(vm2, aux_size);
    const auto sub_m2 = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpa_size         = sub_m2.size;

    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_6_5(vm2, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // ---- Evaluate at x = 4: tmpa = 1024a5 + 256a4 + 64a3 + 16a2 + 4a1 + a0. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a5, a4, a3, a2, a1, a0}, 2u);

    std::ranges::fill(v4, uint_multiprecision_t{0});
    square_toom_cook_6_5(v4, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- Evaluate at x = -4: tmpa = |(a0+16a2+256a4) - (4a1+64a3+1024a5)|. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a4, a2, a0}, 4u);
    // negative part 4a1+64a3+1024a5 into vm4.
    aux_size          = horner_eval_into_tmp(vm4, {a5, a3, a1}, 4u);
    aux_size          = shift_left_n(vm4, aux_size, 2u);
    const auto sub_m4 = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{vm4.data(), aux_size});
    tmpa_size         = sub_m4.size;

    std::ranges::fill(vm4, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_6_5(vm4, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled): tmpa = 32a0+16a1+8a2+4a3+2a4+a5
    // = 32*p(1/2). The square is 1024*r(1/2); the interpolation expects the
    // general kernel's 2048*r(1/2) (it pairs a 6-piece and a 7-piece
    // evaluation), so shift left once after squaring. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5}, 1u);

    std::ranges::fill(vh, uint_multiprecision_t{0});
    square_toom_cook_6_5(vh, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    {
        [[maybe_unused]] const auto sz = shift_left_one(vh, vh.size());
        BEMAN_BIG_INT_DEBUG_ASSERT(sz == vh.size());
    }

    // ---- Evaluate at x = -1/2 (scaled): tmpa = |(32a0+8a2+2a4) - (16a1+4a3+a5)|
    // = |32*p(-1/2)|. Same 2x rescale as vh after squaring. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a2, a4}, 2u);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    // negative part 16a1+4a3+a5 into vmh.
    aux_size          = horner_eval_into_tmp(vmh, {a1, a3, a5}, 2u);
    const auto sub_mh = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{vmh.data(), aux_size});
    tmpa_size         = sub_mh.size;

    std::ranges::fill(vmh, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_6_5(vmh, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
        [[maybe_unused]] const auto sz = shift_left_one(vmh, vmh.size());
        BEMAN_BIG_INT_DEBUG_ASSERT(sz == vmh.size());
    }

    // ---- Evaluate at x = 1/4 (scaled): tmpa = 1024a0+256a1+64a2+16a3+4a4+a5
    // = 1024*p(1/4). The square is 4^10*r(1/4); shift left twice for the
    // general kernel's 4^11 scaling. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5}, 2u);

    std::ranges::fill(vq, uint_multiprecision_t{0});
    square_toom_cook_6_5(vq, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    {
        [[maybe_unused]] const auto sz = shift_left_n(vq, vq.size(), 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(sz == vq.size());
    }

    // ---- Evaluate at x = -1/4 (scaled): tmpa = |(1024a0+64a2+4a4) - (256a1+16a3+a5)|.
    // Same 4x rescale as vq after squaring. ----
    tmpa_size = horner_eval_into_tmp(tmpa, {a0, a2, a4}, 4u);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    // negative part 256a1+16a3+a5 into vmq.
    aux_size          = horner_eval_into_tmp(vmq, {a1, a3, a5}, 4u);
    const auto sub_mq = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{vmq.data(), aux_size});
    tmpa_size         = sub_mq.size;

    std::ranges::fill(vmq, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_6_5(vmq, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
        [[maybe_unused]] const auto sz = shift_left_n(vmq, vmq.size(), 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(sz == vmq.size());
    }

    // ---- Interpolation: the general kernel's sequence with every sign flag
    // pinned to false (squares are non-negative) and c11 = 0. ----

    const auto v0_view  = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto v1_view  = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view  = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view = std::span<const uint_multiprecision_t>{vm2};
    const auto v4_view  = std::span<const uint_multiprecision_t>{v4};
    const auto vm4_view = std::span<const uint_multiprecision_t>{vm4};
    const auto vh_view  = std::span<const uint_multiprecision_t>{vh};
    const auto vmh_view = std::span<const uint_multiprecision_t>{vmh};
    const auto vq_view  = std::span<const uint_multiprecision_t>{vq};
    const auto vmq_view = std::span<const uint_multiprecision_t>{vmq};

    // -- Phase 1: symmetrize each pair. v_x <- E_x; vm_x <- D_x. --
    {
        const auto rem = add_unsigned_spans_and_shift_right_one(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    subtract_unsigned_spans_no_borrow(vm1, v1_view, vm1_view);

    {
        const auto rem = add_unsigned_spans_and_shift_right_one(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    {
        const auto rem = add_unsigned_spans_and_shift_right_one(v4, v4_view, vm4_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(vm4, v4_view, vm4_view, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    {
        const auto rem = add_unsigned_spans_and_shift_right_one(vh, vh_view, vmh_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    subtract_unsigned_spans_no_borrow(vmh, vh_view, vmh_view);

    {
        const auto rem = add_unsigned_spans_and_shift_right_one(vq, vq_view, vmq_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    subtract_unsigned_spans_no_borrow(vmq, vq_view, vmq_view);

    // -- Phase 2: subtract the c0 contributions from the even rows; with
    // c11 = 0 the odd-row reductions collapse to plain exact shifts. --

    subtract_unsigned_spans(v1, v1_view, v0_view);
    subtract_unsigned_spans(v2, v2_view, v0_view);
    subtract_unsigned_spans(v4, v4_view, v0_view);

    // vh = (vh - 2048*c0) / 2.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    std::size_t td_size = trimmed_size_span(v0_view);
    td_size             = shift_left_n(tmp_double, td_size, 11u);
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_one(
            vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // vq = (vq - 4194304*c0) / 4.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    td_size = trimmed_size_span(v0_view);
    td_size = shift_left_n(tmp_double, td_size, 22u);
    {
        const auto rem = subtract_unsigned_spans_and_shift_right_n(
            vq, vq_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size}, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Odd side: c11 = 0, so (vmh - c11)/4 and (vmq - c11)/16 are plain exact
    // shifts and the c11 subtractions from vm1/vm2/vm4 vanish.
    {
        const auto rem = shift_right_n(vmh, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    {
        const auto rem = shift_right_n(vmq, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Pre-scale the odd rows so both subsystems share the same 5x5 matrix
    // (see the general kernel for the derivation).
    {
        // vm2 *= 4.
        std::size_t s = trimmed_size_span(vm2_view);
        s             = shift_left_n(vm2, s, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= vm2.size());
    }
    {
        // vm4 *= 16.
        std::size_t s = trimmed_size_span(vm4_view);
        s             = shift_left_n(vm4, s, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= vm4.size());
    }

    // -- Phase 3: solve the two 5x5 subsystems with the shared elimination. --
    const auto [sign_d_outer_e, sign_d_inner_e] = solve_subsystem(v1, v2, v4, vh, vq, tmp_double);
    const auto [sign_d_outer_o, sign_d_inner_o] = solve_subsystem(vm1, vm2, vm4, vmh, vmq, tmp_double);

    // -- Recover individual c-values from the sum/diff pairs (see the general
    // kernel for the aliasing discussion). --
    recover_pair(v4,
                 vq,
                 std::span<const uint_multiprecision_t>{vq},
                 std::span<const uint_multiprecision_t>{v4},
                 sign_d_outer_e,
                 tmp_double);
    // After: v4 = c2, vq = c10.

    recover_pair(v2,
                 vh,
                 std::span<const uint_multiprecision_t>{vh},
                 std::span<const uint_multiprecision_t>{v2},
                 sign_d_inner_e,
                 tmp_double);
    // After: v2 = c4, vh = c8.

    recover_pair(vm4,
                 vmq,
                 std::span<const uint_multiprecision_t>{vmq},
                 std::span<const uint_multiprecision_t>{vm4},
                 sign_d_outer_o,
                 tmp_double);
    // After: vm4 = c1, vmq = c9.

    recover_pair(vm2,
                 vmh,
                 std::span<const uint_multiprecision_t>{vmh},
                 std::span<const uint_multiprecision_t>{vm2},
                 sign_d_inner_o,
                 tmp_double);
    // After: vm2 = c3, vmh = c7.

    // -- Recompose: place each c_i at offset i*k in result; c0 is already in
    // position and c11 = 0. --
    recompose(
        result, k, {vm4_view, v4_view, vm2_view, v2_view, vm1_view, v1_view, vmh_view, vh_view, vmq_view, vq_view});

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
