// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises digits_to_limbs (MCA Algorithm 1.25 FastIntegerInput) directly at
// the span level. Results are cross-checked against a per-digit Horner
// reference built from span primitives, a chunk-level Horner reference for
// large inputs, and Boost cpp_int as an independent third opinion. The edge
// matrix covers chunk boundaries, big_base^k +- 1 carry chains, odd/even
// element counts at every ladder level, and the fully-written contract.

#include <beman/big_int/detail/config.hpp>

// Boost.Multiprecision cpp_int (the independent reference below) trips GCC's
// -Warray-bounds / stringop checks through its small-buffer optimization under
// fortify + optimization; these are false positives (cf. conversion_bench).
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

#include <beman/big_int/detail/base_conversion.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// Per-digit Horner reference using only span primitives: value = value * base + d.
// Independent of the chunking scheme; quadratic, so reserved for short inputs.
std::vector<uint_t> horner_ref(const std::span<const unsigned char> digits, const int base) {
    std::vector<uint_t> value{0};
    std::vector<uint_t> product;
    for (const unsigned char d : digits) {
        product.assign(value.size() + 1, 0);
        [[maybe_unused]] const std::size_t product_size = detail::multiply_single_limb(
            std::span<uint_t>{product}, std::span<const uint_t>{value}, static_cast<uint_t>(base));
        const uint_t                addend[1] = {static_cast<uint_t>(d)};
        [[maybe_unused]] const bool carry     = detail::add_unsigned_spans(
            std::span<uint_t>{product}, std::span<const uint_t>{product}, std::span<const uint_t>{addend});
        product.resize(detail::trimmed_size_span(std::span<const uint_t>{product}));
        value = product;
    }
    return value;
}

// Chunk-level Horner reference: packs limb_max_input_digits(base) digits per
// chunk (top chunk short) and folds value = value * big_base + chunk. Still
// independent of the divide-and-conquer ladder, but only O(chunks^2).
std::vector<uint_t> chunk_horner_ref(const std::span<const unsigned char> digits, const int base) {
    const auto   cpl      = static_cast<std::size_t>(detail::limb_max_input_digits(base));
    const uint_t big_base = detail::limb_max_power(base);

    const std::size_t top_width = digits.size() % cpl == 0 ? cpl : digits.size() % cpl;

    const auto pack = [&](const std::size_t pos, const std::size_t width) {
        uint_t chunk = 0;
        for (std::size_t i = 0; i < width; ++i) {
            chunk = chunk * static_cast<uint_t>(base) + static_cast<uint_t>(digits[pos + i]);
        }
        return chunk;
    };

    std::vector<uint_t> value{pack(0, top_width)};
    std::vector<uint_t> product;
    for (std::size_t pos = top_width; pos < digits.size(); pos += cpl) {
        product.assign(value.size() + 1, 0);
        [[maybe_unused]] const std::size_t product_size =
            detail::multiply_single_limb(std::span<uint_t>{product}, std::span<const uint_t>{value}, big_base);
        const uint_t                addend[1] = {pack(pos, cpl)};
        [[maybe_unused]] const bool carry     = detail::add_unsigned_spans(
            std::span<uint_t>{product}, std::span<const uint_t>{product}, std::span<const uint_t>{addend});
        product.resize(detail::trimmed_size_span(std::span<const uint_t>{product}));
        value = product;
    }
    return value;
}

// Boost cpp_int reference, exported to little-endian limbs.
std::vector<uint_t> cpp_int_ref(const std::span<const unsigned char> digits, const int base) {
    boost::multiprecision::cpp_int acc = 0;
    for (const unsigned char d : digits) {
        acc *= base;
        acc += d;
    }
    std::vector<uint_t> limbs;
    boost::multiprecision::export_bits(acc, std::back_inserter(limbs), detail::width_v<uint_t>, false);
    if (limbs.empty()) {
        limbs.push_back(0);
    }
    return limbs;
}

// Runs the fast path and checks the fully-written contract: the buffer is
// poisoned beforehand, the count is in [1, bound], and everything above the
// count is zero. Returns the trimmed limbs.
std::vector<uint_t>
run_fast(const std::span<const unsigned char> digits, const int base, const std::size_t basecase_override = 0) {
    const std::size_t   bound = detail::base_conversion_limb_bound(digits.size(), base);
    std::vector<uint_t> out(bound, static_cast<uint_t>(0xfefefefe));

    std::allocator<uint_t> alloc;
    const std::size_t      count = detail::digits_to_limbs(
        std::span<uint_t>{out}, std::span<const unsigned char>{digits}, base, alloc, basecase_override);

    EXPECT_GE(count, 1u);
    EXPECT_LE(count, bound);
    if (count > 0) {
        EXPECT_TRUE(count == 1 || out[count - 1] != 0);
    }
    for (std::size_t i = count; i < out.size(); ++i) {
        EXPECT_EQ(out[i], 0u) << "limb " << i << " above the count is not zero";
    }
    out.resize(count);
    return out;
}

std::vector<unsigned char> random_digits(const std::size_t len, const int base, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> dist(0, base - 1);
    std::vector<unsigned char>         digits(len);
    for (auto& d : digits) {
        d = static_cast<unsigned char>(dist(rng));
    }
    return digits;
}

constexpr std::array<int, 30> non_power_of_two_bases = {3,  5,  6,  7,  9,  10, 11, 12, 13, 14, 15, 17, 18, 19, 20,
                                                        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36};

TEST(BaseConversionInput, ZeroValues) {
    for (const int base : {3, 10, 36}) {
        const auto cpl = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        for (const std::size_t len : {std::size_t{1}, cpl, cpl + 1, 4 * cpl}) {
            const std::vector<unsigned char> digits(len, 0);
            const std::vector<uint_t>        expected{0};
            EXPECT_EQ(run_fast(digits, base), expected) << "base " << base << " len " << len;
        }
    }
}

TEST(BaseConversionInput, SingleChunkValues) {
    std::mt19937_64 rng{0xba5e1};
    for (const int base : {3, 10, 36}) {
        const auto cpl = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        for (std::size_t len = 1; len <= cpl; ++len) {
            const auto digits = random_digits(len, base, rng);
            EXPECT_EQ(run_fast(digits, base), horner_ref(digits, base)) << "base " << base << " len " << len;
        }
    }
}

TEST(BaseConversionInput, ChunkCountsExhaustive) {
    std::mt19937_64 rng{0xba5e2};
    for (const int base : {3, 10, 36}) {
        const auto cpl = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        for (std::size_t chunks = 1; chunks <= 65; ++chunks) {
            // Full top chunk and a one-digit top chunk per count.
            for (const std::size_t len : {chunks * cpl, (chunks - 1) * cpl + 1}) {
                const auto digits = random_digits(len, base, rng);
                const auto fast   = run_fast(digits, base);
                EXPECT_EQ(fast, chunk_horner_ref(digits, base)) << "base " << base << " len " << len;
                // Forced leaf-group sizes drive the ladder deep on these
                // small inputs so every level shape is hit cheaply.
                for (const std::size_t override_group : {std::size_t{1}, std::size_t{2}, std::size_t{8}}) {
                    EXPECT_EQ(run_fast(digits, base, override_group), fast)
                        << "base " << base << " len " << len << " group " << override_group;
                }
                if (chunks <= 8) {
                    EXPECT_EQ(fast, horner_ref(digits, base)) << "base " << base << " len " << len;
                    EXPECT_EQ(fast, cpp_int_ref(digits, base)) << "base " << base << " len " << len;
                }
            }
        }
    }
}

TEST(BaseConversionInput, PowerBoundaryValues) {
    for (const int base : {3, 10, 36}) {
        const auto cpl = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        for (const std::size_t k : {std::size_t{1},
                                    std::size_t{2},
                                    std::size_t{3},
                                    std::size_t{7},
                                    std::size_t{8},
                                    std::size_t{15},
                                    std::size_t{16},
                                    std::size_t{31},
                                    std::size_t{32},
                                    std::size_t{33},
                                    std::size_t{64}}) {
            // big_base^k: "1" followed by k * cpl zeros.
            std::vector<unsigned char> digits(k * cpl + 1, 0);
            digits[0] = 1;
            EXPECT_EQ(run_fast(digits, base), chunk_horner_ref(digits, base)) << "base " << base << " k " << k;

            // big_base^k + 1: "1", zeros, trailing "1".
            digits.back() = 1;
            EXPECT_EQ(run_fast(digits, base), chunk_horner_ref(digits, base)) << "base " << base << " k " << k;

            // big_base^k - 1: (base - 1) repeated k * cpl times. Maximal carry chains.
            const std::vector<unsigned char> nines(k * cpl, static_cast<unsigned char>(base - 1));
            EXPECT_EQ(run_fast(nines, base), chunk_horner_ref(nines, base)) << "base " << base << " k " << k;
        }
    }
}

TEST(BaseConversionInput, InteriorZeroRuns) {
    std::mt19937_64 rng{0xba5e3};
    const int       base = 10;
    const auto      cpl  = static_cast<std::size_t>(detail::limb_max_input_digits(base));

    for (const std::size_t zero_chunks : {std::size_t{1}, std::size_t{5}, std::size_t{40}}) {
        const auto                 prefix = random_digits(5, base, rng);
        const auto                 suffix = random_digits(5, base, rng);
        std::vector<unsigned char> digits;
        digits.insert(digits.end(), prefix.begin(), prefix.end());
        digits.insert(digits.end(), zero_chunks * cpl, static_cast<unsigned char>(0));
        digits.insert(digits.end(), suffix.begin(), suffix.end());
        EXPECT_EQ(run_fast(digits, base), chunk_horner_ref(digits, base)) << "zero chunks " << zero_chunks;
    }
}

TEST(BaseConversionInput, OddEvenLevelCounts) {
    std::mt19937_64 rng{0xba5e4};
    const int       base = 10;
    const auto      cpl  = static_cast<std::size_t>(detail::limb_max_input_digits(base));

    for (std::size_t k = 1; k <= 10; ++k) {
        const std::size_t pow2 = std::size_t{1} << k;
        for (const std::size_t chunks : {pow2 - 1, pow2, pow2 + 1, 3 * pow2 + 1}) {
            const auto digits = random_digits(chunks * cpl, base, rng);
            EXPECT_EQ(run_fast(digits, base), chunk_horner_ref(digits, base)) << "chunks " << chunks;
        }
    }
}

TEST(BaseConversionInput, AllBasesSampled) {
    std::mt19937_64 rng{0xba5e5};
    for (const int base : non_power_of_two_bases) {
        for (const std::size_t len :
             {std::size_t{1}, std::size_t{7}, std::size_t{100}, std::size_t{1000}, std::size_t{5000}}) {
            const auto digits = random_digits(len, base, rng);
            const auto fast   = run_fast(digits, base);
            EXPECT_EQ(fast, chunk_horner_ref(digits, base)) << "base " << base << " len " << len;
            if (len <= 100) {
                EXPECT_EQ(fast, cpp_int_ref(digits, base)) << "base " << base << " len " << len;
            }
        }
    }
}

TEST(BaseConversionInput, Base10Dense) {
    std::mt19937_64 rng{0xba5e6};
    const int       base = 10;

    for (std::size_t len = 1; len <= 200; ++len) {
        const auto digits = random_digits(len, base, rng);
        const auto fast   = run_fast(digits, base);
        EXPECT_EQ(fast, horner_ref(digits, base)) << "len " << len;
        EXPECT_EQ(fast, cpp_int_ref(digits, base)) << "len " << len;
    }
    for (const std::size_t len : {std::size_t{500},
                                  std::size_t{1000},
                                  std::size_t{2000},
                                  std::size_t{5000},
                                  std::size_t{20000},
                                  std::size_t{50000}}) {
        const auto digits = random_digits(len, base, rng);
        EXPECT_EQ(run_fast(digits, base), chunk_horner_ref(digits, base)) << "len " << len;
    }

    // A huge override forces the pure fused-Horner basecase at a size the
    // ladder would normally own.
    const auto digits = random_digits(5000, base, rng);
    EXPECT_EQ(run_fast(digits, base, std::size_t{1} << 20), chunk_horner_ref(digits, base));
}

TEST(BaseConversionInput, LeadingZeros) {
    std::mt19937_64 rng{0xba5e7};
    for (const int base : {3, 10, 36}) {
        const auto cpl   = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        const auto value = random_digits(3 * cpl + 2, base, rng);

        const auto expected = run_fast(value, base);
        for (const std::size_t zeros : {std::size_t{1}, cpl, 3 * cpl}) {
            std::vector<unsigned char> digits(zeros, 0);
            digits.insert(digits.end(), value.begin(), value.end());
            EXPECT_EQ(run_fast(digits, base), expected) << "base " << base << " zeros " << zeros;
        }
    }
}

TEST(BaseConversionInput, ScratchPeakWithinBound) {
    std::mt19937_64 rng{0xba5e8};
    for (const int base : {3, 10}) {
        const auto cpl = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        for (const std::size_t chunks :
             {std::size_t{2}, std::size_t{3}, std::size_t{5}, std::size_t{8}, std::size_t{33}, std::size_t{1024}}) {
            // Default leaf group plus forced sizes covering the pure-ladder
            // and mixed shapes.
            for (const std::size_t override_group : {std::size_t{0}, std::size_t{1}, std::size_t{4}}) {
                const std::size_t len    = chunks * cpl - 1;
                const auto        digits = random_digits(len, base, rng);

                const std::size_t      storage = detail::digits_to_limbs_storage_size(len, base, override_group);
                std::allocator<uint_t> alloc;
                detail::scratch_allocator<std::allocator<uint_t>> scratch(storage, alloc);

                std::vector<uint_t> out(detail::base_conversion_limb_bound(len, base), 0);
                const std::size_t   count =
                    detail::digits_to_limbs(std::span<uint_t>{out},
                                            std::span<const unsigned char>{digits},
                                            base,
                                            static_cast<detail::scratch_allocator_base&>(scratch),
                                            alloc,
                                            override_group);

                EXPECT_EQ(scratch.peak(), storage) << "the storage model is exact; base " << base << " chunks "
                                                   << chunks << " group " << override_group;

                out.resize(count);
                EXPECT_EQ(out, chunk_horner_ref(digits, base)) << "base " << base << " chunks " << chunks;
            }
        }
    }
}

TEST(BaseConversionInput, FusedKernelMatchesMultiplyAdd) {
    std::mt19937_64 rng{0xba5e9};
    for (const std::size_t size : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{17}}) {
        for (int rep = 0; rep < 50; ++rep) {
            std::vector<uint_t> value(size);
            for (auto& limb : value) {
                limb = static_cast<uint_t>(rng());
            }
            const auto mul = static_cast<uint_t>(rng() | 1);
            const auto add = static_cast<uint_t>(rng());

            std::vector<uint_t>                expected(size + 1, 0);
            [[maybe_unused]] const std::size_t product_size =
                detail::multiply_single_limb(std::span<uint_t>{expected}, std::span<const uint_t>{value}, mul);
            const uint_t                addend[1] = {add};
            [[maybe_unused]] const bool carry     = detail::add_unsigned_spans(
                std::span<uint_t>{expected}, std::span<const uint_t>{expected}, std::span<const uint_t>{addend});
            expected.resize(detail::trimmed_size_span(std::span<const uint_t>{expected}));

            std::vector<uint_t> fused = value;
            fused.push_back(static_cast<uint_t>(0xfefefefe));
            const std::size_t fused_size =
                detail::mul_add_single_limb_in_place(std::span<uint_t>{fused}, size, mul, add);

            ASSERT_LE(fused_size, fused.size());
            fused.resize(fused_size);
            EXPECT_EQ(fused, expected) << "size " << size;
        }
    }
}

// Constant-evaluation smoke test against an inline per-digit Horner,
// limb-width agnostic. Group 0 takes the tuned leaf size (the fused Horner
// basecase at this length); group 1 forces the full ladder (packing, power
// chain, combines) at compile time.
consteval bool input_consteval_smoke(const std::size_t basecase_override) {
    constexpr std::size_t                  digit_count = 47;
    std::array<unsigned char, digit_count> digits{};
    for (std::size_t i = 0; i < digit_count; ++i) {
        digits[i] = static_cast<unsigned char>((i * 7 + 3) % 10);
    }

    std::array<uint_t, 8>  out{};
    std::allocator<uint_t> alloc;
    const std::size_t      count = detail::digits_to_limbs(
        std::span<uint_t>{out}, std::span<const unsigned char>{digits}, 10, alloc, basecase_override);

    std::array<uint_t, 8> ref{};
    std::size_t           ref_size = 1;
    for (std::size_t i = 0; i < digit_count; ++i) {
        uint_t carry = static_cast<uint_t>(digits[i]);
        for (std::size_t j = 0; j < ref_size; ++j) {
            const auto [lo, hi] = detail::widening_mul(ref[j], uint_t{10});
            const uint_t next   = lo + carry;
            carry               = hi + static_cast<uint_t>(next < lo);
            ref[j]              = next;
        }
        if (carry != 0) {
            ref[ref_size] = carry;
            ++ref_size;
        }
    }

    if (count != ref_size) {
        return false;
    }
    for (std::size_t j = 0; j < out.size(); ++j) {
        if (out[j] != (j < ref_size ? ref[j] : uint_t{0})) {
            return false;
        }
    }
    return true;
}

static_assert(input_consteval_smoke(0));
static_assert(input_consteval_smoke(1));

} // namespace

BEMAN_BIG_INT_DIAGNOSTIC_POP()
