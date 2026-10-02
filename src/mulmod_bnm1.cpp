// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>

// Wraparound multiplication a * b mod (B^w - 1), compiled once; the header
// keeps the constexpr fold helpers, the size chooser, and the storage model.
// `scratch` must carry the type-erased heap hooks (every
// scratch_allocator<Allocator> installs them) for the internal products and
// the cyclic NTT workspaces.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

namespace {

// r = a * b mod (B^h + 1) with h + 1 = r.size(); a and b canonical in
// [0, B^h] as produced by fold_mod_bnp1. One full h x h product plus a
// signed fold; operands equal to B^h itself (== -1) shortcut to a negation.
void multiply_mod_bnp1(const std::span<uint_multiprecision_t>       r,
                       const std::span<const uint_multiprecision_t> a,
                       const std::span<const uint_multiprecision_t> b,
                       scratch_allocator_base&                      scratch) {
    const std::size_t h = r.size() - 1;
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == h + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() == h + 1);

    const auto negate_into = [&](const std::span<const uint_multiprecision_t> x) {
        // r = (B^h + 1) - x for x in (0, B^h], r = 0 for x == 0.
        if (is_span_zero(x)) {
            std::ranges::fill(r, uint_multiprecision_t{0});
            return;
        }
        std::ranges::fill(r, uint_multiprecision_t{0});
        r[0] = 1;
        r[h] = 1;
        subtract_unsigned_spans(r, r, x);
    };

    if (a[h] != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(a.first(h)));
        negate_into(b);
        return;
    }
    if (b[h] != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(b.first(h)));
        negate_into(a);
        return;
    }

    const std::span<uint_multiprecision_t> prod = scratch.allocate(2 * h);
    std::ranges::fill(prod, uint_multiprecision_t{0});
    multiply_runtime_any(prod, a.first(h), b.first(h), scratch.heap());
    fold_mod_bnp1(r, prod);
    scratch.deallocate(2 * h);
}

} // namespace

// ---------------------------------------------------------------------------
// r = a * b mod (B^w - 1) with w = r.size(), semi-canonical (all-ones means
// zero). a.size() and b.size() must be at most w (fold larger operands
// first); r must not alias the inputs. `scratch` provides
// multiply_mod_bnm1_storage_size(w) limbs.
// Odd wrap sizes fall back to the plain product (size via
// multiply_mod_bnm1_next_size to keep the recursion even).
// `cutoff_override` is a test-only escape hatch forcing deep recursion.
// ---------------------------------------------------------------------------
void multiply_mod_bnm1(const std::span<uint_multiprecision_t>       r,
                       const std::span<const uint_multiprecision_t> a,
                       const std::span<const uint_multiprecision_t> b,
                       scratch_allocator_base&                      scratch,
                       const std::size_t                            cutoff_override) {
    const std::size_t w = r.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(w >= 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() <= w);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() <= w);
    BEMAN_BIG_INT_DEBUG_ASSERT(r.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(r.data() != b.data());

    const std::size_t cutoff = cutoff_override != 0 ? cutoff_override : multiply_mod_bnm1_cutoff;

    // Plain product when the wrap cannot engage (no wraparound, odd size, or
    // too small to be worth the CRT split).
    if (w <= cutoff || (w % 2) != 0 || a.size() + b.size() <= w) {
        const std::size_t                      p_len = a.size() + b.size();
        const std::span<uint_multiprecision_t> prod  = scratch.allocate(p_len);
        std::ranges::fill(prod, uint_multiprecision_t{0});
        multiply_runtime_any(prod, a, b, scratch.heap());
        fold_mod_bnm1(r, std::span<const uint_multiprecision_t>{prod.data(), p_len});
        scratch.deallocate(p_len);
        return;
    }

    // Cyclic NTT tier: one length-L transform set computes the wrapped
    // product directly when w is a chooser size (next_size produces exactly
    // these above the cutoff). Transform workspaces live on the heap like
    // multiply_dispatch's FFT branch, so the scratch model is untouched.
    // The test-only override keeps forcing the CRT recursion.
    if constexpr (width_v<uint_multiprecision_t> == 64) {
        if (cutoff_override == 0 && w >= fft_cyclic_cutoff) {
            const fft_cyclic_params params = multiply_fft_cyclic_next_size(w);
            if (params.wrap_limbs == w) {
                if (is_span_zero(a) || is_span_zero(b)) {
                    std::ranges::fill(r, uint_multiprecision_t{0});
                    return;
                }
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                scratch_heap_array<double>        fp_ws(scratch.heap(), fft_cyclic_fp_storage_size(params));
                scratch_heap_array<std::uint64_t> int_ws(scratch.heap(), fft_cyclic_int_storage_size(params));
                multiply_fft_cyclic(r, a, b, params, fp_ws.span(), int_ws.span());
#else
                scratch_heap_array<std::uint64_t> ws(scratch.heap(), fft_cyclic_storage_size(params));
                multiply_fft_cyclic(r, a, b, params, ws.span());
#endif
                return;
            }
        }
    }

    const std::size_t h = w / 2;

    // Half 1 (recursive): rm1 = a*b mod (B^h - 1), built into r's low half.
    {
        const std::span<uint_multiprecision_t> am1 = scratch.allocate(h);
        const std::span<uint_multiprecision_t> bm1 = scratch.allocate(h);
        fold_mod_bnm1(am1, a);
        fold_mod_bnm1(bm1, b);
        multiply_mod_bnm1(r.first(h),
                          std::span<const uint_multiprecision_t>{am1.data(), h},
                          std::span<const uint_multiprecision_t>{bm1.data(), h},
                          scratch,
                          cutoff_override);
        scratch.deallocate(h);
        scratch.deallocate(h);
    }

    // Half 2: rp1 = a*b mod (B^h + 1).
    const std::span<uint_multiprecision_t> ap1 = scratch.allocate(h + 1);
    const std::span<uint_multiprecision_t> bp1 = scratch.allocate(h + 1);
    const std::span<uint_multiprecision_t> rp1 = scratch.allocate(h + 1);
    fold_mod_bnp1(ap1, a);
    fold_mod_bnp1(bp1, b);
    multiply_mod_bnp1(rp1,
                      std::span<const uint_multiprecision_t>{ap1.data(), h + 1},
                      std::span<const uint_multiprecision_t>{bp1.data(), h + 1},
                      scratch);

    // CRT: r = rm1 + t * (B^h - 1) with t = (rm1 - rp1) / 2 mod (B^h + 1).
    // Reuse ap1's buffer for t.
    const std::span<uint_multiprecision_t> t = ap1;
    {
        std::ranges::copy(r.first(h), t.begin());
        t[h] = 0;
        if (compare_unsigned_spans(t.first(h), rp1) == std::strong_ordering::less) {
            // t = rm1 + (B^h + 1) before the subtraction.
            t[h] = increment_span(t.first(h)) ? 2 : 1;
        }
        subtract_unsigned_spans(t, t, rp1);
        if ((t[0] & 1u) != 0) {
            // Make the value even by adding B^h + 1 once more before halving.
            const bool wrapped = increment_span(t.first(h));
            t[h]               = t[h] + uint_multiprecision_t{1} + uint_multiprecision_t{wrapped};
        }
        const uint_multiprecision_t dropped = shift_right_n(t, 1u);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(t[h] <= 1);
    }

    // Assemble in place: r = [rm1 | t_low] (+ 1 if t's top limb carries the
    // B^w == 1 wrap), then a modular subtraction of t.
    std::ranges::copy(t.first(h), r.begin() + static_cast<std::ptrdiff_t>(h));
    if (t[h] != 0) {
        if (increment_span(r)) {
            r[0] = 1;
        }
    }
    if (subtract_unsigned_spans_borrow_out(r, r, std::span<const uint_multiprecision_t>{t.data(), h + 1})) {
        // Wrapped past zero: -B^w == -1 (mod B^w - 1).
        [[maybe_unused]] const bool all_zero = decrement_span(r);
    }

    scratch.deallocate(h + 1);
    scratch.deallocate(h + 1);
    scratch.deallocate(h + 1);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
