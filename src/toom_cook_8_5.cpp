// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Sign flags for the three palindromic differences recovered by a 7x7 subsystem.
struct subsystem_signs_85 {
    bool d1;
    bool d2;
    bool d3;
};

// dst (magnitude) <- a_signed + k * b_signed, returning the result sign.
// |a| in a_view (sign a_sign); |b| in b_view (sign b_sign); k a small (< 2^32)
// positive constant. tmp holds k*|b|. dst may alias a_view's buffer; b_view and
// tmp must be distinct from dst. To compute a_signed - k*b_signed, pass !b_sign.
static bool signed_axpy(const std::span<uint_multiprecision_t>       dst,
                        const std::span<const uint_multiprecision_t> a_view,
                        const bool                                   a_sign,
                        const uint_multiprecision_t                  k,
                        const std::span<const uint_multiprecision_t> b_view,
                        const bool                                   b_sign,
                        const std::span<uint_multiprecision_t>       tmp) noexcept {
    const auto        bt = b_view.first(trimmed_size_span(b_view));
    const std::size_t ks = bt.empty() ? 0 : multiply_single_limb(tmp, bt, k);
    const auto        kb = std::span<const uint_multiprecision_t>{tmp.data(), ks};
    if (a_sign == b_sign) {
        const bool carry = add_unsigned_spans(dst, a_view, kb);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
        return a_sign;
    }
    const auto r = subtract_unsigned_spans_signed(dst, a_view, kb);
    return a_sign ^ r.negative;
}

// tmp <- src << total_bits, multi-limb safe (total_bits may exceed the limb
// width, unlike shift_left_n). Returns the significant size. Used for the
// c0/c15 endpoint removals whose scale factors reach 2^45 / 2^42.
static std::size_t stage_pow2_scaled(const std::span<uint_multiprecision_t>       tmp,
                                     const std::span<const uint_multiprecision_t> src,
                                     const std::size_t                            total_bits) noexcept {
    std::ranges::fill(tmp, uint_multiprecision_t{0});
    if (src.empty()) { // c15 = 0 (balanced / squaring): nothing to stage
        return 0;
    }
    const std::size_t     sz       = trimmed_size_span(src);
    constexpr std::size_t lb       = width_v<uint_multiprecision_t>;
    const std::size_t     limb_off = total_bits / lb;
    const auto            bit      = static_cast<unsigned>(total_bits % lb);
    std::ranges::copy(src.first(sz), tmp.subspan(limb_off).begin());
    return limb_off + shift_left_n(tmp.subspan(limb_off), sz, bit);
}

// Solves one palindromic 7x7 subsystem of the Toom-8.5 interpolation (shared by
// the general and squaring kernels). On entry the seven buffers hold the rows of
// the common matrix M dotted with the unknown coefficients:
//   r1 = all-ones row, r2/r4/r8 = integer rows (x^2 = 4/16/64),
//   rh/rq/re = reciprocal rows (x^2 = 1/4, 1/16, 1/64), normalized to M.
// The three reciprocal pairs fold into a 4x4 symmetric block (center + three
// palindromic sums) and a 3x3 antisymmetric block (three differences). On return:
//   r1 = center coeff; re = s1, rq = s2, rh = s3 (sums);
//   r2 = |d1|, r4 = |d2|, r8 = |d3| (differences, signs in the return value).
// tmp_double is the caller's 2k+2-limb shift/multiply scratch.
static subsystem_signs_85 solve_subsystem_85(const std::span<uint_multiprecision_t> r1,
                                             const std::span<uint_multiprecision_t> r2,
                                             const std::span<uint_multiprecision_t> r4,
                                             const std::span<uint_multiprecision_t> r8,
                                             const std::span<uint_multiprecision_t> rh,
                                             const std::span<uint_multiprecision_t> rq,
                                             const std::span<uint_multiprecision_t> re,
                                             const std::span<uint_multiprecision_t> tmp_double) noexcept {
    const auto r1v = std::span<const uint_multiprecision_t>{r1};
    const auto r2v = std::span<const uint_multiprecision_t>{r2};
    const auto r4v = std::span<const uint_multiprecision_t>{r4};
    const auto r8v = std::span<const uint_multiprecision_t>{r8};
    const auto rhv = std::span<const uint_multiprecision_t>{rh};
    const auto rqv = std::span<const uint_multiprecision_t>{rq};
    const auto rev = std::span<const uint_multiprecision_t>{re};

    // -- Fold each reciprocal pair into a symmetric (S) and antisymmetric (D)
    // row: S_t = r_int + t^2 * r_recip, D_t = r_int - t^2 * r_recip. t^2*r_recip
    // equals reverse(r_int), so S/D are the palindromic/antipalindromic parts. --
    bool sign_d2 = false;
    bool sign_d4 = false;
    bool sign_d8 = false;

    // Pair (r2, rh) with t^2 = 4: rh <- S_2; r2 <- |D_2|.
    {
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(rh, tmp_double.begin());
        const std::size_t s    = shift_left_n(tmp_double, trimmed_size_span(rhv), 2u);
        const auto        four = std::span<const uint_multiprecision_t>{tmp_double.data(), s};
        const bool        c    = add_unsigned_spans(rh, r2v, four);
        BEMAN_BIG_INT_DEBUG_ASSERT(!c);
        sign_d2 = subtract_unsigned_spans_signed(r2, r2v, four).negative;
    }
    // Pair (r4, rq) with t^2 = 16: rq <- S_4; r4 <- |D_4|.
    {
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(rq, tmp_double.begin());
        const std::size_t s   = shift_left_n(tmp_double, trimmed_size_span(rqv), 4u);
        const auto        s16 = std::span<const uint_multiprecision_t>{tmp_double.data(), s};
        const bool        c   = add_unsigned_spans(rq, r4v, s16);
        BEMAN_BIG_INT_DEBUG_ASSERT(!c);
        sign_d4 = subtract_unsigned_spans_signed(r4, r4v, s16).negative;
    }
    // Pair (r8, re) with t^2 = 64: re <- S_8; r8 <- |D_8|.
    {
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(re, tmp_double.begin());
        const std::size_t s   = shift_left_n(tmp_double, trimmed_size_span(rev), 6u);
        const auto        s64 = std::span<const uint_multiprecision_t>{tmp_double.data(), s};
        const bool        c   = add_unsigned_spans(re, r8v, s64);
        BEMAN_BIG_INT_DEBUG_ASSERT(!c);
        sign_d8 = subtract_unsigned_spans_signed(r8, r8v, s64).negative;
    }

    // -- 4x4 symmetric block (rows r1, S_2=rh, S_4=rq, S_8=re), unknowns
    // (m, s3, s2, s1). Eliminate m with the all-ones row, then s3, then s2;
    // back-substitute with exact divisions. All values stay non-negative. --
    const auto r1_trim = r1v.first(trimmed_size_span(r1v));
    subtract_shifted_unsigned(rh, rhv, r1_trim, 9u);  // S_2' = S_2 - 512*r1
    subtract_shifted_unsigned(rq, rqv, r1_trim, 17u); // S_4' = S_4 - 131072*r1
    subtract_shifted_unsigned(re, rev, r1_trim, 25u); // S_8' = S_8 - 33554432*r1

    // R2 = S_4' - 1600*S_2'; R3 = S_8' - 1806336*S_2'.
    {
        const auto rh_trim = rhv.first(trimmed_size_span(rhv));
        const auto m1600   = multiply_single_limb(tmp_double, rh_trim, uint_multiprecision_t{1600});
        subtract_unsigned_spans(rq, rqv, std::span<const uint_multiprecision_t>{tmp_double.data(), m1600});
        const auto m1806336 = multiply_single_limb(tmp_double, rh_trim, uint_multiprecision_t{1806336});
        subtract_unsigned_spans(re, rev, std::span<const uint_multiprecision_t>{tmp_double.data(), m1806336});
    }
    // R3b = R3 - 5712*R2.
    {
        const auto rq_trim = rqv.first(trimmed_size_span(rqv));
        const auto m5712   = multiply_single_limb(tmp_double, rq_trim, uint_multiprecision_t{5712});
        subtract_unsigned_spans(re, rev, std::span<const uint_multiprecision_t>{tmp_double.data(), m5712});
    }
    // s1 = R3b / 2981874772800 (= 2343600 * 1272348; split so each factor fits a
    // 32-bit limb). re <- s1.
    {
        const auto d1 = divide_unsigned_short(re, rev, uint_multiprecision_t{2343600});
        BEMAN_BIG_INT_DEBUG_ASSERT(d1 == 0);
        const auto d2 = divide_unsigned_short(re, rev, uint_multiprecision_t{1272348});
        BEMAN_BIG_INT_DEBUG_ASSERT(d2 == 0);
    }
    // s2 = (R2 - 242902800*s1) / 10886400. rq <- s2.
    {
        const auto re_trim = rev.first(trimmed_size_span(rev));
        const auto m       = multiply_single_limb(tmp_double, re_trim, uint_multiprecision_t{242902800});
        subtract_unsigned_spans(rq, rqv, std::span<const uint_multiprecision_t>{tmp_double.data(), m});
        const auto d = divide_unsigned_short(rq, rqv, uint_multiprecision_t{10886400});
        BEMAN_BIG_INT_DEBUG_ASSERT(d == 0);
    }
    // s3 = (S_2' - 3600*s2 - 15876*s1) / 576. rh <- s3.
    {
        const auto rq_trim = rqv.first(trimmed_size_span(rqv));
        const auto m3600   = multiply_single_limb(tmp_double, rq_trim, uint_multiprecision_t{3600});
        subtract_unsigned_spans(rh, rhv, std::span<const uint_multiprecision_t>{tmp_double.data(), m3600});
        const auto re_trim = rev.first(trimmed_size_span(rev));
        const auto m15876  = multiply_single_limb(tmp_double, re_trim, uint_multiprecision_t{15876});
        subtract_unsigned_spans(rh, rhv, std::span<const uint_multiprecision_t>{tmp_double.data(), m15876});
        const auto d = divide_unsigned_short(rh, rhv, uint_multiprecision_t{576});
        BEMAN_BIG_INT_DEBUG_ASSERT(d == 0);
    }
    // center m = r1 - s3 - s2 - s1. r1 <- m.
    subtract_unsigned_spans(r1, r1v, rhv);
    subtract_unsigned_spans(r1, r1v, rqv);
    subtract_unsigned_spans(r1, r1v, rev);

    // -- 3x3 antisymmetric block (rows D_2=r2, D_4=r4, D_8=r8), unknowns
    // (d1, d2, d3). Eliminate d1 using D_2, then d2; back-substitute. Signed
    // throughout: each combine is a sign-aware add/subtract of magnitudes. --
    // D_4' = D_4 - 16388*D_2; D_8' = D_8 - 268501008*D_2 (D_2 kept for d1).
    const bool sign_d4p = signed_axpy(r4, r4v, sign_d4, uint_multiprecision_t{16388}, r2v, !sign_d2, tmp_double);
    const bool sign_d8p = signed_axpy(r8, r8v, sign_d8, uint_multiprecision_t{268501008}, r2v, !sign_d2, tmp_double);
    // D_8'' = D_8' - 20500*D_4'.
    const bool sign_d8pp = signed_axpy(r8, r8v, sign_d8p, uint_multiprecision_t{20500}, r4v, !sign_d4p, tmp_double);
    // d3 = D_8'' / -44416512000 (= 293760 * 151200): magnitude / factors, sign flips.
    {
        const auto a = divide_unsigned_short(r8, r8v, uint_multiprecision_t{293760});
        BEMAN_BIG_INT_DEBUG_ASSERT(a == 0);
        const auto b = divide_unsigned_short(r8, r8v, uint_multiprecision_t{151200});
        BEMAN_BIG_INT_DEBUG_ASSERT(b == 0);
    }
    const bool sign_d3 = !sign_d8pp;
    // d2 = (D_4' - 14688000*d3) / 50086080.
    const bool sign_num_d2 =
        signed_axpy(r4, r4v, sign_d4p, uint_multiprecision_t{14688000}, r8v, !sign_d3, tmp_double);
    {
        const auto d = divide_unsigned_short(r4, r4v, uint_multiprecision_t{50086080});
        BEMAN_BIG_INT_DEBUG_ASSERT(d == 0);
    }
    const bool sign_d2_final = sign_num_d2;
    // d1 = (D_2 + 4080*d2 + 960*d3) / -16380: build numerator, divide, sign flips.
    bool sign_num_d1 = signed_axpy(r2, r2v, sign_d2, uint_multiprecision_t{4080}, r4v, sign_d2_final, tmp_double);
    sign_num_d1      = signed_axpy(r2, r2v, sign_num_d1, uint_multiprecision_t{960}, r8v, sign_d3, tmp_double);
    {
        const auto d = divide_unsigned_short(r2, r2v, uint_multiprecision_t{16380});
        BEMAN_BIG_INT_DEBUG_ASSERT(d == 0);
    }
    const bool sign_d1 = !sign_num_d1;

    return {sign_d1, sign_d2_final, sign_d3};
}

void multiply_toom_cook_8_5(const std::span<uint_multiprecision_t>       result,
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

    // Orient: a is the smaller (8 pieces), b is the larger (up to 9 pieces).
    const auto a = a_trim.size() <= b_trim.size() ? a_trim : b_trim;
    const auto b = a_trim.size() <= b_trim.size() ? b_trim : a_trim;

    const std::size_t min_size         = a.size();
    const std::size_t max_size         = b.size();
    const std::size_t k                = (min_size + 7) / 8; // ceil(min/8)
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_8_5_cutoff : cutoff_override;

    // Fall back to Toom-6.5 when below the performance cutoff, when the 7*k
    // invariant would leave a7 empty, or when the size ratio exceeds 9:8 so b
    // doesn't fit in nine pieces. Toom-6.5 cascades onward via its own gate.
    if (min_size < effective_cutoff || min_size <= 7 * k || max_size > 9 * k) {
        multiply_toom_cook_6_5(result, a_trim, b_trim, scratch);
        return;
    }

    // Split a into eight pieces of size k (a7 partial but non-empty).
    const auto a0 = a.subspan(0 * k, k);
    const auto a1 = a.subspan(1 * k, k);
    const auto a2 = a.subspan(2 * k, k);
    const auto a3 = a.subspan(3 * k, k);
    const auto a4 = a.subspan(4 * k, k);
    const auto a5 = a.subspan(5 * k, k);
    const auto a6 = a.subspan(6 * k, k);
    const auto a7 = a.subspan(7 * k);

    // Split b into up to nine pieces of size k (b7 may be partial; b8 may be empty).
    const auto b0 = b.subspan(0 * k, k);
    const auto b1 = b.subspan(1 * k, k);
    const auto b2 = b.subspan(2 * k, k);
    const auto b3 = b.subspan(3 * k, k);
    const auto b4 = b.subspan(4 * k, k);
    const auto b5 = b.subspan(5 * k, k);
    const auto b6 = b.subspan(6 * k, k);
    const auto b7 =
        b.size() > 7 * k ? b.subspan(7 * k, std::min(k, b.size() - 7 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b8 = b.size() > 8 * k ? b.subspan(8 * k) : std::span<const uint_multiprecision_t>{};

    // Scratch: two evaluation buffers (k+2 each), fourteen products + tmp_double
    // (2k+2 each). c0 = a0*b0 lives in result[0..2k); c15 = a7*b8 (when b8 is
    // non-empty) lives in result[15k..).
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 15 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto tmpb       = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap + 0 * prod_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + 1 * prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto v4         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto vm4        = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);
    auto v8         = block.subspan(2 * tmp_cap + 6 * prod_cap, prod_cap);
    auto vm8        = block.subspan(2 * tmp_cap + 7 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 8 * prod_cap, prod_cap);
    auto vmh        = block.subspan(2 * tmp_cap + 9 * prod_cap, prod_cap);
    auto vq         = block.subspan(2 * tmp_cap + 10 * prod_cap, prod_cap);
    auto vmq        = block.subspan(2 * tmp_cap + 11 * prod_cap, prod_cap);
    auto ve         = block.subspan(2 * tmp_cap + 12 * prod_cap, prod_cap);
    auto vme        = block.subspan(2 * tmp_cap + 13 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 14 * prod_cap, prod_cap);

    // ---- c0 = a0*b0 into result[0..2k); c15 = a7*b8 into result[15k..). ----
    multiply_toom_cook_8_5(result.first(a0.size() + b0.size()), a0, b0, scratch);
    if (!b8.empty()) {
        multiply_toom_cook_8_5(result.subspan(15 * k, a7.size() + b8.size()), a7, b8, scratch);
    }

    std::size_t tmpa_size = 0;
    std::size_t tmpb_size = 0;
    std::size_t aux_size  = 0;

    // ---- x = 1: tmpa = sum(a_i); tmpb = sum(b_j). ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5, a6, a7});
    tmpb_size = add_many_into_tmp(tmpb, {b0, b1, b2, b3, b4, b5, b6, b7, b8});
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_8_5(v1,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- x = -1: tmpa = (a0+a2+a4+a6) - (a1+a3+a5+a7); tmpb similar. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a2, a4, a6});
    aux_size  = add_many_into_tmp(vm1, {a1, a3, a5, a7});
    const auto sub_a_m1 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpa_size = sub_a_m1.size;
    tmpb_size = add_many_into_tmp(tmpb, {b0, b2, b4, b6, b8});
    aux_size  = add_many_into_tmp(vm1, {b1, b3, b5, b7});
    const auto sub_b_m1 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpb_size           = sub_b_m1.size;
    const bool sign_vm1 = sub_a_m1.negative ^ sub_b_m1.negative;
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_8_5(vm1,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // Integer points x = +-2, +-4, +-8 via Horner (positive) and even/odd split
    // (negative). horner shift = log2(x); the odd half is shifted one extra
    // log2(x) so the negative value is (even part) - x*(odd part).
    struct int_point {
        std::span<uint_multiprecision_t> vp;
        std::span<uint_multiprecision_t> vm;
        unsigned                         sh;       // horner shift = log2(x)
        bool*                            sign_out; // sign of the product at -x
    };
    bool            sign_vm2 = false;
    bool            sign_vm4 = false;
    bool            sign_vm8 = false;
    const int_point ipts[3]  = {{v2, vm2, 1u, &sign_vm2}, {v4, vm4, 2u, &sign_vm4}, {v8, vm8, 3u, &sign_vm8}};
    for (const auto& p : ipts) {
        // +x: full Horner high-to-low.
        tmpa_size = horner_eval_into_tmp(tmpa, {a7, a6, a5, a4, a3, a2, a1, a0}, p.sh);
        tmpb_size = horner_eval_into_tmp(tmpb, {b8, b7, b6, b5, b4, b3, b2, b1, b0}, p.sh);
        std::ranges::fill(p.vp, uint_multiprecision_t{0});
        multiply_toom_cook_8_5(p.vp,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
        // -x: a-side (even part) - (odd part << log2(x)).
        tmpa_size     = horner_eval_into_tmp(tmpa, {a6, a4, a2, a0}, 2u * p.sh);
        aux_size      = horner_eval_into_tmp(p.vm, {a7, a5, a3, a1}, 2u * p.sh);
        aux_size      = shift_left_n(p.vm, aux_size, p.sh);
        const auto sa = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{p.vm.data(), aux_size});
        tmpa_size     = sa.size;
        tmpb_size     = horner_eval_into_tmp(tmpb, {b8, b6, b4, b2, b0}, 2u * p.sh);
        aux_size      = horner_eval_into_tmp(p.vm, {b7, b5, b3, b1}, 2u * p.sh);
        aux_size      = shift_left_n(p.vm, aux_size, p.sh);
        const auto sb = subtract_unsigned_spans_signed(tmpb,
                                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                                       std::span<const uint_multiprecision_t>{p.vm.data(), aux_size});
        tmpb_size     = sb.size;
        *p.sign_out   = sa.negative ^ sb.negative;
        std::ranges::fill(p.vm, uint_multiprecision_t{0});
        if (tmpa_size != 0 && tmpb_size != 0) {
            multiply_toom_cook_8_5(p.vm,
                                   std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                   std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                   scratch);
        }
    }

    // Reciprocal points x = +-1/2, +-1/4, +-1/8 via reverse Horner. The +x value
    // is base^7 p(1/x) * base^8 q(1/x) = base^15 r(1/x). For -x the operand
    // degree parities differ (p odd, q even), so the a-side positive half and the
    // b-side negative half each take the extra log2(x) shift.
    struct rec_point {
        std::span<uint_multiprecision_t> vp;
        std::span<uint_multiprecision_t> vm;
        unsigned                         sh; // log2(base)
        bool*                            sign_out;
    };
    bool            sign_vmh = false;
    bool            sign_vmq = false;
    bool            sign_vme = false;
    const rec_point rpts[3]  = {{vh, vmh, 1u, &sign_vmh}, {vq, vmq, 2u, &sign_vmq}, {ve, vme, 3u, &sign_vme}};
    for (const auto& p : rpts) {
        tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5, a6, a7}, p.sh);
        tmpb_size = horner_eval_into_tmp(tmpb, {b0, b1, b2, b3, b4, b5, b6, b7, b8}, p.sh);
        std::ranges::fill(p.vp, uint_multiprecision_t{0});
        multiply_toom_cook_8_5(p.vp,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
        // -x: a-side positive half shifted; b-side negative half shifted.
        tmpa_size     = horner_eval_into_tmp(tmpa, {a0, a2, a4, a6}, 2u * p.sh);
        tmpa_size     = shift_left_n(tmpa, tmpa_size, p.sh);
        aux_size      = horner_eval_into_tmp(p.vm, {a1, a3, a5, a7}, 2u * p.sh);
        const auto sa = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{p.vm.data(), aux_size});
        tmpa_size     = sa.size;
        tmpb_size     = horner_eval_into_tmp(tmpb, {b0, b2, b4, b6, b8}, 2u * p.sh);
        aux_size      = horner_eval_into_tmp(p.vm, {b1, b3, b5, b7}, 2u * p.sh);
        aux_size      = shift_left_n(p.vm, aux_size, p.sh);
        const auto sb = subtract_unsigned_spans_signed(tmpb,
                                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                                       std::span<const uint_multiprecision_t>{p.vm.data(), aux_size});
        tmpb_size     = sb.size;
        *p.sign_out   = sa.negative ^ sb.negative;
        std::ranges::fill(p.vm, uint_multiprecision_t{0});
        if (tmpa_size != 0 && tmpb_size != 0) {
            multiply_toom_cook_8_5(p.vm,
                                   std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                   std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                   scratch);
        }
    }

    // ---- Interpolation. Each (v_x, vm_x) folds into the even part E_x (-> v_x)
    // and odd part D_x (-> vm_x). Seven even rows + c0 recover c2,c4,..,c14; seven
    // odd rows + c15 recover c1,c3,..,c13. Both share the matrix solved by
    // solve_subsystem_85. ----
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = result.size() > 15 * k ? std::min(result.size() - 15 * k, 2 * k) : std::size_t{0};
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 15 * k, vinf_size};

    const auto v1v  = std::span<const uint_multiprecision_t>{v1};
    const auto vm1v = std::span<const uint_multiprecision_t>{vm1};
    const auto v2v  = std::span<const uint_multiprecision_t>{v2};
    const auto vm2v = std::span<const uint_multiprecision_t>{vm2};
    const auto v4v  = std::span<const uint_multiprecision_t>{v4};
    const auto vm4v = std::span<const uint_multiprecision_t>{vm4};
    const auto v8v  = std::span<const uint_multiprecision_t>{v8};
    const auto vm8v = std::span<const uint_multiprecision_t>{vm8};
    const auto vhv  = std::span<const uint_multiprecision_t>{vh};
    const auto vmhv = std::span<const uint_multiprecision_t>{vmh};
    const auto vqv  = std::span<const uint_multiprecision_t>{vq};
    const auto vmqv = std::span<const uint_multiprecision_t>{vmq};
    const auto vev  = std::span<const uint_multiprecision_t>{ve};
    const auto vmev = std::span<const uint_multiprecision_t>{vme};

    // -- Phase 1: symmetrize. v_x <- E_x = (v_x + vm_x)/2; vm_x <- D_x =
    // (v_x - vm_x)/(2x), with the /(2x) absorbed via the post-subtract shift. --
    auto symmetrize = [](const std::span<uint_multiprecision_t>       vp,
                         const std::span<const uint_multiprecision_t> vpv,
                         const std::span<uint_multiprecision_t>       vm,
                         const std::span<const uint_multiprecision_t> vmv,
                         const bool                                   sign,
                         const unsigned                               d_shift) {
        const auto rem = sign ? subtract_unsigned_spans_and_shift_right_one(vp, vpv, vmv)
                              : add_unsigned_spans_and_shift_right_one(vp, vpv, vmv);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
        if (d_shift == 0) {
            sign ? add_unsigned_spans_no_carry(vm, vpv, vmv) : subtract_unsigned_spans_no_borrow(vm, vpv, vmv);
        } else {
            const auto r2 = sign ? add_unsigned_spans_and_shift_right_n(vm, vpv, vmv, d_shift)
                                 : subtract_unsigned_spans_and_shift_right_n(vm, vpv, vmv, d_shift);
            BEMAN_BIG_INT_DEBUG_ASSERT(r2 == 0);
        }
    };
    symmetrize(v1, v1v, vm1, vm1v, sign_vm1, 0u);
    symmetrize(v2, v2v, vm2, vm2v, sign_vm2, 1u);
    symmetrize(v4, v4v, vm4, vm4v, sign_vm4, 2u);
    symmetrize(v8, v8v, vm8, vm8v, sign_vm8, 3u);
    symmetrize(vh, vhv, vmh, vmhv, sign_vmh, 0u);
    symmetrize(vq, vqv, vmq, vmqv, sign_vmq, 0u);
    symmetrize(ve, vev, vme, vmev, sign_vme, 0u);

    // -- Phase 2: remove c0 from the even rows and normalize the reciprocal rows;
    // remove c15 from the odd rows (no-ops when c15 = 0) and prescale the odd
    // integer rows so both subsystems share the matrix M. --
    subtract_unsigned_spans(v1, v1v, v0_view);
    subtract_unsigned_spans(v2, v2v, v0_view);
    subtract_unsigned_spans(v4, v4v, v0_view);
    subtract_unsigned_spans(v8, v8v, v0_view);
    {
        std::size_t s = stage_pow2_scaled(tmp_double, v0_view, 15);
        const auto  r = subtract_unsigned_spans_and_shift_right_one(
            vh, vhv, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        BEMAN_BIG_INT_DEBUG_ASSERT(r == 0);
        s             = stage_pow2_scaled(tmp_double, v0_view, 30);
        const auto rq = subtract_unsigned_spans_and_shift_right_n(
            vq, vqv, std::span<const uint_multiprecision_t>{tmp_double.data(), s}, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rq == 0);
        s             = stage_pow2_scaled(tmp_double, v0_view, 45);
        const auto re = subtract_unsigned_spans_and_shift_right_n(
            ve, vev, std::span<const uint_multiprecision_t>{tmp_double.data(), s}, 3u);
        BEMAN_BIG_INT_DEBUG_ASSERT(re == 0);
    }

    // Odd side: subtract c15 * (power) from each row (vinf_view is empty when b8
    // is empty, making these no-ops). vmh/vmq/vme fold the c15 removal into the
    // normalizing shift.
    subtract_unsigned_spans(vm1, vm1v, vinf_view);
    {
        std::size_t s = stage_pow2_scaled(tmp_double, vinf_view, 14);
        subtract_unsigned_spans(vm2, vm2v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        s = stage_pow2_scaled(tmp_double, vinf_view, 28);
        subtract_unsigned_spans(vm4, vm4v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        s = stage_pow2_scaled(tmp_double, vinf_view, 42);
        subtract_unsigned_spans(vm8, vm8v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
    }
    {
        const auto rh = subtract_unsigned_spans_and_shift_right_n(vmh, vmhv, vinf_view, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rh == 0);
        const auto rq = subtract_unsigned_spans_and_shift_right_n(vmq, vmqv, vinf_view, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rq == 0);
        const auto re = subtract_unsigned_spans_and_shift_right_n(vme, vmev, vinf_view, 6u);
        BEMAN_BIG_INT_DEBUG_ASSERT(re == 0);
    }
    // Prescale odd integer rows: vm2*=4, vm4*=16, vm8*=64.
    {
        const auto s2 = shift_left_n(vm2, trimmed_size_span(vm2v), 2u);
        const auto s4 = shift_left_n(vm4, trimmed_size_span(vm4v), 4u);
        const auto s8 = shift_left_n(vm8, trimmed_size_span(vm8v), 6u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s2 <= vm2.size() && s4 <= vm4.size() && s8 <= vm8.size());
    }

    // -- Solve both subsystems, then recover each coefficient pair. --
    const auto se = solve_subsystem_85(v1, v2, v4, v8, vh, vq, ve, tmp_double);
    const auto so = solve_subsystem_85(vm1, vm2, vm4, vm8, vmh, vmq, vme, tmp_double);

    // Even: v1 = c8; (s1,d1)=(ve,v2)->c2,c14; (s2,d2)=(vq,v4)->c4,c12; (s3,d3)=(vh,v8)->c6,c10.
    recover_pair(v2, ve, vev, v2v, se.d1, tmp_double);
    recover_pair(v4, vq, vqv, v4v, se.d2, tmp_double);
    recover_pair(v8, vh, vhv, v8v, se.d3, tmp_double);
    // Odd: vm1 = c7; pairs -> c1,c13; c3,c11; c5,c9.
    recover_pair(vm2, vme, vmev, vm2v, so.d1, tmp_double);
    recover_pair(vm4, vmq, vmqv, vm4v, so.d2, tmp_double);
    recover_pair(vm8, vmh, vmhv, vm8v, so.d3, tmp_double);

    // -- Recompose c1..c14 at offsets 1k..14k; c0 and c15 already in place. --
    recompose(result, k, {vm2v, v2v, vm4v, v4v, vm8v, v8v, vm1v, v1v, vmhv, vhv, vmqv, vqv, vmev, vev});

    scratch.deallocate(total_scratch);
}

void square_toom_cook_8_5(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));

    const std::size_t k                = (a.size() + 7) / 8; // ceil(n/8)
    const std::size_t effective_cutoff = cutoff_override == 0 ? square_toom_cook_8_5_cutoff : cutoff_override;

    // Squaring is always balanced (the general kernel's b8 is empty, c15 = 0).
    if (a.size() < effective_cutoff || a.size() <= 7 * k) {
        square_toom_cook_6_5(result, a, scratch);
        return;
    }

    const auto a0 = a.subspan(0 * k, k);
    const auto a1 = a.subspan(1 * k, k);
    const auto a2 = a.subspan(2 * k, k);
    const auto a3 = a.subspan(3 * k, k);
    const auto a4 = a.subspan(4 * k, k);
    const auto a5 = a.subspan(5 * k, k);
    const auto a6 = a.subspan(6 * k, k);
    const auto a7 = a.subspan(7 * k);

    // One evaluation buffer (the general kernel needs two); fourteen squares plus
    // tmp_double. The negative halves stage in the corresponding vmX buffer.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = tmp_cap + 15 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto v1         = block.subspan(tmp_cap + 0 * prod_cap, prod_cap);
    auto vm1        = block.subspan(tmp_cap + 1 * prod_cap, prod_cap);
    auto v2         = block.subspan(tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(tmp_cap + 3 * prod_cap, prod_cap);
    auto v4         = block.subspan(tmp_cap + 4 * prod_cap, prod_cap);
    auto vm4        = block.subspan(tmp_cap + 5 * prod_cap, prod_cap);
    auto v8         = block.subspan(tmp_cap + 6 * prod_cap, prod_cap);
    auto vm8        = block.subspan(tmp_cap + 7 * prod_cap, prod_cap);
    auto vh         = block.subspan(tmp_cap + 8 * prod_cap, prod_cap);
    auto vmh        = block.subspan(tmp_cap + 9 * prod_cap, prod_cap);
    auto vq         = block.subspan(tmp_cap + 10 * prod_cap, prod_cap);
    auto vmq        = block.subspan(tmp_cap + 11 * prod_cap, prod_cap);
    auto ve         = block.subspan(tmp_cap + 12 * prod_cap, prod_cap);
    auto vme        = block.subspan(tmp_cap + 13 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(tmp_cap + 14 * prod_cap, prod_cap);

    // ---- c0 = a0^2 into result[0..2k); c15 = 0 (result[15k..) stays zero). ----
    square_toom_cook_8_5(result.first(2 * a0.size()), a0, scratch);

    std::size_t tmpa_size = 0;
    std::size_t aux_size  = 0;

    // ---- x = 1. ----
    tmpa_size = add_many_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5, a6, a7});
    std::ranges::fill(v1, uint_multiprecision_t{0});
    square_toom_cook_8_5(v1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

    // ---- x = -1: the square erases the sign, so interpolation uses the
    // non-negative arms throughout (every sign flag is false below). ----
    tmpa_size         = add_many_into_tmp(tmpa, {a0, a2, a4, a6});
    aux_size          = add_many_into_tmp(vm1, {a1, a3, a5, a7});
    const auto sub_m1 = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpa_size         = sub_m1.size;
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0) {
        square_toom_cook_8_5(vm1, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
    }

    // Integer points +-2, +-4, +-8.
    struct int_point {
        std::span<uint_multiprecision_t> vp;
        std::span<uint_multiprecision_t> vm;
        unsigned                         sh;
    };
    const int_point ipts[3] = {{v2, vm2, 1u}, {v4, vm4, 2u}, {v8, vm8, 3u}};
    for (const auto& p : ipts) {
        tmpa_size = horner_eval_into_tmp(tmpa, {a7, a6, a5, a4, a3, a2, a1, a0}, p.sh);
        std::ranges::fill(p.vp, uint_multiprecision_t{0});
        square_toom_cook_8_5(p.vp, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);

        tmpa_size     = horner_eval_into_tmp(tmpa, {a6, a4, a2, a0}, 2u * p.sh);
        aux_size      = horner_eval_into_tmp(p.vm, {a7, a5, a3, a1}, 2u * p.sh);
        aux_size      = shift_left_n(p.vm, aux_size, p.sh);
        const auto sa = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{p.vm.data(), aux_size});
        tmpa_size     = sa.size;
        std::ranges::fill(p.vm, uint_multiprecision_t{0});
        if (tmpa_size != 0) {
            square_toom_cook_8_5(p.vm, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
        }
    }

    // Reciprocal points +-1/2, +-1/4, +-1/8. The square comes out scaled by
    // base^14 instead of the base^15 the (9x8) interpolation expects, so shift
    // left log2(base) after squaring.
    struct rec_point {
        std::span<uint_multiprecision_t> vp;
        std::span<uint_multiprecision_t> vm;
        unsigned                         sh;
    };
    const rec_point rpts[3] = {{vh, vmh, 1u}, {vq, vmq, 2u}, {ve, vme, 3u}};
    for (const auto& p : rpts) {
        tmpa_size = horner_eval_into_tmp(tmpa, {a0, a1, a2, a3, a4, a5, a6, a7}, p.sh);
        std::ranges::fill(p.vp, uint_multiprecision_t{0});
        square_toom_cook_8_5(p.vp, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
        {
            [[maybe_unused]] const auto sz = shift_left_n(p.vp, p.vp.size(), p.sh);
            BEMAN_BIG_INT_DEBUG_ASSERT(sz == p.vp.size());
        }

        tmpa_size     = horner_eval_into_tmp(tmpa, {a0, a2, a4, a6}, 2u * p.sh);
        tmpa_size     = shift_left_n(tmpa, tmpa_size, p.sh);
        aux_size      = horner_eval_into_tmp(p.vm, {a1, a3, a5, a7}, 2u * p.sh);
        const auto sa = subtract_unsigned_spans_signed(tmpa,
                                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                                       std::span<const uint_multiprecision_t>{p.vm.data(), aux_size});
        tmpa_size     = sa.size;
        std::ranges::fill(p.vm, uint_multiprecision_t{0});
        if (tmpa_size != 0) {
            square_toom_cook_8_5(p.vm, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, scratch);
            [[maybe_unused]] const auto sz = shift_left_n(p.vm, p.vm.size(), p.sh);
            BEMAN_BIG_INT_DEBUG_ASSERT(sz == p.vm.size());
        }
    }

    // ---- Interpolation: the general kernel's sequence with every sign pinned
    // false and c15 = 0. ----
    const auto v0_view = std::span<const uint_multiprecision_t>{result.data(), 2 * k};

    const auto v1v  = std::span<const uint_multiprecision_t>{v1};
    const auto vm1v = std::span<const uint_multiprecision_t>{vm1};
    const auto v2v  = std::span<const uint_multiprecision_t>{v2};
    const auto vm2v = std::span<const uint_multiprecision_t>{vm2};
    const auto v4v  = std::span<const uint_multiprecision_t>{v4};
    const auto vm4v = std::span<const uint_multiprecision_t>{vm4};
    const auto v8v  = std::span<const uint_multiprecision_t>{v8};
    const auto vm8v = std::span<const uint_multiprecision_t>{vm8};
    const auto vhv  = std::span<const uint_multiprecision_t>{vh};
    const auto vmhv = std::span<const uint_multiprecision_t>{vmh};
    const auto vqv  = std::span<const uint_multiprecision_t>{vq};
    const auto vmqv = std::span<const uint_multiprecision_t>{vmq};
    const auto vev  = std::span<const uint_multiprecision_t>{ve};
    const auto vmev = std::span<const uint_multiprecision_t>{vme};

    auto symmetrize = [](const std::span<uint_multiprecision_t>       vp,
                         const std::span<const uint_multiprecision_t> vpv,
                         const std::span<uint_multiprecision_t>       vm,
                         const std::span<const uint_multiprecision_t> vmv,
                         const unsigned                               d_shift) {
        const auto rem = add_unsigned_spans_and_shift_right_one(vp, vpv, vmv);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
        if (d_shift == 0) {
            subtract_unsigned_spans_no_borrow(vm, vpv, vmv);
        } else {
            const auto r2 = subtract_unsigned_spans_and_shift_right_n(vm, vpv, vmv, d_shift);
            BEMAN_BIG_INT_DEBUG_ASSERT(r2 == 0);
        }
    };
    symmetrize(v1, v1v, vm1, vm1v, 0u);
    symmetrize(v2, v2v, vm2, vm2v, 1u);
    symmetrize(v4, v4v, vm4, vm4v, 2u);
    symmetrize(v8, v8v, vm8, vm8v, 3u);
    symmetrize(vh, vhv, vmh, vmhv, 0u);
    symmetrize(vq, vqv, vmq, vmqv, 0u);
    symmetrize(ve, vev, vme, vmev, 0u);

    subtract_unsigned_spans(v1, v1v, v0_view);
    subtract_unsigned_spans(v2, v2v, v0_view);
    subtract_unsigned_spans(v4, v4v, v0_view);
    subtract_unsigned_spans(v8, v8v, v0_view);
    {
        std::size_t s = stage_pow2_scaled(tmp_double, v0_view, 15);
        const auto  r = subtract_unsigned_spans_and_shift_right_one(
            vh, vhv, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        BEMAN_BIG_INT_DEBUG_ASSERT(r == 0);
        s             = stage_pow2_scaled(tmp_double, v0_view, 30);
        const auto rq = subtract_unsigned_spans_and_shift_right_n(
            vq, vqv, std::span<const uint_multiprecision_t>{tmp_double.data(), s}, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rq == 0);
        s             = stage_pow2_scaled(tmp_double, v0_view, 45);
        const auto re = subtract_unsigned_spans_and_shift_right_n(
            ve, vev, std::span<const uint_multiprecision_t>{tmp_double.data(), s}, 3u);
        BEMAN_BIG_INT_DEBUG_ASSERT(re == 0);
    }
    // Odd side: c15 = 0, so the reciprocal reductions are plain shifts.
    {
        const auto rh = shift_right_n(vmh, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rh == 0);
        const auto rq = shift_right_n(vmq, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rq == 0);
        const auto re = shift_right_n(vme, 6u);
        BEMAN_BIG_INT_DEBUG_ASSERT(re == 0);
    }
    {
        const auto s2 = shift_left_n(vm2, trimmed_size_span(vm2v), 2u);
        const auto s4 = shift_left_n(vm4, trimmed_size_span(vm4v), 4u);
        const auto s8 = shift_left_n(vm8, trimmed_size_span(vm8v), 6u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s2 <= vm2.size() && s4 <= vm4.size() && s8 <= vm8.size());
    }

    const auto se = solve_subsystem_85(v1, v2, v4, v8, vh, vq, ve, tmp_double);
    const auto so = solve_subsystem_85(vm1, vm2, vm4, vm8, vmh, vmq, vme, tmp_double);

    recover_pair(v2, ve, vev, v2v, se.d1, tmp_double);
    recover_pair(v4, vq, vqv, v4v, se.d2, tmp_double);
    recover_pair(v8, vh, vhv, v8v, se.d3, tmp_double);
    recover_pair(vm2, vme, vmev, vm2v, so.d1, tmp_double);
    recover_pair(vm4, vmq, vmqv, vm4v, so.d2, tmp_double);
    recover_pair(vm8, vmh, vmhv, vm8v, so.d3, tmp_double);

    recompose(result, k, {vm2v, v2v, vm4v, v4v, vm8v, v8v, vm1v, v1v, vmhv, vhv, vmqv, vqv, vmev, vev});

    scratch.deallocate(total_scratch);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
