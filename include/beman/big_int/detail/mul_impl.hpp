// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MUL_IMPL_HPP
#define BEMAN_BIG_INT_MUL_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <array>
    #include <bit>
    #include <compare>
    #include <limits>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <vector>
#endif

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// Long (classical) O(n*m) multiplication. Writes exactly `a.size() + b.size()`
// limbs into `result.first(a.size() + b.size())`; limbs beyond that are
// untouched. `result` need NOT be pre-zeroed.
// `result` must NOT alias `a` or `b`. Both `a` and `b` must be non-empty.
// ---------------------------------------------------------------------------
constexpr void multiply_long(const std::span<uint_multiprecision_t>       result,
                             const std::span<const uint_multiprecision_t> a,
                             const std::span<const uint_multiprecision_t> b) noexcept {

    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());

    // The key invariant from Boost is:
    //   double_limb_max - 2 * limb_max >= limb_max * limb_max
    // This means that: widening_mul(a[i], b[j]).high + carry + bool_carry
    // can never overflow a single limb, so we only need a single-limb carry.

    uint_multiprecision_t carry = 0;

    // First row (i=0): write directly into result without reading. This avoids
    // the pre-zero precondition that the accumulating path below would need.
    {
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto [lo, hi] = widening_mul(a[0], b[j]);
            const auto [s, c]   = carrying_add(lo, carry);
            result[j]           = s;
            carry               = hi + static_cast<uint_multiprecision_t>(c);
        }
        result[b.size()] = carry;
    }

    // Subsequent rows: accumulate onto values written by previous rows.
    for (std::size_t i = 1; i < a.size(); ++i) {
        carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto [lo, hi] = widening_mul(a[i], b[j]);
            const auto [s1, c1] = carrying_add(lo, result[i + j]);
            const auto [s2, c2] = carrying_add(s1, carry);
            result[i + j]       = s2;
            carry               = hi + static_cast<uint_multiprecision_t>(c1) + static_cast<uint_multiprecision_t>(c2);
        }
        result[i + b.size()] = carry;
    }
}

// ---------------------------------------------------------------------------
// Schoolbook squaring: result <- a * a (HAC algorithm 14.16). Computes the
// off-diagonal upper triangle once (n*(n-1)/2 widening muls), doubles it with
// a one-bit shift, then folds in the n diagonal squares: roughly half the
// widening muls of multiply_long. Unlike multiply_long, `result` MUST be
// pre-zeroed and have space for 2 * a.size() limbs. `result` must NOT alias `a`.
// ---------------------------------------------------------------------------
constexpr void square_long(const std::span<uint_multiprecision_t>       result,
                           const std::span<const uint_multiprecision_t> a) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t n = a.size();

    // Off-diagonal upper triangle: result += sum_{i<j} a[i]*a[j] * B^(i+j).
    for (std::size_t i = 0; i + 1 < n; ++i) {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = i + 1; j < n; ++j) {
            const auto [lo, hi] = widening_mul(a[i], a[j]);
            const auto [s1, c1] = carrying_add(lo, result[i + j]);
            const auto [s2, c2] = carrying_add(s1, carry);
            result[i + j]       = s2;
            carry               = hi + static_cast<uint_multiprecision_t>(c1) + static_cast<uint_multiprecision_t>(c2);
        }
        result[i + n] = carry;
    }

    // Double the triangle; cannot overflow 2n limbs since a*a < B^(2n).
    [[maybe_unused]] const auto doubled_size = shift_left_n(result.first(2 * n), 2 * n, 1u);
    BEMAN_BIG_INT_DEBUG_ASSERT(doubled_size == 2 * n);

    // Diagonal: result += sum a[i]^2 * B^(2i). The carry out of slot 2i+1
    // lands in slot 2i+2, the next iteration's low slot, so one flag suffices.
    bool carry_flag = false;
    for (std::size_t i = 0; i < n; ++i) {
        const auto [lo, hi] = widening_mul(a[i], a[i]);
        const auto [s1, c1] = carrying_add(result[2 * i], lo, carry_flag);
        result[2 * i]       = s1;
        const auto [s2, c2] = carrying_add(result[2 * i + 1], hi, c1);
        result[2 * i + 1]   = s2;
        carry_flag          = c2;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(!carry_flag);
}

// Below this limb count square_runtime routes squares back to plain schoolbook
// multiplication: the squaring basecase's doubling pass outweighs the few cross
// products it saves. Tuned end to end on x * x, where the x86_64 assembly
// basecase wins from 5 limbs and the portable one from 4 (kernel-only timings
// flatter the assembly down to 2 limbs, which the full dispatch does not bear out).
#if defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t square_long_cutoff = 10;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t square_long_cutoff = 5;
#elif defined(BEMAN_BIG_INT_ARCH_AARCH64)
// x * x, n = 2..14: at n = 4 the multiply kernel is 9.5% faster than the square kernel, from 5 on the square
// kernel wins or ties. M4 Max, appleclang-release, 2026-09-30, end to end (shape_sweep sqr); measured on M4 only,
// also used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t square_long_cutoff = 5;
#else
inline constexpr std::size_t square_long_cutoff = 4;
#endif

// Minimum number of limbs for Karatsuba to be worthwhile
// Directly from Boost, and reconfirmed as correct on x86_64 and the portable kernel.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// x * y, min 84..132 in one-limb steps: 104-112 tie with the 2-row AArch64 schoolbook kernel and karatsuba_fallback
// 80. M4 Max, appleclang-release, 2026-09-30, end to end (shape_sweep), balanced and unbalanced ratios; measured on M4
// only, also used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t karatsuba_cutoff = 112;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t karatsuba_cutoff = 260;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t karatsuba_cutoff = 47;
#else
inline constexpr std::size_t karatsuba_cutoff = 48;
#endif
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// Leaf size of the Karatsuba recursion: min 96..399 x ratios 1..4, leaves 40..144; 72-112 tie, 80 is 5.7% faster than
// 40 on average. M4 Max, appleclang-release, 2026-09-30, end to end (shape_sweep), balanced and unbalanced ratios;
// measured on M4 only, also used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t karatsuba_fallback = 80;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t karatsuba_fallback = 230;
#else
inline constexpr std::size_t karatsuba_fallback = 40;
#endif

// Heuristic estimate of scratch space needed for Karatsuba multiplication.
// One Karatsuba level uses ~2*s limbs (t1=2n+2, t2=t3=n+1 with n=s/2+1). The
// geometric sum over self-recursion converges to 4*s as the asymptotic worst
// case; empirically (probed via scratch_allocator high-water marks on sizes
// 40-4000 limbs in scratch_peak_bench) the actual peak/s ratio tops out at
// ~3.997. 5*s leaves ~25% safety margin and matches the same generous-but-not-
// wasteful ratio used by the Toom-Cook variants.
static_assert(karatsuba_fallback >= 5,
              "karatsuba_fallback < 5 makes the Karatsuba recursion non-terminating (stack overflow)");

constexpr std::size_t karatsuba_storage_size(const std::size_t s) noexcept { return 5 * s; }

// Maximum number of scratch limbs we are willing to place on the stack
// Value from boost
inline constexpr std::size_t karatsuba_stack_threshold = 300;

// ---------------------------------------------------------------------------
// Recursive Karatsuba multiplication.
// Port of Boost.Multiprecision multiply_karatsuba (lines 98-215).
//
// `result` must have space for a.size() + b.size() limbs or more.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `karatsuba_fallback` for this call only (forcing a split at smaller sizes);
// recursive sub-products always use the default. Production callers omit it.
void multiply_karatsuba(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        const std::span<const uint_multiprecision_t> b_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Karatsuba squaring variant: the squaring
// basecase stays ahead of recursion roughly twice as long as schoolbook does
// against general Karatsuba (the same SQR/MUL threshold ratio GMP observes).
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// x * x, n = 64..400: a Karatsuba split wins from about 170 limbs with the 2-row AArch64 squaring kernel (160-176
// tie). M4 Max, appleclang-release, 2026-09-30, end to end (shape_sweep sqr); measured on M4 only, also used for
// other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t square_karatsuba_cutoff = 168;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t square_karatsuba_cutoff = 257;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t square_karatsuba_cutoff = 120;
#else
inline constexpr std::size_t square_karatsuba_cutoff = 72;
#endif

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_karatsuba: one evaluation (a_h + a_l) per
// level instead of two, and all three sub-products are recursive squares.
// `result` must have space for 2 * a.size() limbs or more; slack beyond the
// product follows the same caller-pre-zeroed convention as the general kernel.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// ---------------------------------------------------------------------------
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it
// replaces `square_karatsuba_cutoff` for this call only; recursive
// sub-squares always use the default.
void square_karatsuba(const std::span<uint_multiprecision_t>       result,
                      const std::span<const uint_multiprecision_t> a_untrimmed,
                      scratch_allocator_base&                      scratch,
                      const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for Toom-Cook 3 to be worthwhile. x86-64 and portable
// provenance (AArch64 is tuned separately below): Karatsuba still wins at
// 300-350 limbs (~15%); Toom-3 reliably overtakes from ~400.
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// min 300..399 (ratios 1..1.75): the leaf-80 Karatsuba loses to Toom-3 from 300 (275-250 tie). M4 Max,
// appleclang-release, 2026-09-30, end to end (shape_sweep), balanced and unbalanced ratios; measured on M4 only, also
// used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t toom_cook_3_cutoff = 300;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t toom_cook_3_cutoff = 1600;
#else
inline constexpr std::size_t toom_cook_3_cutoff = 400;
#endif

// Heuristic estimate of scratch space needed for Toom-Cook 3 multiplication.
// One Toom-3 level uses 8k+10 limbs (~2.67*s where k = ceil(s/3)). The
// geometric sum over self-recursion converges to (8/3)*(3/2)*s = 4*s as the
// asymptotic worst case; empirically (probed via scratch_allocator high-water
// marks on sizes 550-80000 limbs in scratch_peak_bench) the actual peak/s
// ratio tops out at ~4.0016. 5*s leaves ~25% safety margin and matches the
// same generous-but-not-wasteful ratio used by Karatsuba and Toom-4.
constexpr std::size_t toom_cook_3_storage_size(const std::size_t s) noexcept { return 5 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 3-Way multiplication (Bodrato variant).
// Reference: Knuth TAOCP section 4.3.3
// Reference: Bodrato, "Towards Optimal Toom-Cook Multiplication" (2006)
//
// Splits each operand into three pieces of size k = ceil(max(an,bn)/3):
//   a = a2*B^(2k) + a1*B^k + a0,   b = b2*B^(2k) + b1*B^k + b0  (B = 2^limb_bits)
//
// Evaluates the product polynomial r(x) = p(x)*q(x) at five points
// {0, 1, -1, 2, infinity}, then interpolates the five coefficients c0-c4 of
// r(x) via Bodrato's seven-step in-place sequence (one /3, two /2).
// Result = c0 + c1*B^k + c2*B^(2k) + c3*B^(3k) + c4*B^(4k).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_3_cutoff` for this call only; recursive sub-products always use the
// default. Production callers omit it.
// ---------------------------------------------------------------------------
void multiply_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          const std::span<const uint_multiprecision_t> b_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-Cook 3 squaring variant; roughly twice
// the general toom_cook_3_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// x * x, n = 300..599: 500 scores 1.0069 against 1.0322 for the old 300. M4 Max, appleclang-release, 2026-09-30, end
// to end (shape_sweep sqr); measured on M4 only, also used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t square_toom_cook_3_cutoff = 500;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t square_toom_cook_3_cutoff =
    4200; // squares: i9-11900K, gcc-release, 2026-09-30, shape_sweep end to end, balanced and unbalanced ratios.
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t square_toom_cook_3_cutoff = 440; // same provenance
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t square_toom_cook_3_cutoff = 300; // same provenance
#else
inline constexpr std::size_t square_toom_cook_3_cutoff = 300;
#endif

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_3: one evaluation per point
// instead of two, all five products are recursive squares, and p(-1)^2 >= 0
// removes the sign handling from interpolation. Falls back to
// square_karatsuba below the cutoff.
// `result` must be pre-zeroed and have space for 2 * a.size() limbs.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// ---------------------------------------------------------------------------
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it
// replaces `square_toom_cook_3_cutoff` for this call only; recursive
// sub-squares always use the default.
void square_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for Toom-Cook 4 to be worthwhile. x86-64 and portable
// provenance (AArch64 is tuned separately below): Toom-3 still wins at 1400
// (~9%); Toom-4 reliably overtakes from ~1600.
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// Toom-4 and Toom-6.5 share one cutoff (the Toom-4 zone is empty in the ladder; Toom-4 is still reached through
// Toom-6.5's own ratio fallback): Toom-3 to Toom-6.5 crossover scan 900..1700, 1400 has no balanced regression.
// M4 Max, appleclang-release, 2026-09-30, end to end (shape_sweep), balanced and unbalanced ratios; measured on M4
// only, also used for other AArch64 cores and MSVC ARM64.
inline constexpr std::size_t toom_cook_4_cutoff = 1400;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t toom_cook_4_cutoff =
    4000; // i9-11900K, gcc-release, 2026-09-30, shape_sweep end to end, balanced and unbalanced ratios.
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t toom_cook_4_cutoff = 1600; // same provenance
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
// Generic x86-64: Toom-4, 6.5 and 8.5 share 1400, so the ladder is Toom-3 up to 1400 and Toom-8.5 above
// (the other Toom orders added nothing). Same provenance.
inline constexpr std::size_t toom_cook_4_cutoff = 1400;
#else
inline constexpr std::size_t toom_cook_4_cutoff = 1600;
#endif

// Heuristic estimate of scratch space needed for Toom-Cook 4 multiplication.
// One Toom-4 level uses 14k+16 limbs (~3.5*s where k = ceil(s/4)). The geometric
// sum over self-recursion converges to (14/3)*s ~= 4.67*s as the asymptotic
// worst case; empirically (probed via scratch_allocator high-water marks on
// sizes 1400-80000 limbs in scratch_peak_bench) the actual peak/s ratio ranges
// 4.46-4.66 (climbing monotonically toward the asymptote). 6*s leaves ~28%
// safety margin at the worst observed point and matches the same generous-but-
// not-wasteful ratio used by the smaller-radix algorithms.
constexpr std::size_t toom_cook_4_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 4-Way multiplication (Bodrato variant).
// Reference: Bodrato, "Towards Optimal Toom-Cook Multiplication" (2006).
//
// Splits each operand into four pieces of size k = ceil(max(an,bn)/4):
//   a = a3*B^(3k) + a2*B^(2k) + a1*B^k + a0,  b = b3*B^(3k) + ... (B = 2^limb_bits)
//
// Evaluates the product polynomial r(x) = p(x)*q(x) at seven points
// {0, 1, -1, 2, -2, 1/2 (scaled by 8), infinity}, then interpolates the seven
// coefficients c0..c6 of r(x) using a Bodrato-style in-place sequence with
// exact divisions by 2, 3, and 5.
// Result = c0 + c1*B^k + c2*B^(2k) + c3*B^(3k) + c4*B^(4k) + c5*B^(5k) + c6*B^(6k).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
//
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_4_cutoff` for this call only. Production callers should omit it so
// the default cutoff applies. Recursive sub-product calls always use the default.
// ---------------------------------------------------------------------------
void multiply_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          const std::span<const uint_multiprecision_t> b_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-Cook 4 squaring variant; roughly twice
// the general toom_cook_4_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t square_toom_cook_4_cutoff = 2000; // kept; ties within 1% (see toom_cook_4_cutoff)
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t square_toom_cook_4_cutoff = 6000; // same provenance as square_toom_cook_3_cutoff
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t square_toom_cook_4_cutoff = 2000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t square_toom_cook_4_cutoff = 1500; // generic squares: Toom-3 up to 1500, then Toom-8.5
#else
inline constexpr std::size_t square_toom_cook_4_cutoff = 2000;
#endif

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_4: one evaluation per point
// instead of two, all seven products are recursive squares, and the squared
// evaluations at -1 and -2 are non-negative, removing the sign handling from
// interpolation. Falls back to square_toom_cook_3 below the cutoff.
// `result` must be pre-zeroed and have space for 2 * a.size() limbs.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// `cutoff_override` is a benchmark-only escape hatch as in the general kernel.
// ---------------------------------------------------------------------------
void square_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override = 0) noexcept;

// x86-64 and portable provenance (AArch64 is tuned separately below): Toom-6.5
// overtakes Toom-4 cleanly and monotonically from ~2400 limbs (re-measured
// 2026-06-04; the old 3000 left a ~2400-3000 band on the slower Toom-4).
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t toom_cook_6_5_cutoff = 1400; // see toom_cook_4_cutoff
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t toom_cook_6_5_cutoff = 4000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t toom_cook_6_5_cutoff = 1800; // see toom_cook_4_cutoff
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t toom_cook_6_5_cutoff = 1400; // see toom_cook_4_cutoff
#else
inline constexpr std::size_t toom_cook_6_5_cutoff = 2400;
#endif

// Heuristic estimate of scratch space needed for Toom-Cook 6.5 multiplication.
// One Toom-6.5 level uses 24k+26 limbs (~4*s where k = ceil(min/6)) for ten
// scratch products + two evaluation buffers + one tmp_double. The recursive
// child enters Toom-Cook 4 on pieces of size ~s/6, contributing roughly
// (Toom-4 asymptote)/6 ~= 4.67/6 ~= 0.78*s; the combined asymptote is
// ~4.78*s. Empirically (probed via scratch_allocator high-water marks on
// sizes 3000-80000 limbs in scratch_peak_bench) the actual peak/s ratio
// ranges 4.65-4.79 (climbing monotonically toward the asymptote). 6*s leaves
// ~25% safety margin at the worst observed point and matches the same
// generous-but-not-wasteful ratio used by the smaller-radix algorithms.
constexpr std::size_t toom_cook_6_5_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 6.5 ("Toom 6'n'half") multiplication (Bodrato variant).
// Reference: Bodrato, "High degree Toom'n'half for balanced and unbalanced
//            multiplication" (ARITH-20, 2011).
//
// Asymmetric split: the smaller operand is partitioned into 6 pieces (a, degree
// 5 polynomial) and the larger into up to 7 pieces (b, degree 6 polynomial)
// using a common piece size k = ceil(min(an, bn) / 6). For balanced operands
// b6 is empty and c11 = 0; for asymmetric operands with size ratio up to 7:6
// b6 is non-empty.
//
//   p(x) = a0 + a1*x + a2*x^2 + a3*x^3 + a4*x^4 + a5*x^5            (degree 5)
//   q(x) = b0 + b1*x + b2*x^2 + b3*x^3 + b4*x^4 + b5*x^5 + b6*x^6   (degree <= 6)
//   r(x) = p(x)*q(x) = c0 + c1*x + ... + c11*x^11                   (degree 11)
//
// Evaluates r at 12 points {0, +-1, +-2, +-4, +-1/2, +-1/4, +infinity}, then
// solves two 6x6 linear systems (one for even-index coefficients, one for odd)
// to recover c0..c11. c0 lives in result[0..2k); c11 (if non-zero) lives in
// result[11k..); the remaining ten coefficients are added back into result via
// add_shifted.
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
//
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_6_5_cutoff` for this call only. Production callers should omit it
// so the default cutoff applies. Recursive sub-product calls always use the
// default.
// ---------------------------------------------------------------------------
void multiply_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a_untrimmed,
                            const std::span<const uint_multiprecision_t> b_untrimmed,
                            scratch_allocator_base&                      scratch,
                            const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-6.5 squaring variant; roughly twice
// the general toom_cook_6_5_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 2400; // kept; ties within 1% (see toom_cook_4_cutoff)
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 9000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 2000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 1500;
#else
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 2400;
#endif

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_6_5. Squaring is always balanced,
// so the operand splits into exactly six pieces (b6 of the general kernel is
// empty and c11 = 0). One evaluation per point instead of two; all eleven
// products are recursive squares. The squared fractional-point evaluations
// come out scaled by 2^10 / 4^10 instead of the 2^11 / 4^11 the interpolation
// expects (the general kernel pairs a 6-piece and a 7-piece evaluation), so
// vh/vmh are shifted left once and vq/vmq twice after squaring.
// Falls back to square_toom_cook_4 below the cutoff.
// `result` must be pre-zeroed and have space for 2 * a.size() limbs.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// `cutoff_override` is a benchmark-only escape hatch as in the general kernel.
// ---------------------------------------------------------------------------
void square_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for Toom-8.5 to be worthwhile. Measured via
// multiplication_stress_bench (two runs, AppleClang): below ~15000 Toom-6.5
// ties or wins; from 15000 Toom-8.5 overtakes cleanly and monotonically, and
// decisively (~5-8%) beyond ~24000.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t toom_cook_8_5_cutoff =
    15000; // kept; rarely reached on AArch64 (only where the FFT model refuses), not retuned
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t toom_cook_8_5_cutoff = 6000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t toom_cook_8_5_cutoff = 7000; // see toom_cook_4_cutoff
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t toom_cook_8_5_cutoff = 1400; // see toom_cook_4_cutoff
#else
inline constexpr std::size_t toom_cook_8_5_cutoff = 15000;
#endif

// Heuristic estimate of scratch space needed for Toom-Cook 8.5 multiplication.
// One Toom-8.5 level uses 32k+34 limbs (~4*s where k = ceil(min/8)) for fourteen
// scratch products + two evaluation buffers + one tmp_double. The recursive child
// enters Toom-6.5 (or Toom-8.5 above ~8x the cutoff) on pieces of size ~s/8.
// Empirically (BEMAN_BIG_INT_INSTRUMENT high-water probe over sizes 8000-130000,
// including two-level recursion) the peak/s ratio tops out at ~4.59 for the
// multiply kernel and ~4.43 for the square kernel. 6*s leaves ~24% margin and
// matches the ratio used by the smaller-radix algorithms.
constexpr std::size_t toom_cook_8_5_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 8.5 ("Toom 8'n'half") multiplication (Bodrato variant).
// Reference: Bodrato, "High degree Toom'n'half for balanced and unbalanced
//            multiplication" (ARITH-20, 2011); GMP mpn_toom8h_mul /
//            mpn_toom_interpolate_16pts (independent derivation; GMP is LGPL).
//
// Asymmetric Toom-9x8: the smaller operand is split into 8 pieces (degree-7 p)
// and the larger into up to 9 (degree-8 q), piece size k = ceil(min/8). Product
// r = p*q is degree 15. Evaluates r at 16 points
// {0, +-1, +-2, +-4, +-8, +-1/2, +-1/4, +-1/8, +infinity}, then solves two 7x7
// systems (even and odd parity) to recover c0..c15. c0 lives in result[0..2k);
// c15 = a7*b8 (zero for balanced inputs) lives in result[15k..).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs and
// must NOT alias `a` or `b`. `scratch` provides workspace. `cutoff_override` is
// the benchmark-only escape hatch; recursive sub-products use the default.
// Falls back to Toom-6.5 below the cutoff / outside the 9:8 ratio.
// ---------------------------------------------------------------------------
void multiply_toom_cook_8_5(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a_untrimmed,
                            const std::span<const uint_multiprecision_t> b_untrimmed,
                            scratch_allocator_base&                      scratch,
                            const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-8.5 squaring variant. Measured via
// multiplication_stress_bench (two runs): square-Toom-6.5 stays competitive
// longer than the multiply kernel, with a reproducible ~1% square-Toom-8.5 dip
// near 20000, so the cutoff sits above it where 8.5 overtakes cleanly.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t square_toom_cook_8_5_cutoff =
    24000; // kept; rarely reached on AArch64 (only where the FFT model refuses), not retuned
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr std::size_t square_toom_cook_8_5_cutoff = 14000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t square_toom_cook_8_5_cutoff = 5000;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t square_toom_cook_8_5_cutoff = 1500;
#else
inline constexpr std::size_t square_toom_cook_8_5_cutoff = 24000;
#endif

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_8_5. Squaring is always balanced
// (the general kernel's b8 is empty and c15 = 0). One evaluation per point; all
// fourteen products are recursive squares; sign handling drops out. The squared
// fractional-point evaluations come out scaled by base^14 instead of the base^15
// the (9x8) interpolation expects, so vh/vmh are shifted left 1, vq/vmq left 2,
// and ve/vme left 3 after squaring. Falls back to square_toom_cook_6_5 below the
// cutoff.
// `result` must be pre-zeroed with space for 2 * a.size() limbs and must NOT
// alias `a`. `cutoff_override` is a benchmark-only escape hatch.
// ---------------------------------------------------------------------------
void square_toom_cook_8_5(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// ---------------------------------------------------------------------------
// FFT (small-prime NTT) multiplication: the asymptotically-best tier, above
// Toom-Cook 8.5. Operands are split into base-2^b coefficients, convolved via a
// number-theoretic transform modulo several word-size primes, recombined with the
// CRT, and carry-propagated into the product. Two implementations, selected at
// build time by BEMAN_BIG_INT_SIMD_MUL:
//   * default (macro undefined): an INTEGER Montgomery transform over two ~62-bit
//     primes (src/ntt.cpp, src/fft_mul.cpp). Pure integer arithmetic -- exact on
//     every conforming compiler, with no floating-point-environment assumptions.
//   * BEMAN_BIG_INT_SIMD_MUL defined: a double-precision FP transform over three
//     ~50-bit primes with hand-written NEON/AVX2 kernels and runtime dispatch
//     (src/ntt_fp_*.cpp, src/fft_mul_fp.cpp). Faster, but its exactness REQUIRES
//     round-to-nearest and no FMA-contraction / fast-math. The build applies
//     -ffp-contract=off (/fp:strict) to those TUs; opting in means accepting
//     responsibility for that floating-point environment in your own build system.
// ---------------------------------------------------------------------------

// Bits per packed coefficient (b) is chosen per multiply: a larger b means fewer
// coefficients -- a smaller, faster transform. The cap of 50 is the FP-exactness
// ceiling (a coefficient must be < 2^b <= 2^50 < a prime for the double-precision
// modmul) and is safe for the integer path too. 32 is the floor; any higher b is
// pure upside.
inline constexpr unsigned fft_max_coeff_bits = 50;
static_assert(fft_max_coeff_bits <= 50, "FP modmul exactness requires coefficients < 2^50");

// Number of base-2^b coefficients an n-limb operand splits into.
constexpr std::size_t fft_coeff_count(const std::size_t n_limbs, const unsigned b) noexcept {
    return (width_v<uint_multiprecision_t> * n_limbs + b - 1) / b; // ceil
}

// Bit budget for the CRT: a convolution coefficient must stay below the product of
// the NTT primes. Two ~62-bit primes (~2^124) for the integer path; three ~50-bit
// primes (~2^149) for the FP path.
#if defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t fft_crt_bits = 148;
#else
inline constexpr std::size_t fft_crt_bits = 123;
#endif

// Largest b in [32, fft_max_coeff_bits] for which an na-by-nb limb product's
// convolution coefficients provably fit the prime product. The widest coefficient
// sums min(na_coeff, nb_coeff) products of two b-bit values, so we need
// min_coeff * (2^b-1)^2 < prime product; bit_width(min_coeff) + 2b <= fft_crt_bits
// is a safe integer test. Always returns >= 32.
constexpr unsigned fft_choose_coeff_bits(const std::size_t na, const std::size_t nb) noexcept {
    const std::size_t min_limbs = na < nb ? na : nb;
    for (unsigned b = fft_max_coeff_bits; b > 32; --b) {
        const std::size_t min_coeff = fft_coeff_count(min_limbs, b);
        if (static_cast<std::size_t>(std::bit_width(min_coeff)) + 2 * b <= fft_crt_bits) {
            return b;
        }
    }
    return 32;
}

// Transform length for an na-by-nb limb product: the linear convolution has
// na_coeff + nb_coeff - 1 coefficients; round up to a power of two so the cyclic
// transform computes the linear convolution with no wraparound.
constexpr std::size_t fft_transform_length(const std::size_t na, const std::size_t nb) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, b) + fft_coeff_count(nb, b) - 1;
    return std::bit_ceil(result_coeff);
}

#if defined(BEMAN_BIG_INT_SIMD_MUL)
// FP path workspaces. Transform buffers are doubles (fca[N] + fcb[N] + ftw[N], the
// per-level twiddle table uses N-1); the three primes' residues are uint64
// (3 * result_coeff). N = fft_transform_length.
constexpr std::size_t fft_mul_fp_storage_size(const std::size_t na, const std::size_t nb) noexcept {
    return 3 * fft_transform_length(na, nb);
}
constexpr std::size_t fft_mul_int_storage_size(const std::size_t na, const std::size_t nb) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, b) + fft_coeff_count(nb, b) - 1;
    return 3 * result_coeff;
}
constexpr std::size_t square_fft_fp_storage_size(const std::size_t n_limbs) noexcept {
    return 2 * fft_transform_length(n_limbs, n_limbs);
}
constexpr std::size_t square_fft_int_storage_size(const std::size_t n_limbs) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(n_limbs, n_limbs);
    const std::size_t result_coeff = 2 * fft_coeff_count(n_limbs, b) - 1;
    return 3 * result_coeff;
}
#else
// Integer path workspace (std::uint64_t): ca[N] + cb[N] + tw[N/2] +
// res0[result_coeff] for multiply; ca[N] + tw[N/2] + save[result_coeff] for square.
constexpr std::size_t fft_mul_storage_size(const std::size_t na, const std::size_t nb) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(na, nb);
    const std::size_t n            = fft_transform_length(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, b) + fft_coeff_count(nb, b) - 1;
    return 2 * n + n / 2 + result_coeff;
}
constexpr std::size_t square_fft_storage_size(const std::size_t n_limbs) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(n_limbs, n_limbs);
    const std::size_t n            = fft_transform_length(n_limbs, n_limbs);
    const std::size_t result_coeff = 2 * fft_coeff_count(n_limbs, b) - 1;
    return n + n / 2 + result_coeff;
}
#endif

// ---------------------------------------------------------------------------
// Cyclic NTT product sizes: a * b mod (2^(64w) - 1) computed with a
// transform of length L instead of the linear product's ~2L, by packing into
// uniform base 2^b with b * L == 64 * w exactly -- the transform's natural
// wraparound (mod x^L - 1) is then the value wraparound. Each output
// coefficient sums exactly L products of b-bit values, so the CRT bound
// uses L itself rather than the linear path's operand coefficient count.
// ---------------------------------------------------------------------------

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

struct fft_cyclic_params {
    std::size_t wrap_limbs; // w: the modulus is 2^(64w) - 1
    std::size_t length;     // L = 64 * w / b, a power of two
    unsigned    coeff_bits; // b, in [26, fft_max_coeff_bits]
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

// Smallest cyclic-capable wrap size >= min_w, with its packing. L ascends in
// powers of two (>= 64, so b * L is automatically a multiple of 64); for
// each L the smallest usable b is ceil(64 * min_w / L), admissible while it
// clears the cyclic CRT bound bit_width(L) + 2b <= fft_crt_bits. b lands in
// [26, 50] (the 26 floor keeps the search dense: the [32, 50] band's ratio
// is below 2, which would leave uncoverable wrap-size gaps); worst-case
// padding is under min_w / 25 (4%), and exactly zero when min_w is a power
// of two.
[[nodiscard]] constexpr fft_cyclic_params multiply_fft_cyclic_next_size(const std::size_t min_w) noexcept {
    constexpr std::size_t b_floor = 26;
    for (std::size_t length = 64;; length <<= 1) {
        const std::size_t b_cap = std::min<std::size_t>(
            fft_max_coeff_bits, (fft_crt_bits - static_cast<std::size_t>(std::bit_width(length))) / 2);
        const std::size_t b = std::max<std::size_t>(b_floor, (64 * min_w + length - 1) / length);
        if (b <= b_cap) {
            return {.wrap_limbs = b * (length / 64), .length = length, .coeff_bits = static_cast<unsigned>(b)};
        }
    }
}

// Compile-time properties the Barrett wiring relies on: growth, exact
// b*L == 64*w packing, the b range, idempotency (so a chooser size fed back
// in reproduces itself), the padding bound, and power-of-two lengths.
consteval bool fft_cyclic_next_size_properties() {
    for (std::size_t w = 1; w <= (std::size_t{1} << 22); w = w * 7 / 4 + 13) {
        const fft_cyclic_params p = multiply_fft_cyclic_next_size(w);
        const bool ok = p.wrap_limbs >= w && 64 * p.wrap_limbs == static_cast<std::size_t>(p.coeff_bits) * p.length &&
                        p.coeff_bits >= 26 && p.coeff_bits <= fft_max_coeff_bits && std::has_single_bit(p.length) &&
                        multiply_fft_cyclic_next_size(p.wrap_limbs).wrap_limbs == p.wrap_limbs &&
                        p.wrap_limbs < w + w / 25 + 64;
        if (!ok) {
            return false;
        }
    }
    return true;
}
static_assert(fft_cyclic_next_size_properties());

// Workspace sizes for the cyclic kernels (length-L transforms; the residue
// coefficient count equals L exactly).
#if defined(BEMAN_BIG_INT_SIMD_MUL)
constexpr std::size_t fft_cyclic_fp_storage_size(const fft_cyclic_params& p) noexcept { return 3 * p.length; }
constexpr std::size_t fft_cyclic_int_storage_size(const fft_cyclic_params& p) noexcept { return 3 * p.length; }
#else
constexpr std::size_t fft_cyclic_storage_size(const fft_cyclic_params& p) noexcept {
    return 2 * p.length + p.length / 2 + p.length;
}
#endif

// Balanced-product crossovers at which the FFT overtakes Toom-Cook 8.5, measured with multiplication_stress_bench
// (release). They are the starting floors of the x86-64 branches below; FFT cost steps at power-of-two transform
// length band boundaries, so each floor sits just above the boundary where the FFT first wins for the bulk of a band:
//
//   config                     fft_mul   square_fft
//   FP (SIMD), x86-64 AVX2        6000      11000   AVX2 makes the FFT viable early
//   FP (SIMD), x86-64 AVX2 IFMA  50000     (untuned)
//
// The integer x86-64 builds used floors of 24000 (generic, BMI2/ADX) and 400000 (IFMA) before the cost model: x86's
// fast 64x64 multiply makes Toom-Cook 8.5 beat the scalar NTT until about 40000-75000 limbs balanced.
//
// The integer x86-64 and AArch64 builds use the cost model below instead of a single crossover. Its constants come
// from M4 Max measurements (2026-09-30, shape_sweep end to end, integer 249 and SIMD 238 mixed shapes including
// ratios 1.1-8): the isqrt model is fitted to the measured Toom and FFT curves, the floors are limited by the data at
// 1400, and the SIMD floor of 3300 removes band-top false positives. FFT entry gates. The FFT time is a step function
// of the power-of-two transform length L while the Toom ladder is smooth, so a bare `min >= cutoff` is wrong inside
// every L band. The gate is a floor plus an integer cost model (fft_mul_worthwhile / square_fft_worthwhile below): use
// the FFT iff the shorter operand has at least *_min_limbs limbs and L * log2(L) * den <= num * max * isqrt(min) (the
// integer IFMA build uses log2(L)^3 and the cube root instead, see fft_model_log_power). A den of 0 switches the model
// off, leaving the plain floor (the portable branches and the SIMD branches other than x86-64 IFMA and AArch64, whose
// models are not tuned yet). The square gate is checked ahead of the square Toom chain; the model-off square floors
// (SIMD and portable) are at least square_toom_cook_6_5_cutoff, which is where the FFT used to be reachable.
#if defined(BEMAN_BIG_INT_SIMD_MUL)
    #if defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
// i9-11900K, gcc-release, 2026-09-30: the model is fitted to the forced Toom-Cook 8.5 and FP FFT tiers (balanced
// 5000-3276000 limbs) and to 67 shapes with shorter operands of 16000-1300000 and longer ones up to 6400000 limbs (FFT
// against sliced Toom): the faster tier is picked for 99% of the balanced sizes and 93% of those shapes, the rest
// within 1.12x. The plain floors
// (50000, 20000) took the FFT at band bottoms up to 1.3x (mul) and 2.2x (square) slower, and 11/64 (fitted to
// balanced sizes only) left the FFT unused on 1:2 to 1:16 shapes up to 1.3x faster with it. The floors are the
// smallest shapes measured.
inline constexpr std::size_t fft_mul_min_limbs    = 16000;
inline constexpr std::size_t fft_mul_model_num    = 15;
inline constexpr std::size_t fft_mul_model_den    = 64;
inline constexpr std::size_t square_fft_min_limbs = 8000;
inline constexpr std::size_t square_fft_model_num = 75;
inline constexpr std::size_t square_fft_model_den = 512;
    #elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t fft_mul_min_limbs    = 6000;
inline constexpr std::size_t fft_mul_model_num    = 1;
inline constexpr std::size_t fft_mul_model_den    = 0;
inline constexpr std::size_t square_fft_min_limbs = 11000;
inline constexpr std::size_t square_fft_model_num = 1;
inline constexpr std::size_t square_fft_model_den = 0;
    #elif defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t fft_mul_min_limbs    = 3300;
inline constexpr std::size_t fft_mul_model_num    = 5;
inline constexpr std::size_t fft_mul_model_den    = 8;
inline constexpr std::size_t square_fft_min_limbs = 3300;
inline constexpr std::size_t square_fft_model_num = 7;
inline constexpr std::size_t square_fft_model_den = 16;
    #else
inline constexpr std::size_t fft_mul_min_limbs    = 6000;
inline constexpr std::size_t fft_mul_model_num    = 1;
inline constexpr std::size_t fft_mul_model_den    = 0;
inline constexpr std::size_t square_fft_min_limbs = 6000;
inline constexpr std::size_t square_fft_model_num = 1;
inline constexpr std::size_t square_fft_model_den = 0;
    #endif
// x86-64 integer (non-SIMD): i9-11900K, gcc-release, 2026-09-30, shape_sweep end to end, balanced and unbalanced
// ratios. The scalar NTT is a step function of the transform length, so the cost model replaces the old floors (24000
// for generic and BMI2/ADX, 400000 for IFMA); the generic and BMI2/ADX floors are only an early-out, the model rejects
// small shapes itself.
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
// IFMA refit (2026-09-30) with the cubic model shape (fft_model_log_power, fft_model_root_degree below), fitted to the
// forced Toom-Cook 8.5 and NTT tiers up to 3276000 limbs balanced and to 52 shapes with shorter operands of
// 300000-1600000 and longer ones up to 8000000 limbs (NTT against sliced Toom): the faster tier is picked for 93-94%
// of them and the rest are within 1.13x. The earlier 7/64 and 6/64 with the square-root shape took the NTT at band
// bottoms up to 1.9x (mul) and 2.1x (square) slower. Below 300000 limbs the shape was not fitted and would accept
// small band-top shapes, so the multiply floor is a real limit (the NTT loses there); the square floor is below the
// model's first square (788000).
inline constexpr std::size_t fft_mul_min_limbs    = 300000;
inline constexpr std::size_t fft_mul_model_num    = 298;
inline constexpr std::size_t fft_mul_model_den    = 1;
inline constexpr std::size_t square_fft_min_limbs = 500000;
inline constexpr std::size_t square_fft_model_num = 268;
inline constexpr std::size_t square_fft_model_den = 1;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t fft_mul_min_limbs    = 3000;
inline constexpr std::size_t fft_mul_model_num    = 12;
inline constexpr std::size_t fft_mul_model_den    = 64;
inline constexpr std::size_t square_fft_min_limbs = 3000;
inline constexpr std::size_t square_fft_model_num = 8;
inline constexpr std::size_t square_fft_model_den = 64;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t fft_mul_min_limbs    = 3000;
inline constexpr std::size_t fft_mul_model_num    = 15;
inline constexpr std::size_t fft_mul_model_den    = 64;
inline constexpr std::size_t square_fft_min_limbs = 3000;
inline constexpr std::size_t square_fft_model_num = 10;
inline constexpr std::size_t square_fft_model_den = 64;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t fft_mul_min_limbs    = 24000;
inline constexpr std::size_t fft_mul_model_num    = 1;
inline constexpr std::size_t fft_mul_model_den    = 0;
inline constexpr std::size_t square_fft_min_limbs = 24000;
inline constexpr std::size_t square_fft_model_num = 1;
inline constexpr std::size_t square_fft_model_den = 0;
#elif defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t fft_mul_min_limbs    = 1400;
inline constexpr std::size_t fft_mul_model_num    = 13;
inline constexpr std::size_t fft_mul_model_den    = 16;
inline constexpr std::size_t square_fft_min_limbs = 1400;
inline constexpr std::size_t square_fft_model_num = 7;
inline constexpr std::size_t square_fft_model_den = 10;
#else
inline constexpr std::size_t fft_mul_min_limbs    = 4500;
inline constexpr std::size_t fft_mul_model_num    = 1;
inline constexpr std::size_t fft_mul_model_den    = 0;
inline constexpr std::size_t square_fft_min_limbs = 4500;
inline constexpr std::size_t square_fft_model_num = 1;
inline constexpr std::size_t square_fft_model_den = 0;
#endif

// Shape of the cost model, shared by the multiply and square gates: L * log2(L)^fft_model_log_power against
// max * min^(1 / fft_model_root_degree) (see fft_model_worthwhile). The default 1 and 2 fit the crossovers below a few
// hundred thousand limbs. The integer IFMA build crosses over at millions of limbs, where the NTT's memory traffic
// makes its time grow about 2.4x per transform length instead of the 2.1x of L log L, and Toom-Cook 8.5 grows like
// max * min^0.35; with the default shape its break-even drifted 0.8x per length.
#if !defined(BEMAN_BIG_INT_SIMD_MUL) && defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
inline constexpr unsigned fft_model_log_power   = 3;
inline constexpr unsigned fft_model_root_degree = 3;
#else
inline constexpr unsigned fft_model_log_power   = 1;
inline constexpr unsigned fft_model_root_degree = 2;
#endif
static_assert(fft_model_log_power >= 1 && fft_model_log_power <= 4, "log2(L)^p must stay far below 64 bits");
static_assert(fft_model_root_degree == 2 || fft_model_root_degree == 3);

static_assert(karatsuba_cutoff <= fft_mul_min_limbs, "the FFT gate is checked after the schoolbook gate");
// Model-off square floors keep the pre-model behaviour, where the FFT was only reachable from the Toom-6.5 tier up.
static_assert(square_fft_model_den != 0 || square_fft_min_limbs >= square_toom_cook_6_5_cutoff);

// Floor of the square root, exact for every 64-bit input.
[[nodiscard]] constexpr std::uint64_t isqrt_floor(const std::uint64_t x) noexcept {
    if (x < 2) {
        return x;
    }
    std::uint64_t r = std::uint64_t{1} << ((static_cast<unsigned>(std::bit_width(x)) + 1) / 2); // >= sqrt(x)
    for (;;) {
        const std::uint64_t y = (r + x / r) / 2;
        if (y >= r) {
            return r;
        }
        r = y;
    }
}

// Floor of the cube root, exact for every 64-bit input (bitwise; c * c <= x / c is c^3 <= x without overflow).
[[nodiscard]] constexpr std::uint64_t icbrt_floor(const std::uint64_t x) noexcept {
    std::uint64_t r = 0;
    for (int bit = 21; bit >= 0; --bit) { // cbrt(2^64) < 2^22
        const std::uint64_t c = r | (std::uint64_t{1} << bit);
        if (c * c <= x / c) {
            r = c;
        }
    }
    return r;
}

// A 192-bit product of three 64-bit values, for the exact cost-model comparison (MSVC has no __int128).
struct fft_u192 {
    std::uint64_t w0;
    std::uint64_t w1;
    std::uint64_t w2;
};

[[nodiscard]] constexpr fft_u192
fft_mul3(const std::uint64_t a, const std::uint64_t b, const std::uint64_t c) noexcept {
    const auto          ab = widening_mul(a, b);
    const auto          p0 = widening_mul(ab.low_bits, c);
    const auto          p1 = widening_mul(ab.high_bits, c);
    const std::uint64_t w1 = p0.high_bits + p1.low_bits;
    const std::uint64_t cy = w1 < p0.high_bits ? 1 : 0;
    return {p0.low_bits, w1, p1.high_bits + cy};
}

// FFT cost-model test: length * log2(length)^log_power * den <= num * max_size * root(min_size), where root is the
// floor of the square (root_degree 2) or cube (3) root, compared exactly as 192-bit products. `length` is a power of
// two, so log2(length) = bit_width - 1. A den of 0 is "model off".
[[nodiscard]] constexpr bool fft_model_worthwhile(const std::uint64_t length,
                                                  const std::uint64_t num,
                                                  const std::uint64_t den,
                                                  const std::uint64_t max_size,
                                                  const std::uint64_t min_size,
                                                  const unsigned      log_power   = 1,
                                                  const unsigned      root_degree = 2) noexcept {
    if (den == 0) {
        return true;
    }
    const std::uint64_t k     = static_cast<std::uint64_t>(std::bit_width(length)) - 1;
    std::uint64_t       k_pow = 1;
    for (unsigned i = 0; i < log_power; ++i) {
        k_pow *= k; // k <= 63 and log_power <= 4: at most 2^24
    }
    const std::uint64_t root = root_degree == 3 ? icbrt_floor(min_size) : isqrt_floor(min_size);
    const fft_u192      lhs  = fft_mul3(length, k_pow, den);
    const fft_u192      rhs  = fft_mul3(num, max_size, root);
    if (lhs.w2 != rhs.w2) {
        return lhs.w2 < rhs.w2;
    }
    if (lhs.w1 != rhs.w1) {
        return lhs.w1 < rhs.w1;
    }
    return lhs.w0 <= rhs.w0;
}

// True when a min_size x max_size product (min_size <= max_size) should take the FFT (64-bit limbs only).
[[nodiscard]] constexpr bool fft_mul_worthwhile(const std::size_t min_size, const std::size_t max_size) noexcept {
    return min_size >= fft_mul_min_limbs && fft_model_worthwhile(fft_transform_length(min_size, max_size),
                                                                 fft_mul_model_num,
                                                                 fft_mul_model_den,
                                                                 max_size,
                                                                 min_size,
                                                                 fft_model_log_power,
                                                                 fft_model_root_degree);
}

// True when an n-limb square should take the FFT (64-bit limbs only).
[[nodiscard]] constexpr bool square_fft_worthwhile(const std::size_t n) noexcept {
    return n >= square_fft_min_limbs && fft_model_worthwhile(fft_transform_length(n, n),
                                                             square_fft_model_num,
                                                             square_fft_model_den,
                                                             n,
                                                             n,
                                                             fft_model_log_power,
                                                             fft_model_root_degree);
}

// Feature macro for tools that print the cost-model constants above.
#define BEMAN_BIG_INT_HAS_FFT_COST_MODEL 1

static_assert(isqrt_floor(0) == 0 && isqrt_floor(1) == 1 && isqrt_floor(15) == 3 && isqrt_floor(16) == 4);
static_assert(isqrt_floor(std::numeric_limits<std::uint64_t>::max()) == 0xFFFFFFFFULL);
static_assert(isqrt_floor(std::uint64_t{1} << 62) == std::uint64_t{1} << 31);
static_assert(isqrt_floor((std::uint64_t{1} << 40) - 1) == (std::uint64_t{1} << 20) - 1);
static_assert(icbrt_floor(0) == 0 && icbrt_floor(7) == 1 && icbrt_floor(8) == 2 && icbrt_floor(26) == 2);
static_assert(icbrt_floor(27) == 3 && icbrt_floor(std::numeric_limits<std::uint64_t>::max()) == 2642245);
static_assert(icbrt_floor(std::uint64_t{2642245} * 2642245 * 2642245) == 2642245);
static_assert(icbrt_floor(std::uint64_t{2642245} * 2642245 * 2642245 - 1) == 2642244);
// Model off: the gate is the floor alone. Below the floor it is never taken; an enormous right side always is.
static_assert(!fft_mul_worthwhile(fft_mul_min_limbs - 1, fft_mul_min_limbs - 1));
static_assert(fft_mul_model_den != 0 || fft_mul_worthwhile(fft_mul_min_limbs, fft_mul_min_limbs));
static_assert(fft_model_worthwhile(std::uint64_t{1} << 30,
                                   13,
                                   16,
                                   std::numeric_limits<std::uint64_t>::max(),
                                   std::numeric_limits<std::uint64_t>::max()));
static_assert(fft_model_worthwhile(1024, 1, 0, 1, 1));
// Left side overflowing 64 bits (2^62 * 62 * 16) against a tiny right side: exactly false.
static_assert(!fft_model_worthwhile(std::uint64_t{1} << 62, 1, 16, 1, 1));
// Both sides beyond 64 bits are compared, not assumed: 2^62 * 62 * 16 (about 2^72) against 1 * 2^63 * 2^31.
static_assert(fft_model_worthwhile(std::uint64_t{1} << 62, 1, 16, std::uint64_t{1} << 63, std::uint64_t{1} << 62));
static_assert(!fft_model_worthwhile(std::uint64_t{1} << 20, 1, 1, 10, 10));

// Entry size for the cyclic NTT tier of multiply_mod_bnm1: at and above it
// the wrapped product runs one length-L transform set instead of the CRT
// split, whose internal products fall below the linear FFT cutoff and
// surrender its advantage. Tuned with division_kernel_bench's
// cyclic_over_crt sweep (direct kernel vs forced CRT split at chooser
// sizes, including the b = 26 band bottoms, which are the tier's least
// efficient coefficient widths), release builds, 2026-06-10:
//
//   config                    cutoff   worst ratio at/above; first loser below
//   integer, AArch64 (M4)       2048   0.98 at w=3328; w=1664 loses at 1.29
//   integer, x86-64 (11900K)   36864   0.86 at w=36864, 0.77 at the w=53248
//                                      band bottom; w=32768 is break-even and
//                                      w=26624 loses at 1.29 (Toom dominates
//                                      the scalar NTT until the CRT split's
//                                      internal products near the FFT floor)
//   FP (SIMD), x86-64 AVX2      8192   0.93 at w=8192, 0.82 at the w=13312
//                                      band bottom; w=6656 loses at 1.24
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
    // AArch64 (M4 Max, 108 chooser wrap sizes, 2026-09-30): integer 2048 (2560 ties), SIMD 8192 (4500-5000 tie); kept.
    #if defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t fft_cyclic_cutoff = 8192;
    #else
inline constexpr std::size_t fft_cyclic_cutoff = 2048;
    #endif
#elif defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t fft_cyclic_cutoff = 8192;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t fft_cyclic_cutoff = 36864;
#else
inline constexpr std::size_t fft_cyclic_cutoff = 2048;
#endif

// FFT kernels. Operands may be untrimmed; `result` must have space for
// a.size()+b.size() limbs and must NOT alias `a`/`b`; it writes exactly that many
// (resp. 2*a.size()) limbs and the dispatcher trims. Workspaces are sized by the
// *_storage_size helpers above.
#if defined(BEMAN_BIG_INT_SIMD_MUL)
void multiply_fft(std::span<uint_multiprecision_t>       result,
                  std::span<const uint_multiprecision_t> a_untrimmed,
                  std::span<const uint_multiprecision_t> b_untrimmed,
                  std::span<double>                      fp_workspace,
                  std::span<std::uint64_t>               int_workspace) noexcept;

void square_fft(std::span<uint_multiprecision_t>       result,
                std::span<const uint_multiprecision_t> a_untrimmed,
                std::span<double>                      fp_workspace,
                std::span<std::uint64_t>               int_workspace) noexcept;

// Cyclic kernel: result (exactly params.wrap_limbs limbs) = a * b mod
// (2^(64w) - 1), semi-canonical (all-ones means zero). `params` must come
// from multiply_fft_cyclic_next_size, and the operands must be at most w
// limbs. 64-bit limbs only.
void multiply_fft_cyclic(std::span<uint_multiprecision_t>       result,
                         std::span<const uint_multiprecision_t> a_untrimmed,
                         std::span<const uint_multiprecision_t> b_untrimmed,
                         fft_cyclic_params                      params,
                         std::span<double>                      fp_workspace,
                         std::span<std::uint64_t>               int_workspace) noexcept;
#else
void multiply_fft(std::span<uint_multiprecision_t>       result,
                  std::span<const uint_multiprecision_t> a_untrimmed,
                  std::span<const uint_multiprecision_t> b_untrimmed,
                  std::span<std::uint64_t>               workspace) noexcept;

void square_fft(std::span<uint_multiprecision_t>       result,
                std::span<const uint_multiprecision_t> a_untrimmed,
                std::span<std::uint64_t>               workspace) noexcept;

// Cyclic kernel: result (exactly params.wrap_limbs limbs) = a * b mod
// (2^(64w) - 1), semi-canonical (all-ones means zero). `params` must come
// from multiply_fft_cyclic_next_size, and the operands must be at most w
// limbs. 64-bit limbs only.
void multiply_fft_cyclic(std::span<uint_multiprecision_t>       result,
                         std::span<const uint_multiprecision_t> a_untrimmed,
                         std::span<const uint_multiprecision_t> b_untrimmed,
                         fft_cyclic_params                      params,
                         std::span<std::uint64_t>               workspace) noexcept;
#endif

// Scratch limbs for the Karatsuba / Toom ladder chosen by the shorter operand's
// size `min_size`, for a product whose longer operand has `s` limbs.
constexpr std::size_t toom_ladder_storage_size(const std::size_t min_size, const std::size_t s) noexcept {
    if (min_size < toom_cook_3_cutoff) {
        return karatsuba_storage_size(s);
    }
    if (min_size < toom_cook_4_cutoff) {
        return toom_cook_3_storage_size(s);
    }
    if (min_size < toom_cook_6_5_cutoff) {
        return toom_cook_4_storage_size(s);
    }
    if (min_size < toom_cook_8_5_cutoff) {
        return toom_cook_6_5_storage_size(s);
    }
    return toom_cook_8_5_storage_size(s);
}

// Unbalanced products are cut into pieces as long as the shorter operand and
// multiplied piece by piece (multiply_runtime). A product enters slicing once
// long / short >= num / den (the entry threshold). The ratio depends on the size
// of the shorter operand through a table of zones {min_limbs, num, den}, sorted by
// min_limbs: the entry with the largest min_limbs <= the shorter operand applies.
// Zones exist because each Toom tier only takes shapes up to its own ratio (8.5:
// 9/8, 6.5: 7/6, 4: 4/3, 3: 3/2) and a product outside it cascades to a slower
// tier, which slicing avoids. Once slicing, pieces are min-sized except the last,
// which is below 2m (see multiply_sliced).
// Every value is its own named constant (so a driver can edit each one), six zone
// slots per configuration, the first mul_slice_zone_count of them in use; slots
// beyond the count are ignored. One branch per configuration so each can be tuned
// alone. AArch64 (M4 Max) and x86-64 (i9-11900K) are tuned (2026-09-30); the portable
// branch is a placeholder that reproduces the pre-table three-zone behaviour
// (Karatsuba, Toom-3/4, Toom-6.5/8.5).
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
// AArch64 (M4 Max, 2026-09-30): R = 7/4 in the Karatsuba zone; 3/2 in both Toom zones (any ratio >= 3/2 behaves the
// same with the Toom-3 refusal term, and both zones are kept so the table mirrors the ladder).
inline constexpr std::size_t mul_slice_z0_min     = 112;
inline constexpr std::size_t mul_slice_z0_num     = 7;
inline constexpr std::size_t mul_slice_z0_den     = 4;
inline constexpr std::size_t mul_slice_z1_min     = 300;
inline constexpr std::size_t mul_slice_z1_num     = 3;
inline constexpr std::size_t mul_slice_z1_den     = 2;
inline constexpr std::size_t mul_slice_z2_min     = 1400;
inline constexpr std::size_t mul_slice_z2_num     = 3;
inline constexpr std::size_t mul_slice_z2_den     = 2;
inline constexpr std::size_t mul_slice_z3_min     = 0;
inline constexpr std::size_t mul_slice_z3_num     = 3;
inline constexpr std::size_t mul_slice_z3_den     = 2;
inline constexpr std::size_t mul_slice_z4_min     = 0;
inline constexpr std::size_t mul_slice_z4_num     = 3;
inline constexpr std::size_t mul_slice_z4_den     = 2;
inline constexpr std::size_t mul_slice_z5_min     = 0;
inline constexpr std::size_t mul_slice_z5_num     = 3;
inline constexpr std::size_t mul_slice_z5_den     = 2;
inline constexpr std::size_t mul_slice_zone_count = 3;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_AVX512_IFMA
// x86-64 (i9-11900K, gcc-release, 2026-09-30, shape_sweep end to end, balanced and unbalanced ratios.) R = 7/4 in the
// Karatsuba zone, 3/2 from the Toom-3 zone (Toom-3 refuses from about 1.5 anyway), then the ratio falls with the size
// because each larger Toom order accepts only a narrower band (Toom-8.5: 9/8).
inline constexpr std::size_t mul_slice_z0_min     = 260;
inline constexpr std::size_t mul_slice_z0_num     = 7;
inline constexpr std::size_t mul_slice_z0_den     = 4;
inline constexpr std::size_t mul_slice_z1_min     = 1600;
inline constexpr std::size_t mul_slice_z1_num     = 3;
inline constexpr std::size_t mul_slice_z1_den     = 2;
inline constexpr std::size_t mul_slice_z2_min     = 30000;
inline constexpr std::size_t mul_slice_z2_num     = 4;
inline constexpr std::size_t mul_slice_z2_den     = 3;
inline constexpr std::size_t mul_slice_z3_min     = 0;
inline constexpr std::size_t mul_slice_z3_num     = 3;
inline constexpr std::size_t mul_slice_z3_den     = 2;
inline constexpr std::size_t mul_slice_z4_min     = 0;
inline constexpr std::size_t mul_slice_z4_num     = 3;
inline constexpr std::size_t mul_slice_z4_den     = 2;
inline constexpr std::size_t mul_slice_z5_min     = 0;
inline constexpr std::size_t mul_slice_z5_num     = 3;
inline constexpr std::size_t mul_slice_z5_den     = 2;
inline constexpr std::size_t mul_slice_zone_count = 3;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64) && BEMAN_BIG_INT_X86_64_BMI2_ADX
inline constexpr std::size_t mul_slice_z0_min     = 47;
inline constexpr std::size_t mul_slice_z0_num     = 7;
inline constexpr std::size_t mul_slice_z0_den     = 4;
inline constexpr std::size_t mul_slice_z1_min     = 400;
inline constexpr std::size_t mul_slice_z1_num     = 3;
inline constexpr std::size_t mul_slice_z1_den     = 2;
inline constexpr std::size_t mul_slice_z2_min     = 16000;
inline constexpr std::size_t mul_slice_z2_num     = 9;
inline constexpr std::size_t mul_slice_z2_den     = 8;
inline constexpr std::size_t mul_slice_z3_min     = 0;
inline constexpr std::size_t mul_slice_z3_num     = 3;
inline constexpr std::size_t mul_slice_z3_den     = 2;
inline constexpr std::size_t mul_slice_z4_min     = 0;
inline constexpr std::size_t mul_slice_z4_num     = 3;
inline constexpr std::size_t mul_slice_z4_den     = 2;
inline constexpr std::size_t mul_slice_z5_min     = 0;
inline constexpr std::size_t mul_slice_z5_num     = 3;
inline constexpr std::size_t mul_slice_z5_den     = 2;
inline constexpr std::size_t mul_slice_zone_count = 3;
#elif defined(BEMAN_BIG_INT_ARCH_X86_64)
inline constexpr std::size_t mul_slice_z0_min     = 48;
inline constexpr std::size_t mul_slice_z0_num     = 7;
inline constexpr std::size_t mul_slice_z0_den     = 4;
inline constexpr std::size_t mul_slice_z1_min     = 400;
inline constexpr std::size_t mul_slice_z1_num     = 3;
inline constexpr std::size_t mul_slice_z1_den     = 2;
inline constexpr std::size_t mul_slice_z2_min     = 12000;
inline constexpr std::size_t mul_slice_z2_num     = 9;
inline constexpr std::size_t mul_slice_z2_den     = 8;
inline constexpr std::size_t mul_slice_z3_min     = 0;
inline constexpr std::size_t mul_slice_z3_num     = 3;
inline constexpr std::size_t mul_slice_z3_den     = 2;
inline constexpr std::size_t mul_slice_z4_min     = 0;
inline constexpr std::size_t mul_slice_z4_num     = 3;
inline constexpr std::size_t mul_slice_z4_den     = 2;
inline constexpr std::size_t mul_slice_z5_min     = 0;
inline constexpr std::size_t mul_slice_z5_num     = 3;
inline constexpr std::size_t mul_slice_z5_den     = 2;
inline constexpr std::size_t mul_slice_zone_count = 3;
#else
inline constexpr std::size_t mul_slice_z0_min     = 48;
inline constexpr std::size_t mul_slice_z0_num     = 8;
inline constexpr std::size_t mul_slice_z0_den     = 1;
inline constexpr std::size_t mul_slice_z1_min     = 400;
inline constexpr std::size_t mul_slice_z1_num     = 3;
inline constexpr std::size_t mul_slice_z1_den     = 2;
inline constexpr std::size_t mul_slice_z2_min     = 2400;
inline constexpr std::size_t mul_slice_z2_num     = 3;
inline constexpr std::size_t mul_slice_z2_den     = 2;
inline constexpr std::size_t mul_slice_z3_min     = 0;
inline constexpr std::size_t mul_slice_z3_num     = 3;
inline constexpr std::size_t mul_slice_z3_den     = 2;
inline constexpr std::size_t mul_slice_z4_min     = 0;
inline constexpr std::size_t mul_slice_z4_num     = 3;
inline constexpr std::size_t mul_slice_z4_den     = 2;
inline constexpr std::size_t mul_slice_z5_min     = 0;
inline constexpr std::size_t mul_slice_z5_num     = 3;
inline constexpr std::size_t mul_slice_z5_den     = 2;
inline constexpr std::size_t mul_slice_zone_count = 3;
#endif

struct mul_slice_ratio {
    std::uint64_t num;
    std::uint64_t den;
};

struct mul_slice_zone {
    std::uint64_t min_limbs;
    std::uint64_t num;
    std::uint64_t den;
};

inline constexpr std::array<mul_slice_zone, 6> mul_slice_zones{
    {{mul_slice_z0_min, mul_slice_z0_num, mul_slice_z0_den},
     {mul_slice_z1_min, mul_slice_z1_num, mul_slice_z1_den},
     {mul_slice_z2_min, mul_slice_z2_num, mul_slice_z2_den},
     {mul_slice_z3_min, mul_slice_z3_num, mul_slice_z3_den},
     {mul_slice_z4_min, mul_slice_z4_num, mul_slice_z4_den},
     {mul_slice_z5_min, mul_slice_z5_num, mul_slice_z5_den}}};

// The table must be usable for the dispatcher: 1 to 6 zones, the first at karatsuba_cutoff, sorted strictly by
// min_limbs, and every ratio at or above 9/8 (which also means num > den). The lower bound keeps the Euclidean
// remainder chain of a slicing product shrinking geometrically, which bounds the nesting depth of the tail
// re-dispatch and guarantees the last piece is below 2m.
[[nodiscard]] constexpr bool mul_slice_zones_valid() noexcept {
    if (mul_slice_zone_count < 1 || mul_slice_zone_count > mul_slice_zones.size()) {
        return false;
    }
    if (mul_slice_zones[0].min_limbs != karatsuba_cutoff) {
        return false;
    }
    for (std::size_t i = 0; i < mul_slice_zone_count; ++i) {
        const mul_slice_zone& z = mul_slice_zones[i];
        if (z.den == 0 || z.num * 8 < z.den * 9 || z.num <= z.den) {
            return false;
        }
        if (i > 0 && mul_slice_zones[i - 1].min_limbs >= z.min_limbs) {
            return false;
        }
    }
    return true;
}
static_assert(mul_slice_zones_valid(), "invalid slicing zone table (see mul_slice_zones_valid)");

// Feature macro for tools that print the slicing zone table.
#define BEMAN_BIG_INT_HAS_SLICE_ZONE_TABLE 1

// The slicing ratio for a shorter operand of min_size limbs: the zone with the largest min_limbs <= min_size (the
// first zone below the table), shared by the dispatcher and the tests.
[[nodiscard]] constexpr mul_slice_ratio mul_slice_zone_ratio(const std::size_t min_size) noexcept {
    std::size_t zone = 0;
    for (std::size_t i = 1; i < mul_slice_zone_count; ++i) {
        if (mul_slice_zones[i].min_limbs <= min_size) {
            zone = i;
        }
    }
    return {mul_slice_zones[zone].num, mul_slice_zones[zone].den};
}

// Toom-3's acceptance test: a min_size x max_size product is refused (and falls back to Karatsuba) when
// min_size <= 2 * ceil(max_size / 3). Shared by the kernel (src/toom_cook_3.cpp) and mul_should_slice.
[[nodiscard]] constexpr bool toom_cook_3_refuses_shape(const std::size_t min_size,
                                                       const std::size_t max_size) noexcept {
    return min_size <= 2 * ((max_size + 2) / 3);
}

// True when a min_size x max_size product (min_size <= max_size, both at or
// above karatsuba_cutoff) enters slicing: the zone's ratio test fires or, in
// the Toom zones, Toom-3 would refuse the shape (its k = ceil(max / 3) rounding
// makes shapes just below 3:2 fall through to pure Karatsuba). The second term
// needs max >= 3 * ceil(min / 2) - 2, so a cut piece always leaves a positive
// remainder.
[[nodiscard]] constexpr bool mul_should_slice(const std::size_t min_size, const std::size_t max_size) noexcept {
    const mul_slice_ratio r = mul_slice_zone_ratio(min_size);
    if (std::uint64_t{max_size} * r.den >= std::uint64_t{min_size} * r.num) {
        return true;
    }
    return min_size >= toom_cook_3_cutoff && toom_cook_3_refuses_shape(min_size, max_size);
}

// Feature macro for tools (tests/beman/big_int/perf/shape_sweep.cpp) that need
// the shape-aware dispatch names above.
#define BEMAN_BIG_INT_HAS_SHAPE_DISPATCH 1

// ---------------------------------------------------------------------------
// Runtime multiplication tier ladders (src/mul_dispatch.cpp): operands must
// be trimmed with at least two limbs; `result` pre-zeroed, sized for the
// full product, non-aliasing. Kernel workspaces come from the type-erased
// heap hooks. Returns the trimmed significant size.
// ---------------------------------------------------------------------------
std::size_t multiply_runtime(std::span<uint_multiprecision_t>       result,
                             std::span<const uint_multiprecision_t> a,
                             std::span<const uint_multiprecision_t> b,
                             const scratch_heap_source&             heap);

// Benchmark/test-only variants of multiply_runtime, same contract. The
// schoolbook (min < karatsuba_cutoff) and power-of-two shortcuts still apply in
// both. _sliced forces slicing of the top-level product whenever max > min,
// ignoring the ratio test, and skips the FFT gate even where it would take the
// product (so --path sliced there times Toom pieces); the pieces' tails revert
// to automatic dispatch and may take the FFT. _unsliced never slices; it keeps
// the automatic FFT gate.
std::size_t multiply_runtime_sliced(std::span<uint_multiprecision_t>       result,
                                    std::span<const uint_multiprecision_t> a,
                                    std::span<const uint_multiprecision_t> b,
                                    const scratch_heap_source&             heap);

std::size_t multiply_runtime_unsliced(std::span<uint_multiprecision_t>       result,
                                      std::span<const uint_multiprecision_t> a,
                                      std::span<const uint_multiprecision_t> b,
                                      const scratch_heap_source&             heap);

std::size_t square_runtime(std::span<uint_multiprecision_t>       result,
                           std::span<const uint_multiprecision_t> a,
                           const scratch_heap_source&             heap);

// Untrimmed/short-operand-tolerant runtime product for the src-side callers
// (the division tiers and mulmod): trims, takes the single-limb shortcuts,
// then runs the tier ladder. The runtime mirror of multiply_dispatch.
std::size_t multiply_runtime_any(std::span<uint_multiprecision_t>       result,
                                 std::span<const uint_multiprecision_t> a_untrimmed,
                                 std::span<const uint_multiprecision_t> b_untrimmed,
                                 const scratch_heap_source&             heap);

// ---------------------------------------------------------------------------
// result <- a * p2 where p2 is a trimmed power of two: a shifted copy of `a`
// placed at limb offset p2.size() - 1, bit-shifted by countr_zero(p2.back()).
// O(n) with no scratch, versus a full kernel dispatch (see GMP mpz_mul_2exp).
// `result` must be pre-zeroed, have space for a.size() + p2.size() limbs, and
// must NOT alias `a`. Returns the number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t multiply_power_of_two(const std::span<uint_multiprecision_t>       result,
                                            const std::span<const uint_multiprecision_t> a,
                                            const std::span<const uint_multiprecision_t> p2) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(is_power_of_two_span(p2));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + p2.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t limb_offset = p2.size() - 1;
    const auto        bit_shift   = static_cast<unsigned>(std::countr_zero(p2.back()));
    const auto        dst         = result.subspan(limb_offset);

    std::ranges::copy(a, dst.begin());
    return limb_offset + shift_left_n(dst, a.size(), bit_shift);
}

// ---------------------------------------------------------------------------
// Squaring dispatcher: result <- a * a using kernels that exploit the
// symmetric cross products. Same contract as multiply_dispatch: `result` must
// be pre-zeroed with space for 2 * a.size() limbs and must NOT alias `a`.
// `a` must be trimmed with at least two limbs. Returns the number of
// significant result limbs.
// ---------------------------------------------------------------------------
template <class Allocator>
std::size_t square_dispatch(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a,
                            Allocator&                                   alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const scratch_allocator<Allocator> hooks(alloc);
    return square_runtime(result, a, hooks.heap());
}

// ---------------------------------------------------------------------------
// Top-level multiplication dispatcher.
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// Returns the number of significant result limbs (trimmed).
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t multiply_dispatch(const std::span<uint_multiprecision_t>       result,
                                        const std::span<const uint_multiprecision_t> a_untrimmed,
                                        const std::span<const uint_multiprecision_t> b_untrimmed,
                                        Allocator&                                   alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_untrimmed.size() + b_untrimmed.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Trivial case, use single-limb shortcuts
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

    // This check has 0 runtime cost, but could speed up/reduce depth of constant evaluation
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        if (is_power_of_two_span(b)) {
            return multiply_power_of_two(result, a, b);
        }
        if (is_power_of_two_span(a)) {
            return multiply_power_of_two(result, b, a);
        }
        // Squaring halves the widening muls, and so the consteval step count.
        if (a.data() == b.data() && a.size() == b.size()) {
            square_long(result, a);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), 2 * a.size()});
        }
    }

    // The tier ladder lives in src/mul_dispatch.cpp; constant evaluation
    // keeps long multiplication (the recursive tiers could blow up consteval
    // step limits, and the kernels are runtime-compiled).
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        const scratch_allocator<Allocator> hooks(alloc);
        return multiply_runtime(result, a, b, hooks.heap());
    } else {
        // Long multiplication fallback explicitly on the constexpr path.
        multiply_long(result, a, b);
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size() + b.size()});
    }
}

// ---------------------------------------------------------------------------
// Wraparound multiplication: a * b mod (B^w - 1), the GMP mulmod_bnm1
// lineage. B^w == 1 (mod B^w - 1), so the full 2w-limb product folds in half;
// computing it via the CRT split
//   mod (B^h - 1)  (recursive)   x   mod (B^h + 1)  (one h x h product)
// with a multiplication-free reassembly makes a wrapped product cost about
// half a full multiplication. Used by the Barrett division tier, where the
// subtrahend's true value is known to be small so only its residue matters.
// ---------------------------------------------------------------------------

// Below this wrap size the plain product plus a fold wins. Re-validated on
// both tuning machines 2026-06-10: a floor of 8 ties within 2%, 32 and up
// lose 2-25% at wraps of 32-256 limbs; one value serves both machines (AArch64 has its own
// branch only so that x86 tuning cannot change it).
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t multiply_mod_bnm1_cutoff = 16; // AArch64 (M4 Max, 2026-09-30): thresholds 8-32 are flat
#else
inline constexpr std::size_t multiply_mod_bnm1_cutoff = 16;
#endif

static_assert(multiply_mod_bnm1_cutoff >= 2, "the recursion must stop above single-limb wraps");

// Smallest wrap size >= n that the recursion can halve all the way down to
// the threshold without going odd (GMP's mpn_mulmod_bnm1_next_size shape).
// At and above fft_cyclic_cutoff (64-bit limbs) sizes come from the cyclic
// NTT chooser instead, so the wrapped product runs as one length-L
// transform set rather than the CRT split.
[[nodiscard]] constexpr std::size_t multiply_mod_bnm1_next_size(const std::size_t n,
                                                                const std::size_t threshold) noexcept {
    if (n <= threshold) {
        return n;
    }
    if constexpr (width_v<uint_multiprecision_t> == 64) {
        if (n >= fft_cyclic_cutoff) {
            return multiply_fft_cyclic_next_size(n).wrap_limbs;
        }
    }
    std::size_t chunk = 1;
    while ((n + chunk - 1) / chunk > threshold) {
        chunk <<= 1;
    }
    return ((n + chunk - 1) / chunk) * chunk;
}

// Scratch upper bound for multiply_mod_bnm1 at wrap size w: the recursion
// holds the two h-limb folded inputs across its recursive call, then frees
// them before the mod-(B^h + 1) stage's three (h+1)-limb values and 2h-limb
// product; the basecase needs the full 2w-limb product. Validated by the
// instrumented probe in mulmod_bnm1.test.cpp.
constexpr std::size_t multiply_mod_bnm1_storage_size(const std::size_t w) noexcept { return 3 * w + 16; }

// dst = src mod (B^w - 1) with w = dst.size(), semi-canonical (the all-ones
// pattern, equal to the modulus, may appear and means zero).
// Requires src.size() <= 2 * w.
constexpr void fold_mod_bnm1(const std::span<uint_multiprecision_t>       dst,
                             const std::span<const uint_multiprecision_t> src) noexcept {
    const std::size_t w = dst.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(src.size() <= 2 * w);

    const std::size_t lo = std::min(w, src.size());
    std::ranges::copy(src.first(lo), dst.begin());
    std::ranges::fill(dst.subspan(lo), uint_multiprecision_t{0});
    if (src.size() > w) {
        if (add_unsigned_spans(dst, dst, src.subspan(w))) {
            // B^w == 1: fold the carry back in; if that wraps too, the value
            // was B^w == 1 exactly.
            if (increment_span(dst)) {
                dst[0] = 1;
            }
        }
    }
}

// dst = (dst + addend) mod (B^w - 1) with w = dst.size(), semi-canonical.
// addend.size() <= w.
constexpr void add_mod_bnm1(const std::span<uint_multiprecision_t>       dst,
                            const std::span<const uint_multiprecision_t> addend) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(addend.size() <= dst.size());
    if (add_unsigned_spans(dst, dst, addend)) {
        if (increment_span(dst)) {
            dst[0] = 1;
        }
    }
}

// dst = src mod (B^h + 1) with h + 1 = dst.size(), canonical in [0, B^h]
// (so dst's top limb is 0 or 1, and 1 forces the rest to zero).
// Requires src.size() <= 2 * h.
constexpr void fold_mod_bnp1(const std::span<uint_multiprecision_t>       dst,
                             const std::span<const uint_multiprecision_t> src) noexcept {
    const std::size_t h = dst.size() - 1;
    BEMAN_BIG_INT_DEBUG_ASSERT(h >= 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(src.size() <= 2 * h);

    // B^h == -1: value = lo - hi.
    const auto lo = src.first(std::min(h, src.size()));
    const auto hi = src.size() > h ? src.subspan(h) : std::span<const uint_multiprecision_t>{};

    if (compare_unsigned_spans(lo, hi) != std::strong_ordering::less) {
        std::ranges::copy(lo, dst.begin());
        std::ranges::fill(dst.subspan(lo.size()), uint_multiprecision_t{0});
        subtract_unsigned_spans(dst.first(h), dst.first(h), hi);
        return;
    }

    // lo < hi: value = lo - hi + B^h + 1; the wrapped subtraction already
    // adds B^h, so only the +1 remains. If that carries the value is exactly
    // B^h (lo - hi == -1).
    std::ranges::copy(lo, dst.begin());
    std::ranges::fill(dst.subspan(lo.size()), uint_multiprecision_t{0});
    const bool borrow = subtract_unsigned_spans_borrow_out(dst.first(h), dst.first(h), hi);
    BEMAN_BIG_INT_DEBUG_ASSERT(borrow);
    dst[h] = increment_span(dst.first(h)) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// r = a * b mod (B^w - 1) with w = r.size(), semi-canonical (all-ones means
// zero). a.size() and b.size() must be at most w (fold larger operands
// first); r must not alias the inputs. `scratch` provides
// multiply_mod_bnm1_storage_size(w) limbs AND must carry the type-erased
// heap hooks (any scratch_allocator<Allocator> does) for the internal
// products and the cyclic NTT tier's workspaces.
// Odd wrap sizes fall back to the plain product (size via
// multiply_mod_bnm1_next_size to keep the recursion even).
// `cutoff_override` is a test-only escape hatch forcing deep recursion.
// Compiled once in src/mulmod_bnm1.cpp.
// ---------------------------------------------------------------------------
void multiply_mod_bnm1(std::span<uint_multiprecision_t>       r,
                       std::span<const uint_multiprecision_t> a,
                       std::span<const uint_multiprecision_t> b,
                       scratch_allocator_base&                scratch,
                       std::size_t                            cutoff_override = 0);

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_MUL_IMPL_HPP
