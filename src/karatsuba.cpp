// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/multiply_long_runtime.hpp>
#include <beman/big_int/detail/square_long_runtime.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Squares below square_karatsuba_cutoff (and the squaring leaves below) stay
// inside the IFMA square kernel's native range.
#if defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
static_assert(square_karatsuba_cutoff - 1 <= ifma_square_native_max_limbs,
              "square_karatsuba_cutoff must not exceed the IFMA square kernel's native range");
#endif

void multiply_karatsuba(const std::span<uint_multiprecision_t>       result,
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

    const std::size_t effective_fallback = cutoff_override == 0 ? karatsuba_fallback : cutoff_override;

    // First, check if we have enough limbs to justify karatsuba.
    // If not, simply utilize runtime schoolbook long multiplication.
    // The runtime subroutine is selected because karatsuba is
    // exclusively on the runtime path by design.
    if (a.size() < effective_fallback || b.size() < effective_fallback) {
        ::beman_big_int_multiply_long_runtime(
            result.first(a.size() + b.size()).data(), a.data(), a.size(), b.data(), b.size());
        return;
    }

    // Partition at n = max(a.size(), b.size()) / 2 + 1
    const std::size_t n = std::max(a.size(), b.size()) / 2 + 1;

    // Split: a = a_h * B^n + a_l,  b = b_h * B^n + b_l
    // where B = 2^bits_per_limb. When a or b is shorter than n limbs the high
    // half is the empty span (treated as zero by the span ops below).
    const auto a_l = a.first(std::min(a.size(), n));
    const auto a_h = a.size() > n ? a.subspan(n) : a.last(0);

    const auto b_l = b.first(std::min(b.size(), n));
    const auto b_h = b.size() > n ? b.subspan(n) : b.last(0);

    // Allocate all temporaries in a single bump from scratch, then carve
    // sub-spans for each:
    //   t1: holds (a_h + a_l) * (b_h + b_l), needs up to 2*n + 2 limbs
    //   t2: holds a_h + a_l, needs up to n + 1 limbs
    //   t3: holds b_h + b_l, needs up to n + 1 limbs
    const std::size_t t1_cap        = 2 * n + 2;
    const std::size_t t2_cap        = n + 1;
    const std::size_t t3_cap        = n + 1;
    const std::size_t total_scratch = t1_cap + t2_cap + t3_cap;

    auto scratch_block = scratch.allocate(total_scratch);
    auto t1            = scratch_block.first(t1_cap);
    auto t2            = scratch_block.subspan(t1_cap, t2_cap);
    auto t3            = scratch_block.subspan(t1_cap + t2_cap, t3_cap);

    // result layout:
    //
    // result[0, 2*n) = result_low (will hold a_l * b_l)
    // result[2*n, result.size()) = result_high (will hold a_h * b_h)
    auto result_low  = result.first(2 * n);
    auto result_high = result.size() > 2 * n ? result.subspan(2 * n) : std::span<uint_multiprecision_t>{};

    // Compute result_low = a_l * b_l
    multiply_karatsuba(result_low, a_l, b_l, scratch);
    const std::size_t result_low_size =
        trimmed_size_span(std::span<const uint_multiprecision_t>{result_low.data(), a_l.size() + b_l.size()});

    // Zero unused limbs in result_low region
    std::ranges::fill(result_low.subspan(result_low_size), uint_multiprecision_t{0});

    // Compute result_high = a_h * b_h
    if (!result_high.empty()) {
        if ((a.size() > n) && (b.size() > n)) {
            multiply_karatsuba(result_high, a_h, b_h, scratch);

            const std::size_t result_high_size =
                trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});

            // Zero unused limbs in result_high region
            std::ranges::fill(result_high.subspan(result_high_size), uint_multiprecision_t{0});
        } else {
            result_high = std::span<uint_multiprecision_t>{};
        }
    }

    // Compute t2 = a_h + a_l
    std::size_t t2_size = std::max(a_h.size(), a_l.size());
    if (add_unsigned_spans(t2.first(t2_size), a_l, a_h)) {
        t2[t2_size] = 1;
        ++t2_size;
    }

    // Compute t3 = b_h + b_l
    std::size_t t3_size = std::max(b_h.size(), b_l.size());
    if (add_unsigned_spans(t3.first(t3_size), b_l, b_h)) {
        t3[t3_size] = 1;
        ++t3_size;
    }

    // Compute t1 = t2 * t3 = (a_h + a_l) * (b_h + b_l)
    std::ranges::fill(t1, uint_multiprecision_t{0});
    const auto t2_span = std::span<const uint_multiprecision_t>{t2.data(), t2_size};
    const auto t3_span = std::span<const uint_multiprecision_t>{t3.data(), t3_size};
    multiply_karatsuba(t1, t2_span, t3_span, scratch);
    std::size_t t1_size = trimmed_size_span(std::span<const uint_multiprecision_t>{t1.data(), t2_size + t3_size});

    // t1 -= result_high (a_h * b_h)
    if (!result_high.empty()) {
        const std::size_t rh_size =
            trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});
        t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                          std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                          std::span<const uint_multiprecision_t>{result_high.data(), rh_size});
    }

    // t1 -= result_low (a_l * b_l)
    t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                      std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                      std::span<const uint_multiprecision_t>{result_low.data(), result_low_size});

    // Add t1 shifted left by n limbs into result: result[n...] += t1
    add_shifted(result, n, std::span<const uint_multiprecision_t>{t1.data(), t1_size});

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    // No actual deallocation happens,
    // this is pointer arithmetic within a single pre-allocated buffer.
    scratch.deallocate(total_scratch);
}

void square_karatsuba(const std::span<uint_multiprecision_t>       result,
                      const std::span<const uint_multiprecision_t> a_untrimmed,
                      scratch_allocator_base&                      scratch,
                      const std::size_t                            cutoff_override) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));

    const std::size_t effective_cutoff = cutoff_override == 0 ? square_karatsuba_cutoff : cutoff_override;

    // Below the cutoff the schoolbook squaring basecase wins. It writes its
    // whole 2 * a.size() window, so nothing needs zeroing first.
    if (a.size() < effective_cutoff) {
        ::beman_big_int_square_long_runtime(result.first(2 * a.size()).data(), a.data(), a.size());
        return;
    }

    // Partition at n = a.size() / 2 + 1; the cutoff guarantees a_h is non-empty.
    const std::size_t n = a.size() / 2 + 1;

    const auto a_l = a.first(n);
    const auto a_h = a.subspan(n);

    // Scratch: one evaluation buffer (the general kernel needs two):
    //   t1: holds (a_h + a_l)^2, needs up to 2*n + 2 limbs
    //   t2: holds a_h + a_l, needs up to n + 1 limbs
    const std::size_t t1_cap        = 2 * n + 2;
    const std::size_t t2_cap        = n + 1;
    const std::size_t total_scratch = t1_cap + t2_cap;

    auto scratch_block = scratch.allocate(total_scratch);
    auto t1            = scratch_block.first(t1_cap);
    auto t2            = scratch_block.subspan(t1_cap, t2_cap);

    auto result_low  = result.first(2 * n);
    auto result_high = result.subspan(2 * n);

    // result_low = a_l^2
    square_karatsuba(result_low, a_l, scratch);
    const std::size_t result_low_size =
        trimmed_size_span(std::span<const uint_multiprecision_t>{result_low.data(), 2 * a_l.size()});
    std::ranges::fill(result_low.subspan(result_low_size), uint_multiprecision_t{0});

    // result_high = a_h^2
    square_karatsuba(result_high, a_h, scratch);
    const std::size_t result_high_size =
        trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), 2 * a_h.size()});
    std::ranges::fill(result_high.subspan(result_high_size), uint_multiprecision_t{0});

    // t2 = a_h + a_l
    std::size_t t2_size = std::max(a_h.size(), a_l.size());
    if (add_unsigned_spans(t2.first(t2_size), a_l, a_h)) {
        t2[t2_size] = 1;
        ++t2_size;
    }

    // t1 = (a_h + a_l)^2
    std::ranges::fill(t1, uint_multiprecision_t{0});
    square_karatsuba(t1, std::span<const uint_multiprecision_t>{t2.data(), t2_size}, scratch);
    std::size_t t1_size = trimmed_size_span(std::span<const uint_multiprecision_t>{t1.data(), 2 * t2_size});

    // t1 -= a_h^2; t1 -= a_l^2, leaving the doubled cross term 2*a_l*a_h.
    t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                      std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                      std::span<const uint_multiprecision_t>{result_high.data(), result_high_size});
    t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                      std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                      std::span<const uint_multiprecision_t>{result_low.data(), result_low_size});

    // Add t1 shifted left by n limbs into result: result[n...] += t1
    add_shifted(result, n, std::span<const uint_multiprecision_t>{t1.data(), t1_size});

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    scratch.deallocate(total_scratch);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
