// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BASE_CONVERSION_HPP
#define BEMAN_BIG_INT_BASE_CONVERSION_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/base_tables.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <array>
    #include <bit>
    #include <cstddef>
    #include <memory>
    #include <span>
    #include <type_traits>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// ---------------------------------------------------------------------------
// Sub-quadratic base conversion (Modern Computer Arithmetic, Brent &
// Zimmermann, section 1.7.2: Algorithm 1.25 FastIntegerInput and Algorithm
// 1.26 FastIntegerOutput). Both directions work at chunk granularity: groups
// of limb_max_input_digits(base) digits pack into one limb (each chunk below
// big_base = limb_max_power(base)), and the divide-and-conquer ladder
// combines or splits chunks with the shared power chain
// P_j = big_base^(2^j). The digit boundary is digit VALUES (0..base-1, one
// unsigned char each, most significant first; the GMP mpn_set_str/get_str
// convention) -- ASCII mapping and validation stay in the charconv layer.
// Only non-power-of-two bases in [3, 36] are supported here; power-of-two
// bases already convert in linear time elsewhere.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr bool is_fast_conversion_base(const int base) noexcept {
    return base >= 3 && base <= 36 && !std::has_single_bit(static_cast<unsigned>(base));
}

// Upper bound on the limbs a digit_count-digit value in `base` can occupy.
[[nodiscard]] constexpr std::size_t base_conversion_limb_bound(const std::size_t digit_count, const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(digit_count > 0);
    return div_to_pos_inf(approximate_ceil_mul_log2(digit_count, base),
                          static_cast<std::size_t>(width_v<uint_multiprecision_t>));
}

// Chunks (groups of limb_max_input_digits(base) digits, one limb each) covering digit_count digits.
[[nodiscard]] constexpr std::size_t base_conversion_chunk_count(const std::size_t digit_count, const int base) {
    return div_to_pos_inf(digit_count, static_cast<std::size_t>(limb_max_input_digits(base)));
}

// Leaf group size for digits_to_limbs: inputs below TWO full groups take the
// fused Horner basecase outright (a (group, runt) split pays for the whole
// power chain to combine a sliver and measured 15-40% below the basecase on
// both architectures), and the ladder's leaves hold this many chunks. Power
// of two: leaf groups must tile the ladder's strides.
//
// Tuned with the base_conversion_bench crossover sweep (RelWithDebInfo,
// min-of-reps / median-of-samples, M4-class AArch64 + i9-11900K x86-64,
// 2026-06-12). AArch64: splitting ties the pure Horner at 64-chunk inputs
// and wins 8-15% by 128, leaves of 32 the best measured then (re-measured below). x86-64's stronger
// mul_1 keeps the basecase ahead much longer: po2-aligned splits first win
// at 512 chunks (leaves 128, +5%), but arbitrary counts in the 300-700 band
// lose 15-40% to the basecase for every leaf size tried, and 512-chunk
// leaves with the two-full-groups gate were the first shape that never
// measured below the basecase anywhere (at the cost of 15-25% off the
// po2-aligned optimum at 1024+ chunks). The 2^k + 1 lone-top sawtooth
// (Algorithm 1.25's unmultiplied top element) costs ~15% at the worst single
// point on both architectures; GMP-style balanced splitting was considered
// and deferred - the penalty amortizes to noise at neighboring sizes.
// AArch64 was re-measured on 2026-09-30 (M4 Max, appleclang-release, from_chars 228..155648 digits; measured on M4
// only, also used for other AArch64 cores and MSVC ARM64): leaves of 16 chunks beat 32 by 5.5% on average.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t fast_input_basecase_chunks = 16;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t fast_input_basecase_chunks = 512;
#else
inline constexpr std::size_t fast_input_basecase_chunks = 32;
#endif

static_assert(std::has_single_bit(fast_input_basecase_chunks),
              "leaf groups must tile the ladder's power-of-two strides");

// ---------------------------------------------------------------------------
// Quadratic basecase: packs the digit run into chunks (top chunk short) and
// folds them MSD-first via acc = acc * big_base + chunk. `acc` must hold
// base_conversion_chunk_count(digits.size(), base) limbs; only the returned
// size is written.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::size_t basecase_digits_to_limbs(const std::span<uint_multiprecision_t> acc,
                                                             const std::span<const unsigned char>   digits,
                                                             const int                              base) {
    const auto                  cpl      = static_cast<std::size_t>(limb_max_input_digits(base));
    const auto                  ubase    = static_cast<uint_multiprecision_t>(base);
    const uint_multiprecision_t big_base = limb_max_power(base);

    const auto pack = [&](const std::size_t pos, const std::size_t width) {
        uint_multiprecision_t chunk = 0;
        for (std::size_t i = 0; i < width; ++i) {
            BEMAN_BIG_INT_DEBUG_ASSERT(digits[pos + i] < base);
            chunk = chunk * ubase + static_cast<uint_multiprecision_t>(digits[pos + i]);
        }
        return chunk;
    };

    const std::size_t top_width = digits.size() % cpl == 0 ? cpl : digits.size() % cpl;

    acc[0]           = pack(0, top_width);
    std::size_t size = 1;
    for (std::size_t pos = top_width; pos < digits.size(); pos += cpl) {
        size = mul_add_single_limb_in_place(acc, size, big_base, pack(pos, cpl));
    }
    return size;
}

// ---------------------------------------------------------------------------
// One entry of the shared power chain, stored in the caller's scratch arena
// as P_j = big_base^(2^j) = value * B^low_zero_limbs. For even bases
// big_base = 2^z * odd, so P_j carries z * 2^j trailing zero bits and the
// floor(z * 2^j / limb_bits) whole zero limbs are not stored: every multiply
// or divide against the power shrinks by that much (the GMP powtab trick).
// Odd bases store low_zero_limbs == 0 and take the same code paths.
// ---------------------------------------------------------------------------
struct base_power_entry {
    std::span<const uint_multiprecision_t> value{};
    std::size_t                            low_zero_limbs = 0;
    // FastIntegerOutput preinv fields (attach_power_reciprocals, runtime
    // only, large entries only): the normalized value (top bit set) and its
    // reciprocal_span inverse, so every division by this power runs the
    // Barrett march with no per-call reciprocal.
    std::span<const uint_multiprecision_t> value_norm{};
    std::span<const uint_multiprecision_t> reciprocal{};
    std::size_t                            norm_shift = 0;
};

// The chain P_0 = big_base, P_{j+1} = P_j^2, shared by both conversion
// directions. entry[j] is valid for j < levels.
struct base_power_table {
    std::array<base_power_entry, width_v<std::size_t>> entry{};
    std::size_t                                        levels          = 0;
    uint_multiprecision_t                              big_base        = 0;
    int                                                base            = 0;
    int                                                chars_per_chunk = 0;
};

// ---------------------------------------------------------------------------
// dst <- src * src. Same contract as multiply_dispatch: `dst` pre-zeroed with
// space for 2 * src.size() limbs, no aliasing, `src` trimmed. Returns the
// trimmed product size. Routes to the squaring tiers at runtime;
// multiply_dispatch's same-pointer detection covers constant evaluation.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t square_into(const std::span<uint_multiprecision_t>       dst,
                                  const std::span<const uint_multiprecision_t> src,
                                  Allocator&                                   alloc) {
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (src.size() >= 2) {
            return square_dispatch(dst, src, alloc);
        }
    }
    return multiply_dispatch(dst, src, src, alloc);
}

// Scratch limbs build_power_table_for_chunks carves for the chain slots:
// slot j holds 2^j limbs, so a chain covering chunk_count chunks costs
// bit_ceil(chunk_count) - 1 limbs in total.
[[nodiscard]] constexpr std::size_t power_table_storage_size(const std::size_t chunk_count) noexcept {
    return chunk_count <= 1 ? 0 : std::bit_ceil(chunk_count) - 1;
}

// Fills the table header and carves the 1-limb slot for P_0 = big_base.
// Returns the initial trailing-zero bit offset for append_squared_power.
[[nodiscard]] constexpr std::size_t
init_power_table(base_power_table& table, const int base, scratch_allocator_base& scratch) {
    table.base            = base;
    table.chars_per_chunk = limb_max_input_digits(base);
    table.big_base        = limb_max_power(base);
    table.levels          = 1;

    const std::span<uint_multiprecision_t> slot0 = scratch.allocate(1);
    slot0[0]                                     = table.big_base;
    table.entry[0]                               = {std::span<const uint_multiprecision_t>{slot0}, 0};
    return static_cast<std::size_t>(std::countr_zero(table.big_base));
}

// ---------------------------------------------------------------------------
// Squares entry[level - 1] into a fresh slot of `slot_limbs` limbs (>= twice
// the stored value) and appends it as entry[level]. `r` is the in-limb
// trailing-zero bit offset of the stored value: P_j carries z * 2^j trailing
// zero bits (z = countr_zero(big_base)) of which whole limbs are trimmed
// away, and doubling r tells whether this squaring completes one more
// trimmable limb beyond the doubled trim - overflow-free, unlike tracking
// z * 2^j itself.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void append_squared_power(base_power_table&       table,
                                    const std::size_t       level,
                                    const std::size_t       slot_limbs,
                                    std::size_t&            r,
                                    scratch_allocator_base& scratch,
                                    Allocator&              alloc) {
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;

    const base_power_entry& prev = table.entry[level - 1];
    BEMAN_BIG_INT_DEBUG_ASSERT(slot_limbs >= 2 * prev.value.size());
    const std::span<uint_multiprecision_t> slot = scratch.allocate(slot_limbs);
    std::ranges::fill(slot, uint_multiprecision_t{0});
    std::size_t size = square_into(slot, prev.value, alloc);

    const std::size_t drop = 2 * r >= local_limb_bits ? 1 : 0;
    r                      = 2 * r - drop * local_limb_bits;
    if (drop != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(slot[0] == 0);
        std::ranges::copy(slot.subspan(1, size - 1), slot.begin());
        --size;
    }
    table.entry[level] = {std::span<const uint_multiprecision_t>{slot.data(), size}, 2 * prev.low_zero_limbs + drop};
    table.levels       = level + 1;
}

// ---------------------------------------------------------------------------
// Builds the chain P_0 .. P_{levels-1} with levels = ceil(log2(chunk_count)),
// exactly the powers Algorithm 1.25's ladder consumes for chunk_count leaf
// chunks. Slots are carved from `scratch` (power_table_storage_size limbs in
// total) and stay live until the caller rewinds them (LIFO).
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void build_power_table_for_chunks(base_power_table&       table,
                                            const std::size_t       chunk_count,
                                            const int               base,
                                            scratch_allocator_base& scratch,
                                            Allocator&              alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(is_fast_conversion_base(base));
    BEMAN_BIG_INT_DEBUG_ASSERT(chunk_count >= 2);

    const std::size_t target = static_cast<std::size_t>(std::bit_width(chunk_count - 1));
    std::size_t       r      = init_power_table(table, base, scratch);
    for (std::size_t j = 1; j < target; ++j) {
        append_squared_power(table, j, std::size_t{1} << j, r, scratch, alloc);
    }
}

// Leaf group size: the tuned threshold, or the passed override rounded down
// to a power of two (the test-only deep-recursion escape hatch).
[[nodiscard]] constexpr std::size_t fast_input_group_size(const std::size_t basecase_override) noexcept {
    return basecase_override == 0 ? fast_input_basecase_chunks : std::bit_floor(basecase_override);
}

// Scratch upper bound for digits_to_limbs: the basecase accumulator when the
// input stays below two leaf groups, otherwise two ping-pong combine arenas
// of bit_ceil(chunks) limbs each plus the power chain slots. Exact, and
// locked by the peak() probes in base_conversion_input.test.cpp. The
// multiplication tiers size and own their own workspace through the
// allocator's heap hooks.
[[nodiscard]] constexpr std::size_t digits_to_limbs_storage_size(const std::size_t digit_count,
                                                                 const int         base,
                                                                 const std::size_t basecase_override = 0) noexcept {
    const std::size_t chunks = base_conversion_chunk_count(digit_count, base);
    return chunks < 2 * fast_input_group_size(basecase_override) ? chunks : 3 * std::bit_ceil(chunks) - 1;
}

// ---------------------------------------------------------------------------
// One Algorithm 1.25 ladder level: next[p] = cur[2p] + P * cur[2p+1] over
// fixed strides (level elements occupy `stride` limbs and are below P, the
// destination stride is twice that, and the combined value stays below P^2).
// A lone top element passes through unmultiplied. Returns ceil(k / 2).
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t combine_level(const std::span<uint_multiprecision_t>       next,
                                    const std::span<const uint_multiprecision_t> cur,
                                    const std::size_t                            k,
                                    const std::size_t                            stride,
                                    const base_power_entry&                      power,
                                    Allocator&                                   alloc) {
    const std::size_t out_stride = 2 * stride;
    const std::size_t pairs      = k / 2;
    for (std::size_t p = 0; p < pairs; ++p) {
        const std::span<const uint_multiprecision_t> lo  = cur.subspan(2 * p * stride, stride);
        const std::span<const uint_multiprecision_t> hi  = cur.subspan((2 * p + 1) * stride, stride);
        const std::span<uint_multiprecision_t>       dst = next.subspan(p * out_stride, out_stride);
        std::ranges::fill(dst, uint_multiprecision_t{0});
        if (is_span_zero(hi)) {
            std::ranges::copy(lo, dst.begin());
            continue;
        }
        // P * hi lands B^low_zero_limbs up; the trimmed product fits the rest
        // of the stride exactly: |value| + |hi| <= (stride - low) + stride.
        BEMAN_BIG_INT_DEBUG_ASSERT(power.value.size() + power.low_zero_limbs <= stride);
        multiply_dispatch(dst.subspan(power.low_zero_limbs), power.value, hi.first(trimmed_size_span(hi)), alloc);
        [[maybe_unused]] const bool carry = add_unsigned_spans(dst, dst, lo);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    }
    if (k % 2 == 1) {
        const std::span<const uint_multiprecision_t> top = cur.subspan((k - 1) * stride, stride);
        const std::span<uint_multiprecision_t>       dst = next.subspan(pairs * out_stride, out_stride);
        std::ranges::copy(top, dst.begin());
        std::ranges::fill(dst.subspan(stride), uint_multiprecision_t{0});
    }
    return pairs + (k % 2);
}

// ---------------------------------------------------------------------------
// MCA Algorithm 1.25 FastIntegerInput at chunk granularity: leaf groups of
// fast_input_basecase_chunks chunks are evaluated by the fused Horner
// basecase straight into their ladder stride, then adjacent pairs combine
// bottom-up with the squared power chain until one value remains.
// `digits` holds MSD-first digit values, each below `base`, non-empty
// (leading zeros allowed). `result` must hold at least
// base_conversion_limb_bound(digits.size(), base) limbs; it is fully written
// (zero above the significant limbs). Returns the trimmed limb count (>= 1;
// a zero value yields 1 with result[0] == 0).
// `scratch` must provide digits_to_limbs_storage_size(...) limbs for the
// same `basecase_override`; the override forces a smaller leaf group
// (rounded down to a power of two) so tests recurse deeply on small inputs.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t digits_to_limbs(const std::span<uint_multiprecision_t> result,
                                      const std::span<const unsigned char>   digits,
                                      const int                              base,
                                      scratch_allocator_base&                scratch,
                                      Allocator&                             alloc,
                                      const std::size_t                      basecase_override = 0) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!digits.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(is_fast_conversion_base(base));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= base_conversion_limb_bound(digits.size(), base));

    const std::size_t chunks = base_conversion_chunk_count(digits.size(), base);
    const std::size_t group  = fast_input_group_size(basecase_override);
    const auto        cpl    = static_cast<std::size_t>(limb_max_input_digits(base));

    // The ladder only starts once it can form two full leaf groups: a split
    // into (group, runt) pays for the whole power chain to combine a sliver,
    // which measures well below the straight basecase on both architectures.
    if (chunks < 2 * group) {
        const std::span<uint_multiprecision_t> acc  = scratch.allocate(chunks);
        const std::size_t                      size = basecase_digits_to_limbs(acc, digits, base);
        std::ranges::copy(acc.first(size), result.begin());
        std::ranges::fill(result.subspan(size), uint_multiprecision_t{0});
        scratch.deallocate(chunks);
        return size;
    }

    const std::size_t                      arena_limbs = std::bit_ceil(chunks);
    const std::span<uint_multiprecision_t> arena_a     = scratch.allocate(arena_limbs);
    const std::span<uint_multiprecision_t> arena_b     = scratch.allocate(arena_limbs);

    base_power_table table{};
    build_power_table_for_chunks(table, chunks, base, scratch, alloc);

    // Leaf groups are LSD-aligned like the chunks themselves (only the top
    // group is short), each packed into its own stride of `group` limbs.
    const std::size_t groups = div_to_pos_inf(chunks, group);
    std::ranges::fill(arena_a.first(groups * group), uint_multiprecision_t{0});
    for (std::size_t g = 0; g < groups; ++g) {
        const std::size_t                  end   = digits.size() - g * group * cpl;
        const std::size_t                  span  = group * cpl;
        const std::size_t                  start = end > span ? end - span : 0;
        [[maybe_unused]] const std::size_t size =
            basecase_digits_to_limbs(arena_a.subspan(g * group, group), digits.subspan(start, end - start), base);
    }

    std::span<uint_multiprecision_t> cur   = arena_a;
    std::span<uint_multiprecision_t> next  = arena_b;
    std::size_t                      k     = groups;
    std::size_t                      level = static_cast<std::size_t>(std::countr_zero(group));
    while (k > 1) {
        k = combine_level(next, cur, k, std::size_t{1} << level, table.entry[level], alloc);
        std::swap(cur, next);
        ++level;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT((std::size_t{1} << level) == arena_limbs);

    const std::span<const uint_multiprecision_t> final_value{cur.data(), arena_limbs};
    const std::size_t                            size = trimmed_size_span(final_value);
    std::ranges::copy(final_value.first(size), result.begin());
    std::ranges::fill(result.subspan(size), uint_multiprecision_t{0});

    scratch.deallocate(2 * arena_limbs + power_table_storage_size(chunks));
    return size;
}

// Convenience overload owning the scratch workspace (the house dual-entry
// pattern, cf. divide_burnikel_ziegler).
template <class Allocator>
    requires(!std::is_base_of_v<scratch_allocator_base, Allocator>)
constexpr std::size_t digits_to_limbs(const std::span<uint_multiprecision_t> result,
                                      const std::span<const unsigned char>   digits,
                                      const int                              base,
                                      Allocator&                             alloc,
                                      const std::size_t                      basecase_override = 0) {
    scratch_allocator<Allocator> scratch(digits_to_limbs_storage_size(digits.size(), base, basecase_override), alloc);
    return digits_to_limbs(
        result, digits, base, static_cast<scratch_allocator_base&>(scratch), alloc, basecase_override);
}

// ===========================================================================
// FastIntegerOutput (MCA Algorithm 1.26)
// ===========================================================================

// Whether a recursive field keeps its full zero padding (interior fields are
// exactly as wide as their power, the classic correctness trap) or trims
// leading zeros (the leftmost spine and the public boundary).
enum class digit_padding : unsigned char {
    keep_leading_zeros,
    trim_leading_zeros,
};

// Below this many chunks limbs_to_digits extracts a field with the repeated
// short-division basecase instead of splitting by a power. Power of two
// (fields tile the po2 chain) and at least 2 (split divisors must be
// P_j, j >= 1, and the recursion steps down one level).
//
// Tuned with the base_conversion_bench output crossover sweep
// (RelWithDebInfo, min-of-reps / median-of-samples, M4-class AArch64 +
// i9-11900K x86-64, 2026-06-12): both architectures agree - the basecase
// wins 16-chunk inputs by 10-18%, splitting wins 32-chunk inputs by 6-9%
// and grows from there (the division basecase is far costlier than the
// input direction's mul_1 Horner, so the ladder pays much earlier and one value
// serves both; AArch64 has its own branch so x86 tuning cannot change it). Re-checked on
// AArch64 2026-09-30 (M4 Max): 16 stays best.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t fast_output_basecase_chunks = 16;
#else
inline constexpr std::size_t fast_output_basecase_chunks = 16;
#endif

static_assert(std::has_single_bit(fast_output_basecase_chunks) && fast_output_basecase_chunks >= 2,
              "output fields must tile the power-of-two chain and leave room to recurse");

// Output field size: the tuned threshold, or the override clamped to a power
// of two no smaller than 2 (the test-only deep-recursion escape hatch).
[[nodiscard]] constexpr std::size_t fast_output_group_size(const std::size_t basecase_override) noexcept {
    return basecase_override == 0 ? fast_output_basecase_chunks
                                  : std::max<std::size_t>(std::bit_floor(basecase_override), 2);
}

// Powers at least this many limbs get a precomputed reciprocal and take the
// invariant-divisor Barrett march instead of divide_dispatch. The tree's
// divisions are balanced (dividend ~ twice the power), exactly the shapes
// divide_dispatch routes to Barrett from barrett_balanced_cutoff dividend
// limbs up - there the precomputed reciprocal is a pure saving. Below the
// gate Burnikel-Ziegler wins those shapes even with the reciprocal free: a
// 32768-limb gate measured 9-14% SLOWER whole-conversion at 3M-10M digits
// on an M4-class machine (2026-06-12) by preempting B-Z at 32k-131k-limb
// levels. Deriving from the dispatch gate keeps it per-arch and
// NTT-configuration aware.
inline constexpr std::size_t fast_output_preinv_min_limbs = barrett_balanced_cutoff / 2;

// Bit width of a little-endian value span (0 for the value zero).
[[nodiscard]] constexpr std::size_t value_bit_width(const std::span<const uint_multiprecision_t> v) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!v.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(v.size() == 1 || v.back() != 0);
    return (v.size() - 1) * width_v<uint_multiprecision_t> + static_cast<std::size_t>(std::bit_width(v.back()));
}

// Upper bound on the digits an n-limb value needs in `base` (the
// to_basic_string sizing formula; overestimates by at most one digit).
[[nodiscard]] constexpr std::size_t base_conversion_digit_bound(const std::span<const uint_multiprecision_t> value,
                                                                const int                                    base) {
    const std::size_t width = value_bit_width(value.first(trimmed_size_span(value)));
    return width <= 1 ? std::size_t{1} : approximate_ceil_div_log2(width - 1, base) + 1;
}

// ---------------------------------------------------------------------------
// charconv integration glue. from_chars / to_chars route non-power-of-two bases
// through the kernels above a per-direction size gate; below it the fixed cost
// of a temporary digit-value buffer plus the kernel's owned scratch arena
// outweighs the win, so small (common) inputs keep the inline path. The gates
// are tuned at the whole-conversion crossover (the ASCII transcode included) --
// the single tunable surface for the integration.
// ---------------------------------------------------------------------------

// from_chars routes to the kernel once the input reaches this many chunks. The
// kernel's fused-Horner basecase already beats the charconv inline multiply-add
// loop from ~3-4 chunks up -- far below the kernel's OWN ladder gate
// (2 * fast_input_basecase_chunks) -- so the whole-conversion crossover (temp
// buffer + scratch arena + ASCII transcode included) sits much earlier. Tuned
// with the base_conversion_bench InputXover sweep (RelWithDebInfo, M4-class
// AArch64 + i9-11900K x86-64, 2026-06-15): both arches break even at 2-4 chunks
// and the kernel wins >= 1.6x by 8 chunks, growing from there. 8 (~150 base-10
// digits) keeps small-number parsing on the zero-allocation inline path with
// margin above the noisy floor. On AArch64 the 2026-09-30 re-measurement (M4 Max,
// from_chars 3..1000 digits, end to end) moved it to 3: the kernel already wins by
// ~3 chunks, and 8 was 1.5-2.2x slower at 57-133 digits. Measured on M4 only,
// also used for other AArch64 cores and MSVC ARM64. from_chars uses the kernel
// only when the value cannot fit the target's in-place storage, which keeps
// parsing of in-place values allocation-free.
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
inline constexpr std::size_t fast_input_charconv_min_chunks = 3;
#else
inline constexpr std::size_t fast_input_charconv_min_chunks = 8;
#endif

[[nodiscard]] constexpr bool fast_digits_to_limbs_profitable(const std::size_t digit_count, const int base) noexcept {
    return is_fast_conversion_base(base) &&
           base_conversion_chunk_count(digit_count, base) >= fast_input_charconv_min_chunks;
}

// limbs_to_digits takes the ladder when ceil(digit_bound / cpl) > fast_output_basecase_chunks.
[[nodiscard]] constexpr bool fast_limbs_to_digits_profitable(const std::span<const uint_multiprecision_t> value,
                                                             const int base) noexcept {
    if (!is_fast_conversion_base(base)) {
        return false;
    }
    const auto        cpl    = static_cast<std::size_t>(limb_max_input_digits(base));
    const std::size_t chunks = div_to_pos_inf(base_conversion_digit_bound(value, base), cpl);
    return chunks > fast_output_basecase_chunks;
}

// ---------------------------------------------------------------------------
// Owning, constexpr-safe scratch array of digit VALUES (0..base-1) for the
// charconv glue: from_chars transcodes the validated ASCII run into one before
// calling digits_to_limbs, and to_chars receives the MSD-first digits from
// limbs_to_digits into one before mapping them to ASCII. Allocated through a
// rebind of the big_int's own allocator so stateful / pmr allocators carry
// through. Bytes are value-initialized to begin their lifetimes during
// constant evaluation; every byte that is read is written first.
// ---------------------------------------------------------------------------
template <class LimbAllocator>
class digit_value_buffer {
    using byte_allocator = typename std::allocator_traits<LimbAllocator>::template rebind_alloc<unsigned char>;
    using traits         = std::allocator_traits<byte_allocator>;

  public:
    constexpr digit_value_buffer(const LimbAllocator& alloc, const std::size_t count)
        : m_alloc(alloc), m_count(count), m_data(traits::allocate(m_alloc, count)) {
        for (std::size_t i = 0; i < count; ++i) {
            std::construct_at(m_data + i, static_cast<unsigned char>(0));
        }
    }
    constexpr ~digit_value_buffer() {
        std::destroy_n(m_data, m_count);
        traits::deallocate(m_alloc, m_data, m_count);
    }
    digit_value_buffer(const digit_value_buffer&)            = delete;
    digit_value_buffer& operator=(const digit_value_buffer&) = delete;

    [[nodiscard]] constexpr std::span<unsigned char> span() const noexcept { return {m_data, m_count}; }

  private:
    BEMAN_BIG_INT_NO_UNIQUE_ADDRESS byte_allocator m_alloc;
    std::size_t                                    m_count;
    unsigned char*                                 m_data;
};

// True when value(v) >= P (the entry's power, value * B^low_zero_limbs).
// Decided by bit widths except in the equal-width band, where the stored
// limbs are compared against the value's matching window.
[[nodiscard]] constexpr bool value_reaches_power(const std::span<const uint_multiprecision_t> v,
                                                 const base_power_entry&                      power) noexcept {
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t power_bits = (power.low_zero_limbs + power.value.size() - 1) * local_limb_bits +
                                   static_cast<std::size_t>(std::bit_width(power.value.back()));
    const std::size_t value_bits = value_bit_width(v);
    if (value_bits != power_bits) {
        return value_bits > power_bits;
    }
    // Equal widths: v = v_hi * B^low + v_lo with v_hi the same width as the
    // stored value, so v >= P exactly when v_hi >= value (v_lo only breaks
    // the tie upward).
    return compare_unsigned_spans(v.subspan(power.low_zero_limbs), power.value) != std::strong_ordering::less;
}

// ---------------------------------------------------------------------------
// Builds the chain until the next square would certainly exceed `value`
// (bit-width test: bits(P^2) >= 2 bits(P) - 1), so the top entry satisfies
// P_top^2 > value - the emit invariant - at the cost of at most one level
// whose power exceeds the value (the recursion descends through it for
// free). Slots are tight (twice the previous stored size), unlike the
// stride-sized slots of the chunk-count builder.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void build_power_table_for_value(base_power_table&                            table,
                                           const std::span<const uint_multiprecision_t> value,
                                           const int                                    base,
                                           scratch_allocator_base&                      scratch,
                                           Allocator&                                   alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(is_fast_conversion_base(base));

    const std::size_t value_bits = value_bit_width(value);

    std::size_t r = init_power_table(table, base, scratch);
    while (true) {
        const base_power_entry& top = table.entry[table.levels - 1];
        const std::size_t top_bits  = top.low_zero_limbs * width_v<uint_multiprecision_t> + value_bit_width(top.value);
        if (2 * top_bits - 1 > value_bits) {
            return;
        }
        append_squared_power(table, table.levels, 2 * top.value.size(), r, scratch, alloc);
    }
}

// ---------------------------------------------------------------------------
// Precomputed single-limb divisor for the output basecase loops. The chunk
// peel calls divide_unsigned_short's algorithm with the reciprocal hoisted
// out (recomputing it per chunk is a measurable share at basecase sizes),
// and the digit expansion replaces `% base` / `/ base` outright - a runtime
// base defeats the compiler's divide-by-constant strength reduction, leaving
// a hardware divide per digit.
// ---------------------------------------------------------------------------
struct short_divisor {
    uint_multiprecision_t norm  = 0; // divisor << shift, top bit set
    uint_multiprecision_t recip = 0; // reciprocal_word(norm)
    std::size_t           shift = 0;
};

[[nodiscard]] constexpr short_divisor make_short_divisor(const uint_multiprecision_t divisor) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor >= 2);
    const auto                  shift = static_cast<std::size_t>(std::countl_zero(divisor));
    const uint_multiprecision_t norm  = divisor << shift;
    return {norm, reciprocal_word(norm), shift};
}

// In-place s.first(size) /= divisor; returns the remainder. The normalized
// funnel of divide_unsigned_short with the reciprocal hoisted.
[[nodiscard]] constexpr uint_multiprecision_t divide_short_preinv_in_place(const std::span<uint_multiprecision_t> s,
                                                                           const std::size_t                      size,
                                                                           const short_divisor& divisor) noexcept {
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(size >= 1 && size <= s.size());

    if (divisor.shift == 0) {
        uint_multiprecision_t r = 0;
        for (std::size_t i = size; i-- > 0;) {
            const auto qr = div_2by1_preinv(
                wide<uint_multiprecision_t>{.low_bits = s[i], .high_bits = r}, divisor.norm, divisor.recip);
            s[i] = qr.quotient;
            r    = qr.remainder;
        }
        return r;
    }

    uint_multiprecision_t cur = s[size - 1];
    uint_multiprecision_t r   = cur >> (local_limb_bits - divisor.shift);
    for (std::size_t i = size - 1; i > 0; --i) {
        const uint_multiprecision_t next = s[i - 1];
        const uint_multiprecision_t u    = funnel_shl(wide<uint_multiprecision_t>{.low_bits = next, .high_bits = cur},
                                                      static_cast<unsigned>(divisor.shift));
        const auto                  qr =
            div_2by1_preinv(wide<uint_multiprecision_t>{.low_bits = u, .high_bits = r}, divisor.norm, divisor.recip);
        s[i] = qr.quotient;
        r    = qr.remainder;
        cur  = next;
    }
    const auto qr = div_2by1_preinv(
        wide<uint_multiprecision_t>{.low_bits = cur << divisor.shift, .high_bits = r}, divisor.norm, divisor.recip);
    s[0] = qr.quotient;
    return qr.remainder >> divisor.shift;
}

// ---------------------------------------------------------------------------
// Attaches normalized values and reciprocal_span inverses to chain entries
// of at least fast_output_preinv_min_limbs limbs (runtime only:
// reciprocal_span and the Barrett march are compiled tiers). One reciprocal
// per level then serves all of that level's balanced divisions. Entries
// wider than half the value never see a balanced division (dividends by
// P_j stay below P_j^2), so their reciprocal would be dead weight - the
// root level often lands there when the chain stops close to the value.
// Storage stays live in the caller's arena like the chain slots themselves.
// ---------------------------------------------------------------------------
inline void
attach_power_reciprocals(base_power_table& table, const std::size_t value_limbs, scratch_allocator_base& scratch) {
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;

    for (std::size_t j = 1; j < table.levels; ++j) {
        base_power_entry& entry = table.entry[j];
        if (entry.value.size() < fast_output_preinv_min_limbs || 2 * entry.value.size() > value_limbs + 2) {
            continue;
        }
        const std::size_t s     = entry.value.size();
        const auto        shift = static_cast<std::size_t>(std::countl_zero(entry.value.back()));

        std::span<const uint_multiprecision_t> norm = entry.value;
        if (shift != 0) {
            const std::span<uint_multiprecision_t> norm_slot = scratch.allocate(s);
            std::ranges::copy(entry.value, norm_slot.begin());
            const std::size_t norm_size = shift_left_n(norm_slot, s, static_cast<unsigned>(shift));
            BEMAN_BIG_INT_DEBUG_ASSERT(norm_size == s);
            norm = std::span<const uint_multiprecision_t>{norm_slot.data(), s};
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(norm.back() >> (local_limb_bits - 1) == 1);

        const std::span<uint_multiprecision_t> inv_slot = scratch.allocate(s);
        reciprocal_span(inv_slot, norm, scratch);

        entry.value_norm = norm;
        entry.reciprocal = std::span<const uint_multiprecision_t>{inv_slot.data(), s};
        entry.norm_shift = shift;
    }
}

// ---------------------------------------------------------------------------
// Digit expansion of a single chunk: plain hardware division. A
// div_2by1_preinv replacement (the GMP fractional-limb lineage) was measured
// and REJECTED on both architectures (2026-06-12): the M4-class hardware
// divider beats the two-multiply chain by ~2 ns/digit, and on the i9-11900K
// GCC fuses the % / pair into one divide, leaving plain expansion 3-13%
// faster whole-conversion at 1k-30k digits.
// ---------------------------------------------------------------------------

// Writes `chunk` as exactly field.size() digit values, most significant
// first (zero-padded).
constexpr void write_chunk_digits(const std::span<unsigned char> field, uint_multiprecision_t chunk, const int base) {
    const auto ubase = static_cast<uint_multiprecision_t>(base);
    for (std::size_t d = field.size(); d-- > 0;) {
        field[d] = static_cast<unsigned char>(chunk % ubase);
        chunk /= ubase;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(chunk == 0);
}

// Writes `chunk` without leading zeros (value 0 -> one 0 digit); returns the
// digit count.
[[nodiscard]] constexpr std::size_t
write_chunk_digits_trimmed(const std::span<unsigned char> out, uint_multiprecision_t chunk, const int base) {
    const auto                    ubase = static_cast<uint_multiprecision_t>(base);
    std::array<unsigned char, 64> reversed{};
    std::size_t                   count = 0;
    do {
        BEMAN_BIG_INT_DEBUG_ASSERT(count < reversed.size());
        reversed[count] = static_cast<unsigned char>(chunk % ubase);
        chunk /= ubase;
        ++count;
    } while (chunk != 0);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = reversed[count - 1 - i];
    }
    return count;
}

// ---------------------------------------------------------------------------
// Quadratic basecase: peels field_chunks chunks off a working copy of `v`
// with repeated short divisions by big_base, then expands them MSD-first at
// `pos`. keep_leading_zeros writes the field's full
// field_chunks * chars_per_chunk digits; trim_leading_zeros writes only the
// significant ones. Preconditions: `v` trimmed, value(v) < big_base ^
// field_chunks; scratch holds field_chunks + v.size() limbs.
// ---------------------------------------------------------------------------
constexpr void basecase_limbs_to_digits(const std::span<unsigned char>               out,
                                        std::size_t&                                 pos,
                                        const std::span<const uint_multiprecision_t> v,
                                        const std::size_t                            field_chunks,
                                        const digit_padding                          padding,
                                        const int                                    base,
                                        scratch_allocator_base&                      scratch) {
    const auto          cpl     = static_cast<std::size_t>(limb_max_input_digits(base));
    const short_divisor big_div = make_short_divisor(limb_max_power(base));

    const std::span<uint_multiprecision_t> staging = scratch.allocate(field_chunks);
    const std::span<uint_multiprecision_t> work    = scratch.allocate(v.size());
    std::ranges::copy(v, work.begin());

    std::size_t size = v.size();
    std::size_t top  = 0;
    for (std::size_t i = 0; i < field_chunks; ++i) {
        if (size == 1 && work[0] == 0) {
            staging[i] = 0;
            continue;
        }
        staging[i] = divide_short_preinv_in_place(work, size, big_div);
        if (staging[i] != 0) {
            top = i;
        }
        if (size > 1 && work[size - 1] == 0) {
            --size;
        }
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(size == 1 && work[0] == 0);

    if (padding == digit_padding::keep_leading_zeros) {
        write_chunk_digits(out.subspan(pos, cpl), staging[field_chunks - 1], base);
        pos += cpl;
        for (std::size_t i = field_chunks - 1; i-- > 0;) {
            write_chunk_digits(out.subspan(pos, cpl), staging[i], base);
            pos += cpl;
        }
    } else {
        pos += write_chunk_digits_trimmed(out.subspan(pos), staging[top], base);
        for (std::size_t i = top; i-- > 0;) {
            write_chunk_digits(out.subspan(pos, cpl), staging[i], base);
            pos += cpl;
        }
    }
    scratch.deallocate(field_chunks + v.size());
}

// ---------------------------------------------------------------------------
// One MCA 1.26 node: a field of 2^(level + 1) chunks holding value(v) <
// P_level^2. Fields at or below the leaf size take the basecase; a value
// below P_level descends with the high half's zeros written (or skipped
// under trim); otherwise DivRem by P_level = value * B^w via the nested
// floor identity - only v >> w is divided by the trimmed power, and the
// remainder is its remainder concatenated above the low w limbs. The
// quotient spine inherits `padding`; the remainder field is emitted at
// exactly 2^level chunks.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void emit_digits(const std::span<unsigned char>               out,
                           std::size_t&                                 pos,
                           const std::span<const uint_multiprecision_t> v,
                           const std::size_t                            level,
                           const digit_padding                          padding,
                           const base_power_table&                      table,
                           const std::size_t                            group,
                           scratch_allocator_base&                      scratch,
                           Allocator&                                   alloc) {
    const std::size_t field = std::size_t{1} << (level + 1);
    if (field <= group) {
        basecase_limbs_to_digits(out, pos, v, field, padding, table.base, scratch);
        return;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(level >= 1 && level < table.levels);
    const base_power_entry& power = table.entry[level];

    if (!value_reaches_power(v, power)) {
        if (padding == digit_padding::keep_leading_zeros) {
            const std::size_t zeros = (field / 2) * static_cast<std::size_t>(table.chars_per_chunk);
            std::ranges::fill(out.subspan(pos, zeros), static_cast<unsigned char>(0));
            pos += zeros;
        }
        emit_digits(out, pos, v, level - 1, padding, table, group, scratch, alloc);
        return;
    }

    const std::size_t w    = power.low_zero_limbs;
    const std::size_t m    = v.size();
    const auto        v_hi = v.subspan(w);
    BEMAN_BIG_INT_DEBUG_ASSERT(v_hi.size() >= power.value.size());

    const std::size_t                      q_limbs = v_hi.size() - power.value.size() + 1;
    const std::span<uint_multiprecision_t> q_buf   = scratch.allocate(q_limbs);
    const std::span<uint_multiprecision_t> r_buf   = scratch.allocate(m + 1);
    std::ranges::fill(q_buf, uint_multiprecision_t{0});
    std::ranges::fill(r_buf, uint_multiprecision_t{0});
    std::ranges::copy(v.first(w), r_buf.begin());

    if (power.value.size() == 1) {
        // Heavily trimmed even-base powers can collapse to one limb.
        r_buf[w] = divide_unsigned_short(
            q_buf.first(v_hi.size()), std::span<const uint_multiprecision_t>{v_hi}, power.value[0]);
    } else if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        // The precomputed reciprocal only replaces divisions divide_dispatch
        // would route to Barrett anyway (the balanced shape rule) - there it
        // is a pure saving. Short-quotient nodes (the root when the chain
        // lands close to the value) stay with the dispatch ladder: a forced
        // 2-block march costs two full-width products no matter how small
        // the quotient is, measured +16% whole-conversion at 3M-10M digits.
        const bool balanced =
            v_hi.size() >= barrett_balanced_cutoff && v_hi.size() - power.value.size() >= power.value.size();
        if (!power.reciprocal.empty() && balanced) {
            divide_barrett_preinv(q_buf,
                                  r_buf.subspan(w),
                                  v_hi,
                                  power.value_norm,
                                  static_cast<unsigned>(power.norm_shift),
                                  power.reciprocal,
                                  scratch);
        } else {
            divide_dispatch(q_buf, r_buf.subspan(w), v_hi, power.value, scratch, alloc);
        }
    } else {
        divide_dispatch(q_buf, r_buf.subspan(w), v_hi, power.value, scratch, alloc);
    }

    const auto q_view = std::span<const uint_multiprecision_t>{q_buf}.first(
        trimmed_size_span(std::span<const uint_multiprecision_t>{q_buf}));
    const auto r_view = std::span<const uint_multiprecision_t>{r_buf}.first(
        trimmed_size_span(std::span<const uint_multiprecision_t>{r_buf}));

    emit_digits(out, pos, q_view, level - 1, padding, table, group, scratch, alloc);
    emit_digits(out, pos, r_view, level - 1, digit_padding::keep_leading_zeros, table, group, scratch, alloc);

    scratch.deallocate(q_limbs + m + 1);
}

// Scratch upper bound for limbs_to_digits over n limbs: the tight power
// chain (< 4n), the per-level quotient/remainder path (geometric, < 4n plus
// a few limbs per level), one schoolbook division transient (< 2n), and the
// basecase staging + working copy - bounded by the leaf field size, itself
// never beyond the value's own chunk capacity (~1.06n for the worst base).
// Values large enough for the preinv tier additionally hold the normalized
// powers and inverses (< 4(n + 2)), the reciprocal_span transient, and the
// Barrett march workspace of the top-level division. Conservative, not
// exact - node sizes depend on the value - and locked from above by the
// peak() probes in base_conversion_output.test.cpp.
[[nodiscard]] constexpr std::size_t limbs_to_digits_storage_size(const std::size_t limb_count,
                                                                 const int         base,
                                                                 const std::size_t basecase_override = 0) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(is_fast_conversion_base(base));
    const std::size_t staging = std::min(fast_output_group_size(basecase_override), limb_count + limb_count / 8 + 4);
    std::size_t       total   = 10 * limb_count + 2 * staging + 256;
    if (limb_count >= fast_output_preinv_min_limbs) {
        total += 4 * (limb_count + 2) + reciprocal_span_storage_size(limb_count + 2, reciprocal_span_cutoff) +
                 barrett_preinv_storage_size(limb_count / 2 + 2, 4);
    }
    return total;
}

// ---------------------------------------------------------------------------
// MCA Algorithm 1.26 FastIntegerOutput at chunk granularity: build the
// squared power chain up to the value, then split top-down with DivRem,
// remainder fields zero-padded to exactly their power's width and the
// leftmost spine trimmed.
// `value` holds little-endian limbs (untrimmed tolerated, not empty). `out`
// must hold base_conversion_digit_bound(value, base) digits; out[0..count)
// receives MSD-first digit values with no leading zeros (value 0 -> one 0
// digit). Returns the exact digit count.
// `scratch` must provide limbs_to_digits_storage_size(...) limbs for the
// same `basecase_override`; the override forces a smaller leaf field
// (clamped to a power of two >= 2) so tests recurse deeply on small inputs.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t limbs_to_digits(const std::span<unsigned char>               out,
                                      const std::span<const uint_multiprecision_t> value,
                                      const int                                    base,
                                      scratch_allocator_base&                      scratch,
                                      Allocator&                                   alloc,
                                      const std::size_t                            basecase_override = 0) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!value.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(is_fast_conversion_base(base));
    BEMAN_BIG_INT_DEBUG_ASSERT(out.size() >= base_conversion_digit_bound(value, base));

    const auto v     = value.first(trimmed_size_span(value));
    const auto group = fast_output_group_size(basecase_override);
    const auto cpl   = static_cast<std::size_t>(limb_max_input_digits(base));

    std::size_t pos = 0;

    const std::size_t chunk_bound = div_to_pos_inf(base_conversion_digit_bound(v, base), cpl);
    if (chunk_bound <= group) {
        basecase_limbs_to_digits(out, pos, v, chunk_bound, digit_padding::trim_leading_zeros, base, scratch);
        return pos;
    }

    const std::size_t table_mark = scratch.m_offset;
    base_power_table  table{};
    build_power_table_for_value(table, v, base, scratch, alloc);
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (v.size() >= 2 * fast_output_preinv_min_limbs) {
            attach_power_reciprocals(table, v.size(), scratch);
        }
    }

    emit_digits(out, pos, v, table.levels - 1, digit_padding::trim_leading_zeros, table, group, scratch, alloc);

    scratch.deallocate(scratch.m_offset - table_mark);
    return pos;
}

// Convenience overload owning the scratch workspace.
template <class Allocator>
    requires(!std::is_base_of_v<scratch_allocator_base, Allocator>)
constexpr std::size_t limbs_to_digits(const std::span<unsigned char>               out,
                                      const std::span<const uint_multiprecision_t> value,
                                      const int                                    base,
                                      Allocator&                                   alloc,
                                      const std::size_t                            basecase_override = 0) {
    scratch_allocator<Allocator> scratch(
        limbs_to_digits_storage_size(trimmed_size_span(value), base, basecase_override), alloc);
    return limbs_to_digits(out, value, base, static_cast<scratch_allocator_base&>(scratch), alloc, basecase_override);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_BASE_CONVERSION_HPP
