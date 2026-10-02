// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises limbs_to_digits (MCA Algorithm 1.26 FastIntegerOutput) directly
// at the span level. Cross-checks: a per-digit short-division reference, a
// chunk-level reference, round-trips through digits_to_limbs (validated by
// base_conversion_input.test.cpp), and digit-string identities for power
// boundaries - in particular remainder fields with leading zeros, the
// classic divide-and-conquer output bug.

#include <beman/big_int/detail/base_conversion.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// Per-digit reference: repeated short division by `base` itself. Quadratic
// with a large constant; reserved for short values.
std::vector<unsigned char> digit_ref(const std::span<const uint_t> value, const int base) {
    std::vector<uint_t> work(value.begin(), value.end());
    work.resize(detail::trimmed_size_span(std::span<const uint_t>{work}));

    std::vector<unsigned char> reversed;
    std::size_t                size = work.size();
    do {
        const uint_t r = detail::divide_unsigned_short(std::span<uint_t>{work.data(), size},
                                                       std::span<const uint_t>{work.data(), size},
                                                       static_cast<uint_t>(base));
        reversed.push_back(static_cast<unsigned char>(r));
        if (size > 1 && work[size - 1] == 0) {
            --size;
        }
    } while (size > 1 || work[0] != 0);
    return {reversed.rbegin(), reversed.rend()};
}

// Chunk-level reference: repeated short division by big_base, chunks
// expanded digit by digit. Independent of the divide-and-conquer ladder.
std::vector<unsigned char> chunk_ref(const std::span<const uint_t> value, const int base) {
    const auto   cpl      = static_cast<std::size_t>(detail::limb_max_input_digits(base));
    const uint_t big_base = detail::limb_max_power(base);

    std::vector<uint_t> work(value.begin(), value.end());
    work.resize(detail::trimmed_size_span(std::span<const uint_t>{work}));

    std::vector<uint_t> chunks;
    std::size_t         size = work.size();
    do {
        chunks.push_back(detail::divide_unsigned_short(
            std::span<uint_t>{work.data(), size}, std::span<const uint_t>{work.data(), size}, big_base));
        if (size > 1 && work[size - 1] == 0) {
            --size;
        }
    } while (size > 1 || work[0] != 0);

    std::vector<unsigned char> digits;
    for (std::size_t i = chunks.size(); i-- > 0;) {
        uint_t                        chunk = chunks[i];
        std::array<unsigned char, 64> tmp{};
        std::size_t                   count = 0;
        do {
            tmp[count] = static_cast<unsigned char>(chunk % static_cast<uint_t>(base));
            chunk /= static_cast<uint_t>(base);
            ++count;
        } while (chunk != 0);
        const bool top = i + 1 == chunks.size();
        if (!top) {
            for (std::size_t pad = count; pad < cpl; ++pad) {
                digits.push_back(0);
            }
        }
        for (std::size_t d = count; d-- > 0;) {
            digits.push_back(tmp[d]);
        }
    }
    return digits;
}

// Runs the fast output path with contract checks: poisoned buffer, count in
// [1, bound], all digit values below the base, no leading zero unless the
// value is zero.
std::vector<unsigned char>
run_fast_out(const std::span<const uint_t> value, const int base, const std::size_t basecase_override = 0) {
    const std::size_t          bound = detail::base_conversion_digit_bound(value, base);
    std::vector<unsigned char> out(bound, static_cast<unsigned char>(0xAB));

    std::allocator<uint_t> alloc;
    const std::size_t      count =
        detail::limbs_to_digits(std::span<unsigned char>{out}, value, base, alloc, basecase_override);

    EXPECT_GE(count, 1u);
    EXPECT_LE(count, bound);
    // The Q0.8 reciprocal-log coefficient overshoots by < 1/256 per bit, so
    // the bound's slack stays proportionally small.
    EXPECT_LE(bound - count, bound / 32 + 2) << "digit bound slack grew beyond the Q0.8 error model";
    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_LT(static_cast<int>(out[i]), base) << "digit " << i;
    }
    if (count > 1) {
        EXPECT_NE(out[0], 0u) << "leading zero digit";
    }
    out.resize(count);
    return out;
}

// Builds a value from MSD-first digit values via digits_to_limbs (validated
// in base_conversion_input.test.cpp).
std::vector<uint_t> value_of(const std::span<const unsigned char> digits, const int base) {
    std::vector<uint_t>    limbs(detail::base_conversion_limb_bound(digits.size(), base), 0);
    std::allocator<uint_t> alloc;
    const std::size_t      count = detail::digits_to_limbs(std::span<uint_t>{limbs}, digits, base, alloc);
    limbs.resize(count);
    return limbs;
}

std::vector<uint_t> random_value(const std::size_t limbs, std::mt19937_64& rng) {
    std::vector<uint_t> v(limbs);
    for (auto& limb : v) {
        limb = static_cast<uint_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

std::vector<unsigned char> random_digits(const std::size_t len, const int base, std::mt19937_64& rng) {
    std::uniform_int_distribution<int> dist(0, base - 1);
    std::vector<unsigned char>         digits(len);
    for (auto& d : digits) {
        d = static_cast<unsigned char>(dist(rng));
    }
    if (digits[0] == 0) {
        digits[0] = 1;
    }
    return digits;
}

constexpr std::array<int, 30> non_power_of_two_bases = {3,  5,  6,  7,  9,  10, 11, 12, 13, 14, 15, 17, 18, 19, 20,
                                                        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36};

TEST(BaseConversionOutput, ZeroAndSmallValues) {
    for (const int base : {3, 10, 12, 36}) {
        const uint_t big_base = detail::limb_max_power(base);
        for (const uint_t value : {uint_t{0},
                                   uint_t{1},
                                   static_cast<uint_t>(base - 1),
                                   static_cast<uint_t>(base),
                                   big_base - 1,
                                   big_base,
                                   big_base + 1}) {
            const uint_t                  limbs[1] = {value};
            const std::span<const uint_t> v{limbs};
            EXPECT_EQ(run_fast_out(v, base), digit_ref(v, base)) << "base " << base << " value " << value;
        }
    }
}

TEST(BaseConversionOutput, RoundTripRandom) {
    std::mt19937_64 rng{0x07b1};
    for (const int base : {3, 10, 12, 36}) {
        for (const std::size_t limbs : {std::size_t{1},
                                        std::size_t{2},
                                        std::size_t{3},
                                        std::size_t{7},
                                        std::size_t{16},
                                        std::size_t{33},
                                        std::size_t{100},
                                        std::size_t{257}}) {
            for (const std::size_t override_group : {std::size_t{0}, std::size_t{2}, std::size_t{4}}) {
                const auto value  = random_value(limbs, rng);
                const auto digits = run_fast_out(value, base, override_group);
                EXPECT_EQ(value_of(digits, base), value)
                    << "base " << base << " limbs " << limbs << " group " << override_group;
                if (limbs <= 33) {
                    EXPECT_EQ(digits, chunk_ref(value, base))
                        << "base " << base << " limbs " << limbs << " group " << override_group;
                }
            }
        }
    }
}

// Digit-string identities around the power chain: values built from digit
// strings convert back to exactly those strings. Remainder fields full of
// zeros (Q * P_j) and nearly full of zeros (Q * P_j + tiny) are the classic
// bug shapes.
TEST(BaseConversionOutput, PowerBoundaryStrings) {
    std::mt19937_64 rng{0x07b2};
    for (const int base : {3, 10, 36}) {
        const auto cpl = static_cast<std::size_t>(detail::limb_max_input_digits(base));
        for (const std::size_t k :
             {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{8}, std::size_t{16}, std::size_t{32}}) {
            // big_base^k = "1" followed by k * cpl zeros.
            std::vector<unsigned char> digits(k * cpl + 1, 0);
            digits[0] = 1;
            EXPECT_EQ(run_fast_out(value_of(digits, base), base), digits) << "base " << base << " k " << k;

            // big_base^k + 1.
            digits.back() = 1;
            EXPECT_EQ(run_fast_out(value_of(digits, base), base), digits) << "base " << base << " k " << k;

            // big_base^k - 1: all (base - 1), maximal borrow/carry chains.
            const std::vector<unsigned char> nines(k * cpl, static_cast<unsigned char>(base - 1));
            EXPECT_EQ(run_fast_out(value_of(nines, base), base), nines) << "base " << base << " k " << k;

            // Q * big_base^k and Q * big_base^k + tiny for random Q: the
            // remainder field is all (or nearly all) leading zeros.
            const auto                 q = random_digits(3 * cpl, base, rng);
            std::vector<unsigned char> padded(q.begin(), q.end());
            padded.insert(padded.end(), k * cpl, static_cast<unsigned char>(0));
            EXPECT_EQ(run_fast_out(value_of(padded, base), base), padded) << "base " << base << " k " << k;

            padded.back() = static_cast<unsigned char>(base - 1);
            EXPECT_EQ(run_fast_out(value_of(padded, base), base), padded) << "base " << base << " k " << k;
        }
    }
}

TEST(BaseConversionOutput, AllBasesSampled) {
    std::mt19937_64 rng{0x07b3};
    for (const int base : non_power_of_two_bases) {
        for (const std::size_t limbs :
             {std::size_t{1}, std::size_t{2}, std::size_t{5}, std::size_t{20}, std::size_t{100}, std::size_t{500}}) {
            const auto value  = random_value(limbs, rng);
            const auto digits = run_fast_out(value, base);
            EXPECT_EQ(value_of(digits, base), value) << "base " << base << " limbs " << limbs;
            if (limbs <= 20) {
                EXPECT_EQ(digits, chunk_ref(value, base)) << "base " << base << " limbs " << limbs;
            }
        }
    }
}

TEST(BaseConversionOutput, Base10DenseLimbCounts) {
    std::mt19937_64 rng{0x07b4};
    const int       base = 10;
    for (std::size_t limbs = 1; limbs <= 100; ++limbs) {
        const auto value  = random_value(limbs, rng);
        const auto digits = run_fast_out(value, base);
        EXPECT_EQ(value_of(digits, base), value) << "limbs " << limbs;
        EXPECT_EQ(digits, chunk_ref(value, base)) << "limbs " << limbs;
    }
    for (const std::size_t limbs : {std::size_t{500}, std::size_t{2000}, std::size_t{10000}}) {
        const auto value  = random_value(limbs, rng);
        const auto digits = run_fast_out(value, base);
        EXPECT_EQ(value_of(digits, base), value) << "limbs " << limbs;
        if (limbs <= 2000) {
            EXPECT_EQ(digits, chunk_ref(value, base)) << "limbs " << limbs;
        }
    }
}

TEST(BaseConversionOutput, UntrimmedInputTolerated) {
    std::mt19937_64 rng{0x07b5};
    const auto      value = random_value(9, rng);

    std::vector<uint_t> padded = value;
    padded.insert(padded.end(), 5, uint_t{0});
    EXPECT_EQ(run_fast_out(padded, 10), run_fast_out(value, 10));

    const std::vector<uint_t>        zeros(7, uint_t{0});
    const std::vector<unsigned char> zero_digits{0};
    EXPECT_EQ(run_fast_out(zeros, 10), zero_digits);
}

TEST(BaseConversionOutput, ScratchPeakWithinBound) {
    std::mt19937_64 rng{0x07b6};
    for (const int base : {3, 10, 12}) {
        for (const std::size_t limbs :
             {std::size_t{1}, std::size_t{3}, std::size_t{17}, std::size_t{64}, std::size_t{300}, std::size_t{1500}}) {
            for (const std::size_t override_group : {std::size_t{0}, std::size_t{2}}) {
                const auto value = random_value(limbs, rng);

                const std::size_t      storage = detail::limbs_to_digits_storage_size(limbs, base, override_group);
                std::allocator<uint_t> alloc;
                detail::scratch_allocator<std::allocator<uint_t>> scratch(storage, alloc);

                std::vector<unsigned char> out(detail::base_conversion_digit_bound(value, base), 0);
                const std::size_t          count =
                    detail::limbs_to_digits(std::span<unsigned char>{out},
                                            std::span<const uint_t>{value},
                                            base,
                                            static_cast<detail::scratch_allocator_base&>(scratch),
                                            alloc,
                                            override_group);

                EXPECT_LE(scratch.peak(), storage)
                    << "base " << base << " limbs " << limbs << " group " << override_group;

                out.resize(count);
                EXPECT_EQ(value_of(out, base), value) << "base " << base << " limbs " << limbs;
            }
        }
    }
}

// The invariant-divisor Barrett entry against the self-contained driver on
// random shapes: identical quotient/remainder, peak within its storage
// model, and the scratch fully rewound (the conversion tree reuses one
// arena across many calls).
TEST(BaseConversionOutput, BarrettPreinvMatchesBarrett) {
    std::mt19937_64 rng{0x07b7};
    for (const std::size_t s : {std::size_t{2}, std::size_t{5}, std::size_t{16}, std::size_t{64}, std::size_t{300}}) {
        for (const std::size_t m : {2 * s, 3 * s + 1, 16 * s}) {
            const auto dividend = random_value(m, rng);
            auto       divisor  = random_value(s, rng);

            std::allocator<uint_t> alloc;
            std::vector<uint_t>    q_ref(m - s + 1, 0);
            std::vector<uint_t>    r_ref(m + 1, 0);
            detail::divide_barrett(std::span<uint_t>{q_ref},
                                   std::span<uint_t>{r_ref},
                                   std::span<const uint_t>{dividend},
                                   std::span<const uint_t>{divisor},
                                   alloc);

            // Normalize and invert once, as the conversion table does.
            const auto          shift  = static_cast<unsigned>(std::countl_zero(divisor.back()));
            std::vector<uint_t> d_norm = divisor;
            if (shift != 0) {
                [[maybe_unused]] const std::size_t norm_size =
                    detail::shift_left_n(std::span<uint_t>{d_norm}, s, shift);
            }

            const std::size_t blocks =
                detail::barrett_blocks(std::span<const uint_t>{dividend}, std::span<const uint_t>{divisor});
            const std::size_t storage = detail::reciprocal_span_storage_size(s, detail::reciprocal_span_cutoff) +
                                        detail::barrett_preinv_storage_size(s, blocks) + s;
            detail::scratch_allocator<std::allocator<uint_t>> scratch(storage, alloc);

            const std::span<uint_t> inv = scratch.allocate(s);
            detail::reciprocal_span(inv, std::span<const uint_t>{d_norm}, scratch);
            const std::size_t mark = scratch.m_offset;

            std::vector<uint_t> q(m - s + 1, 0);
            std::vector<uint_t> r(m + 1, 0);
            detail::divide_barrett_preinv(std::span<uint_t>{q},
                                          std::span<uint_t>{r},
                                          std::span<const uint_t>{dividend},
                                          std::span<const uint_t>{d_norm},
                                          shift,
                                          std::span<const uint_t>{inv},
                                          scratch);

            EXPECT_EQ(q, q_ref) << "m " << m << " s " << s;
            EXPECT_EQ(r, r_ref) << "m " << m << " s " << s;
            EXPECT_EQ(scratch.m_offset, mark) << "preinv must rewind fully";
            EXPECT_LE(scratch.peak(), storage) << "m " << m << " s " << s;
        }
    }
}

// Constant-evaluation smoke test: digits -> limbs -> digits is the identity,
// once with the tuned leaf size and once forcing the divide ladder.
consteval bool output_consteval_smoke(const std::size_t basecase_override) {
    constexpr std::size_t                  digit_count = 47;
    std::array<unsigned char, digit_count> digits{};
    for (std::size_t i = 0; i < digit_count; ++i) {
        digits[i] = static_cast<unsigned char>(i % 9 + (i == 0 ? 1 : 0));
    }

    std::array<uint_t, 8>  limbs{};
    std::allocator<uint_t> alloc;
    const std::size_t      limb_count =
        detail::digits_to_limbs(std::span<uint_t>{limbs}, std::span<const unsigned char>{digits}, 10, alloc);

    std::array<unsigned char, digit_count + 1> out{};
    const std::size_t count = detail::limbs_to_digits(std::span<unsigned char>{out},
                                                      std::span<const uint_t>{limbs.data(), limb_count},
                                                      10,
                                                      alloc,
                                                      basecase_override);

    if (count != digit_count) {
        return false;
    }
    for (std::size_t i = 0; i < digit_count; ++i) {
        if (out[i] != digits[i]) {
            return false;
        }
    }
    return true;
}

static_assert(output_consteval_smoke(0));
static_assert(output_consteval_smoke(2));

} // namespace
