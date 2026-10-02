// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/multiply_long_runtime.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/square_long_runtime.hpp>

// The runtime multiplication tier ladders, compiled once. The header
// dispatchers (multiply_dispatch / square_dispatch) keep the constexpr
// small-operand and constant-evaluation paths and forward every runtime
// multi-limb product here; kernel workspaces come from the type-erased heap
// hooks, so a single compiled definition serves every allocator.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

std::size_t square_runtime(const std::span<uint_multiprecision_t>       result,
                           const std::span<const uint_multiprecision_t> a,
                           const scratch_heap_source&                   heap) {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t n            = a.size();
    const std::size_t result_total = 2 * n;

    // Tiny squares: plain schoolbook beats the squaring basecase.
    if (n < square_long_cutoff) {
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            ::beman_big_int_multiply_long_runtime(
                result.first(result_total).data(), a.data(), a.size(), a.data(), a.size());
        } else {
            multiply_long(result.first(result_total), a, a);
        }
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    // (2^k)^2 = 2^(2k): a shifted copy beats any squaring kernel.
    if (is_power_of_two_span(a)) {
        return multiply_power_of_two(result, a, a);
    }

    if (n < square_karatsuba_cutoff) {
        ::beman_big_int_square_long_runtime(result.first(result_total).data(), a.data(), n);
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    // The FFT kernel packs into 64-bit words, so it is gated to 64-bit limbs (the branch is discarded otherwise).
    if constexpr (width_v<uint_multiprecision_t> == 64) {
        if (square_fft_worthwhile(n)) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
            scratch_heap_array<double>        fp_ws(heap, square_fft_fp_storage_size(n));
            scratch_heap_array<std::uint64_t> int_ws(heap, square_fft_int_storage_size(n));
            square_fft(result.first(result_total), a, fp_ws.span(), int_ws.span());
#else
            scratch_heap_array<std::uint64_t> ws(heap, square_fft_storage_size(n));
            square_fft(result.first(result_total), a, ws.span());
#endif
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
    }

    const auto in_heap_scratch = [&](const std::size_t limbs, auto&& kernel) {
        scratch_heap_array<uint_multiprecision_t> buf(heap, limbs);
        scratch_allocator_base                    scratch(buf.data(), limbs);
        kernel(scratch);
    };

    if (n < square_toom_cook_3_cutoff) {
        in_heap_scratch(karatsuba_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_karatsuba(result.first(result_total), a, scratch);
        });
    } else if (n < square_toom_cook_4_cutoff) {
        in_heap_scratch(toom_cook_3_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_toom_cook_3(result.first(result_total), a, scratch);
        });
    } else if (n < square_toom_cook_6_5_cutoff) {
        in_heap_scratch(toom_cook_4_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_toom_cook_4(result.first(result_total), a, scratch);
        });
    } else if (n < square_toom_cook_8_5_cutoff) {
        in_heap_scratch(toom_cook_6_5_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_toom_cook_6_5(result.first(result_total), a, scratch);
        });
    } else {
        in_heap_scratch(toom_cook_8_5_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_toom_cook_8_5(result.first(result_total), a, scratch);
        });
    }

    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
}

namespace {

enum class slice_mode { automatic, forced, disabled };

// Karatsuba over a stack buffer. Kept out of line so the buffer never sits in
// the frames of the dispatch and slicing recursion.
BEMAN_BIG_INT_NOINLINE void karatsuba_on_stack(const std::span<uint_multiprecision_t>       result,
                                               const std::span<const uint_multiprecision_t> a,
                                               const std::span<const uint_multiprecision_t> b) noexcept {
    uint_multiprecision_t  stack_buf[karatsuba_stack_threshold];
    scratch_allocator_base scratch(stack_buf, karatsuba_stack_threshold);
    multiply_karatsuba(result, a, b, scratch);
}

// Karatsuba / Toom-3 / Toom-4 / Toom-6.5 / Toom-8.5 chosen by the shorter
// operand. `result` is pre-zeroed with exactly a.size() + b.size() limbs and
// `scratch` holds toom_ladder_storage_size(min, max) limbs.
void toom_ladder(const std::span<uint_multiprecision_t>       result,
                 const std::span<const uint_multiprecision_t> a,
                 const std::span<const uint_multiprecision_t> b,
                 scratch_allocator_base&                      scratch) noexcept {
    const std::size_t min_size = std::min(a.size(), b.size());
    if (min_size < toom_cook_3_cutoff) {
        multiply_karatsuba(result, a, b, scratch);
    } else if (min_size < toom_cook_4_cutoff) {
        multiply_toom_cook_3(result, a, b, scratch);
    } else if (min_size < toom_cook_6_5_cutoff) {
        multiply_toom_cook_4(result, a, b, scratch);
    } else if (min_size < toom_cook_8_5_cutoff) {
        multiply_toom_cook_6_5(result, a, b, scratch);
    } else {
        multiply_toom_cook_8_5(result, a, b, scratch);
    }
}

void run_ladder(const std::span<uint_multiprecision_t>       result,
                const std::span<const uint_multiprecision_t> a,
                const std::span<const uint_multiprecision_t> b,
                const scratch_heap_source&                   heap) {
    const std::size_t min_size = std::min(a.size(), b.size());
    const std::size_t s        = std::max(a.size(), b.size());
    if (min_size < toom_cook_3_cutoff && karatsuba_storage_size(s) <= karatsuba_stack_threshold) {
        karatsuba_on_stack(result, a, b);
        return;
    }
    const std::size_t                         limbs = toom_ladder_storage_size(min_size, s);
    scratch_heap_array<uint_multiprecision_t> buf(heap, limbs);
    scratch_allocator_base                    scratch(buf.data(), limbs);
    toom_ladder(result, a, b, scratch);
}

std::size_t multiply_runtime_impl(std::span<uint_multiprecision_t>       result,
                                  std::span<const uint_multiprecision_t> a,
                                  std::span<const uint_multiprecision_t> b,
                                  const scratch_heap_source&             heap,
                                  slice_mode                             mode);

// Euclidean slicing: cuts the longer operand into pieces of the shorter one's
// length m and accumulates piece * short at each offset. In automatic mode
// pieces are cut while the remainder is at least 2m or still enters slicing
// (mul_should_slice), so the last piece is below 2m for every ratio; the
// forced mode cuts until at most m remains. Full pieces (at least m limbs) run
// the FFT when the cost-model gate takes the m x piece product (automatic mode
// only; 64-bit limbs) and the Karatsuba/Toom ladder otherwise, over scratch
// allocated up front (the FFT workspace on first use, sized for the largest
// piece); a real
// tail or a piece that trims below m re-dispatches through multiply_runtime_any
// (it may slice the other way or take the FFT) and allocates its own scratch,
// possibly after piece 0 has been written. On x86 (model off, gate = floor on
// min) slicing is only entered when min = m is below the floor, so no piece
// takes the FFT and this path is a no-op there. Callers pre-zero the result and
// discard it if an allocation throws. Requires trimmed operands,
// min >= karatsuba_cutoff and a pre-zeroed result.
std::size_t multiply_sliced(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a,
                            const std::span<const uint_multiprecision_t> b,
                            const scratch_heap_source&                   heap,
                            const bool                                   force) {
    const auto        lng = a.size() >= b.size() ? a : b;
    const auto        sht = a.size() >= b.size() ? b : a;
    const std::size_t m   = sht.size();
    const std::size_t n   = lng.size();

    std::size_t pieces_before_last = 0;
    std::size_t rem                = n;
    while (force ? rem > m : (rem >= 2 * m || mul_should_slice(m, rem))) {
        ++pieces_before_last;
        rem -= m;
    }

    const std::size_t s_max    = std::max(m, rem);
    const std::size_t tmp_size = m + s_max;
    const std::size_t ws_size  = toom_ladder_storage_size(m, s_max);

    scratch_heap_array<uint_multiprecision_t> buf(heap, tmp_size + ws_size);
    const std::span<uint_multiprecision_t>    tmp(buf.data(), tmp_size);
#if defined(BEMAN_BIG_INT_SIMD_MUL)
    [[maybe_unused]] std::optional<scratch_heap_array<double>>        fft_fp_ws;
    [[maybe_unused]] std::optional<scratch_heap_array<std::uint64_t>> fft_int_ws;
#else
    [[maybe_unused]] std::optional<scratch_heap_array<std::uint64_t>> fft_ws;
#endif

    for (std::size_t i = 0; i <= pieces_before_last; ++i) {
        const std::size_t off   = i * m;
        const std::size_t len   = (i < pieces_before_last) ? m : rem;
        const auto        piece = lng.subspan(off, len);
        const std::size_t pn    = trimmed_size_span(piece);
        if (pn == 1 && piece[0] == 0) {
            continue;
        }
        const auto p   = piece.first(pn);
        const auto dst = (i == 0 ? result : tmp).first(pn + m);
        if (i != 0) {
            std::ranges::fill(dst, uint_multiprecision_t{0});
        }

        if (pn < m) {
            multiply_runtime_any(dst, p, sht, heap);
        } else {
            bool piece_done = false;
            // Full pieces (m x pn, pn >= m) consult the FFT gate first, except in the forced mode.
            if constexpr (width_v<uint_multiprecision_t> == 64) {
                if (!force && fft_mul_worthwhile(m, pn)) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                    if (!fft_fp_ws) {
                        fft_fp_ws.emplace(heap, fft_mul_fp_storage_size(m, s_max));
                        fft_int_ws.emplace(heap, fft_mul_int_storage_size(m, s_max));
                    }
                    multiply_fft(dst, p, sht, fft_fp_ws->span(), fft_int_ws->span());
#else
                    if (!fft_ws) {
                        fft_ws.emplace(heap, fft_mul_storage_size(m, s_max));
                    }
                    multiply_fft(dst, p, sht, fft_ws->span());
#endif
                    piece_done = true;
                }
            }
            if (!piece_done) {
                scratch_allocator_base ws(buf.data() + tmp_size, ws_size);
                toom_ladder(dst, p, sht, ws);
            }
        }

        if (i != 0) {
            add_shifted(result.first(off + len + m), off, dst);
        }
    }
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), n + m});
}

std::size_t multiply_runtime_impl(const std::span<uint_multiprecision_t>       result,
                                  const std::span<const uint_multiprecision_t> a,
                                  const std::span<const uint_multiprecision_t> b,
                                  const scratch_heap_source&                   heap,
                                  const slice_mode                             mode) {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());

    // x * x and x *= x pass the same span twice, so squaring detection is
    // a pointer compare that almost always fails fast for ordinary mul.
    if (a.data() == b.data() && a.size() == b.size()) {
        return square_runtime(result, a, heap);
    }

    const std::size_t min_size     = std::min(a.size(), b.size());
    const std::size_t max_size     = std::max(a.size(), b.size());
    const std::size_t result_total = a.size() + b.size();

    if (min_size < karatsuba_cutoff) {
        // Schoolbook long multiplication runtime fallback (known to be on the runtime path).
        ::beman_big_int_multiply_long_runtime(result.data(), a.data(), a.size(), b.data(), b.size());
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    // Power-of-two operands reduce to a shifted copy of the other operand.
    // This is only worth checking if we're about to do a big number mul anyway.
    if (is_power_of_two_span(b)) {
        return multiply_power_of_two(result, a, b);
    }
    if (is_power_of_two_span(a)) {
        return multiply_power_of_two(result, b, a);
    }

    // The FFT kernel packs into 64-bit words, so it is gated to 64-bit limbs.
    if constexpr (width_v<uint_multiprecision_t> == 64) {
        const bool use_fft = mode != slice_mode::forced && fft_mul_worthwhile(min_size, max_size);
        if (use_fft) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
            scratch_heap_array<double>        fp_ws(heap, fft_mul_fp_storage_size(a.size(), b.size()));
            scratch_heap_array<std::uint64_t> int_ws(heap, fft_mul_int_storage_size(a.size(), b.size()));
            multiply_fft(result.first(result_total), a, b, fp_ws.span(), int_ws.span());
#else
            scratch_heap_array<std::uint64_t> ws(heap, fft_mul_storage_size(a.size(), b.size()));
            multiply_fft(result.first(result_total), a, b, ws.span());
#endif
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
    }

    const bool slice = mode == slice_mode::forced      ? max_size > min_size
                       : mode == slice_mode::automatic ? mul_should_slice(min_size, max_size)
                                                       : false;
    if (slice) {
        return multiply_sliced(result, a, b, heap, mode == slice_mode::forced);
    }

    run_ladder(result.first(result_total), a, b, heap);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
}

} // namespace

std::size_t multiply_runtime(const std::span<uint_multiprecision_t>       result,
                             const std::span<const uint_multiprecision_t> a,
                             const std::span<const uint_multiprecision_t> b,
                             const scratch_heap_source&                   heap) {
    return multiply_runtime_impl(result, a, b, heap, slice_mode::automatic);
}

std::size_t multiply_runtime_sliced(const std::span<uint_multiprecision_t>       result,
                                    const std::span<const uint_multiprecision_t> a,
                                    const std::span<const uint_multiprecision_t> b,
                                    const scratch_heap_source&                   heap) {
    return multiply_runtime_impl(result, a, b, heap, slice_mode::forced);
}

std::size_t multiply_runtime_unsliced(const std::span<uint_multiprecision_t>       result,
                                      const std::span<const uint_multiprecision_t> a,
                                      const std::span<const uint_multiprecision_t> b,
                                      const scratch_heap_source&                   heap) {
    return multiply_runtime_impl(result, a, b, heap, slice_mode::disabled);
}

std::size_t multiply_runtime_any(const std::span<uint_multiprecision_t>       result,
                                 const std::span<const uint_multiprecision_t> a_untrimmed,
                                 const std::span<const uint_multiprecision_t> b_untrimmed,
                                 const scratch_heap_source&                   heap) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_untrimmed.size() + b_untrimmed.size());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    if (a.size() == 1 && b.size() == 1) {
        const auto [lo, hi] = widening_mul(a[0], b[0]);
        result[0]           = lo;
        result[1]           = hi;
        return hi != 0 ? 2 : 1;
    }
    if (a.size() == 1) {
        return multiply_single_limb(result, b, a[0]);
    }
    if (b.size() == 1) {
        return multiply_single_limb(result, a, b[0]);
    }

    return multiply_runtime(result, a, b, heap);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
