// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Unbalanced products: the shape-aware runtime dispatch (Euclidean slicing and
// the shape-aware FFT gate in src/mul_dispatch.cpp). Every shape is derived from
// the detail:: tuning constants so the test tracks retuning. Each product is
// checked against a two-prime residue, against an independent reference
// (multiply_long, the unsliced dispatch, or the FFT kernel), in both argument
// orders, and through the forced-slice and never-slice escape hatches.

#include "boost_mp_testing.hpp"
#include "testing.hpp"

#include <beman/big_int.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bmp    = ::beman::big_int::boost_mp_testing;
namespace detail = ::beman::big_int::detail;

namespace {

using uint_t  = ::beman::big_int::uint_multiprecision_t;
using alloc_t = std::allocator<uint_t>;
using limbs_t = std::vector<uint_t>;

constexpr std::size_t limb_bits = std::numeric_limits<uint_t>::digits;

// Work caps keep the suite modest under sanitizers: products beyond max_shape_work limb products, or with more
// than max_shape_limbs limbs, are skipped; multiply_long is the reference up to max_exact_work.
constexpr std::size_t max_exact_work  = 2'000'000;
constexpr std::size_t max_shape_work  = 20'000'000;
constexpr std::size_t max_shape_limbs = 250'000;
constexpr std::size_t guard_limbs     = 3;

// Picks the wide or narrow constant by limb width without a (useless) cast on 64-bit limbs.
template <class T>
constexpr T pick(const unsigned long long wide, const unsigned long long narrow) {
    if constexpr (sizeof(T) == 8) {
        return static_cast<T>(wide);
    } else {
        return static_cast<T>(narrow);
    }
}

constexpr uint_t sentinel = pick<uint_t>(0xA5A5A5A5A5A5A5A5ULL, 0xA5A5A5A5ULL);

// Two primes below the limb width for the residue checks (2^61 - 1, 2^62 - 57; 2^31 - 1, 2^32 - 5).
constexpr uint_t prime_1 = pick<uint_t>(2305843009213693951ULL, 2147483647ULL);
constexpr uint_t prime_2 = pick<uint_t>(4611686018427387847ULL, 4294967291ULL);

uint_t residue(const std::span<const uint_t> x, const uint_t p) { return detail::mod_unsigned_short(x, p); }

uint_t mulmod(const uint_t x, const uint_t y, const uint_t p) {
    const auto   w    = detail::widening_mul(x, y);
    const uint_t v[2] = {w.low_bits, w.high_bits};
    return detail::mod_unsigned_short(std::span<const uint_t>{v, 2}, p);
}

limbs_t make_random(const std::size_t n, const std::uint64_t seed) {
    std::mt19937_64 rng{seed * 0x9E3779B97F4A7C15ULL + n};
    limbs_t         v(n);
    for (auto& x : v) {
        x = static_cast<uint_t>(rng());
    }
    if (!v.empty() && v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

limbs_t trimmed(limbs_t v) {
    while (v.size() > 1 && v.back() == 0) {
        v.pop_back();
    }
    return v;
}

using runtime_fn = std::size_t (*)(std::span<uint_t>,
                                   std::span<const uint_t>,
                                   std::span<const uint_t>,
                                   const detail::scratch_heap_source&);

// Runs `fn` into a pre-zeroed result with guard limbs, checks the guards and the returned size, and returns the
// a.size() + b.size() product limbs.
limbs_t run(const runtime_fn fn, const std::span<const uint_t> a, const std::span<const uint_t> b) {
    const std::size_t total = a.size() + b.size();
    limbs_t           buf(total + guard_limbs, uint_t{0});
    std::fill(buf.begin() + static_cast<std::ptrdiff_t>(total), buf.end(), sentinel);
    alloc_t                                  alloc;
    const detail::scratch_allocator<alloc_t> hooks(alloc);
    const std::size_t                        n = fn(std::span<uint_t>{buf.data(), total}, a, b, hooks.heap());
    for (std::size_t i = total; i < buf.size(); ++i) {
        EXPECT_EQ(buf[i], sentinel) << "guard limb " << (i - total) << " overwritten";
    }
    buf.resize(total);
    EXPECT_EQ(n, detail::trimmed_size_span(std::span<const uint_t>{buf})) << "returned size is not the trimmed size";
    return buf;
}

std::size_t runtime_any(const std::span<uint_t>            r,
                        const std::span<const uint_t>      a,
                        const std::span<const uint_t>      b,
                        const detail::scratch_heap_source& h) {
    return detail::multiply_runtime_any(r, a, b, h);
}

[[maybe_unused]] limbs_t fft_reference(const std::span<const uint_t> a, const std::span<const uint_t> b) {
    limbs_t out(a.size() + b.size(), uint_t{0});
    if constexpr (limb_bits == 64) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
        std::vector<double>        fp_ws(detail::fft_mul_fp_storage_size(a.size(), b.size()));
        std::vector<std::uint64_t> int_ws(detail::fft_mul_int_storage_size(a.size(), b.size()));
        detail::multiply_fft(out, a, b, fp_ws, int_ws);
#else
        std::vector<std::uint64_t> ws(detail::fft_mul_storage_size(a.size(), b.size()));
        detail::multiply_fft(out, a, b, ws);
#endif
    }
    return out;
}

bool work_ok(const std::size_t m, const std::size_t n) {
    return m >= 2 && n >= m && m + n <= max_shape_limbs && (n <= max_shape_work / m);
}

// Full check of a * b: any/swapped/sliced/unsliced dispatch, residues, and an independent reference.
void check_product(const std::span<const uint_t> a, const std::span<const uint_t> b) {
    SCOPED_TRACE("a limbs=" + std::to_string(a.size()) + " b limbs=" + std::to_string(b.size()));

    const limbs_t got = run(&runtime_any, a, b);
    EXPECT_EQ(trimmed(run(&runtime_any, b, a)), trimmed(got)) << "argument order changed the product";

    for (const uint_t p : {prime_1, prime_2}) {
        EXPECT_EQ(mulmod(residue(a, p), residue(b, p), p), residue(std::span<const uint_t>{got}, p))
            << "residue mismatch mod " << p;
    }

    const auto ta = a.first(detail::trimmed_size_span(a));
    const auto tb = b.first(detail::trimmed_size_span(b));
    if (ta.size() < 2 || tb.size() < 2) {
        return;
    }

    const std::size_t min_size = std::min(ta.size(), tb.size());
    const std::size_t max_size = std::max(ta.size(), tb.size());
    const auto        want     = trimmed(got);

    if (max_size <= max_exact_work / min_size) {
        limbs_t ref(ta.size() + tb.size());
        detail::multiply_long(ref, ta, tb);
        EXPECT_EQ(want, trimmed(ref)) << "differs from multiply_long";
    } else {
        EXPECT_EQ(want, trimmed(run(&detail::multiply_runtime_unsliced, ta, tb))) << "differs from unsliced dispatch";
        if constexpr (limb_bits == 64) {
            if (ta.size() + tb.size() <= 200'000) {
                EXPECT_EQ(want, trimmed(fft_reference(ta, tb))) << "differs from the FFT kernel";
            }
        }
    }

    EXPECT_EQ(want, trimmed(run(&detail::multiply_runtime_sliced, ta, tb))) << "forced slicing differs";
    EXPECT_EQ(want, trimmed(run(&detail::multiply_runtime_sliced, tb, ta))) << "forced slicing (swapped) differs";
}

void check_shape(const std::size_t m, const std::size_t n, const std::uint64_t seed = 1) {
    if (!work_ok(m, n)) {
        return;
    }
    const limbs_t lng = make_random(n, seed * 7 + 1);
    const limbs_t sht = make_random(m, seed * 7 + 2);
    check_product(lng, sht);
}

// Reduced check for shapes above the work cap: auto dispatch in both orders, the returned size, the two-prime
// residue and (64-bit limbs, unless fft_reference_too is false) the FFT kernel as the independent reference. No
// multiply_long, no other dispatch runs.
void check_cheap(const std::size_t   m,
                 const std::size_t   n,
                 const std::uint64_t seed              = 1,
                 const bool          fft_reference_too = true) {
    SCOPED_TRACE("cheap m=" + std::to_string(m) + " n=" + std::to_string(n));
    if (m < 2 || n < m) {
        return;
    }
    const limbs_t lng = make_random(n, seed * 7 + 3);
    const limbs_t sht = make_random(m, seed * 7 + 4);
    const limbs_t got = run(&runtime_any, lng, sht);
    EXPECT_EQ(trimmed(run(&runtime_any, sht, lng)), trimmed(got)) << "argument order changed the product";
    for (const uint_t p : {prime_1, prime_2}) {
        EXPECT_EQ(mulmod(residue(lng, p), residue(sht, p), p), residue(std::span<const uint_t>{got}, p))
            << "residue mismatch mod " << p;
    }
    if constexpr (limb_bits == 64) {
        if (fft_reference_too) {
            EXPECT_EQ(trimmed(got), trimmed(fft_reference(lng, sht))) << "differs from the FFT kernel";
        }
    }
}

// uint64_t to size_t without a useless cast where they are the same type (a template, so -Wuseless-cast stays quiet).
template <class U>
constexpr std::size_t to_size(const U v) {
    if constexpr (sizeof(std::size_t) < sizeof(U)) {
        return static_cast<std::size_t>(v);
    } else {
        return v;
    }
}

// Reduced check of x * x (one object on both sides, so the squaring path): residue, and the FFT kernel (64-bit limbs).
[[maybe_unused]] void check_square_cheap(const std::size_t n, const std::uint64_t seed = 1) {
    SCOPED_TRACE("cheap square n=" + std::to_string(n));
    if (n < 2) {
        return;
    }
    const limbs_t v   = make_random(n, seed * 7 + 5);
    const limbs_t got = run(&runtime_any, v, v);
    for (const uint_t p : {prime_1, prime_2}) {
        const uint_t r = residue(v, p);
        EXPECT_EQ(mulmod(r, r, p), residue(std::span<const uint_t>{got}, p)) << "residue mismatch mod " << p;
    }
    if constexpr (limb_bits == 64) {
        EXPECT_EQ(trimmed(got), trimmed(fft_reference(v, v))) << "differs from the FFT kernel";
    }
}

// The slicing ratio for a shorter operand of m limbs, from the dispatcher's own helper.
std::pair<std::size_t, std::size_t> zone_ratio(const std::size_t m) {
    const auto r = detail::mul_slice_zone_ratio(m);
    return {to_size(r.num), to_size(r.den)};
}

std::size_t ceil_ratio(const std::size_t m, const std::size_t num, const std::size_t den) {
    return (m * num + den - 1) / den;
}

// Gate values to build shapes around; the FFT floor is left out of the (expensive) quotient grid.
std::vector<std::size_t> gates(const bool with_fft_floor = true) {
    std::vector<std::size_t> g{detail::karatsuba_cutoff,
                               detail::toom_cook_3_cutoff,
                               detail::toom_cook_4_cutoff,
                               detail::toom_cook_6_5_cutoff,
                               detail::toom_cook_8_5_cutoff};
    if constexpr (limb_bits == 64) {
        if (with_fft_floor) {
            g.push_back(detail::fft_mul_min_limbs); // the work cap prunes large floors (e.g. IFMA's)
        }
    }
    for (std::size_t i = 0; i < detail::mul_slice_zone_count; ++i) {
        g.push_back(to_size(detail::mul_slice_zones[i].min_limbs)); // every slicing zone boundary
    }
    std::sort(g.begin(), g.end());
    g.erase(std::unique(g.begin(), g.end()), g.end());
    return g;
}

} // namespace

// The dispatcher constants relate as the design assumes.
TEST(MultiplicationUnbalanced, ConstantsAreConsistent) {
    // FFT cost-model gate: never below the floors; with the model off (den == 0) the floor alone decides.
    const std::size_t ff = detail::fft_mul_min_limbs;
    const std::size_t sf = detail::square_fft_min_limbs;
    EXPECT_LE(detail::karatsuba_cutoff, ff);
    EXPECT_FALSE(detail::fft_mul_worthwhile(ff - 1, 4 * ff));
    EXPECT_FALSE(detail::square_fft_worthwhile(sf - 1));
    if constexpr (detail::fft_mul_model_den == 0) {
        EXPECT_TRUE(detail::fft_mul_worthwhile(ff, ff));
        EXPECT_TRUE(detail::fft_mul_worthwhile(ff, 3 * ff));
    }
    if constexpr (detail::square_fft_model_den == 0) {
        EXPECT_TRUE(detail::square_fft_worthwhile(sf));
    }
    for (std::uint64_t x = 0; x < 5000; ++x) {
        const std::uint64_t r = detail::isqrt_floor(x);
        EXPECT_LE(r * r, x);
        EXPECT_GT((r + 1) * (r + 1), x);
    }
    // The slicing entry test flips exactly at ceil(min * num / den) in the Karatsuba zone.
    const std::size_t kc          = detail::karatsuba_cutoff;
    const auto        kr          = detail::mul_slice_zone_ratio(kc);
    const std::size_t slice_entry = to_size((kc * kr.num + kr.den - 1) / kr.den);
    EXPECT_TRUE(detail::mul_should_slice(kc, slice_entry));
    EXPECT_FALSE(detail::mul_should_slice(kc, slice_entry - 1));
    EXPECT_FALSE(detail::mul_should_slice(kc, kc));
    // toom_cook_3_refuses_shape is the kernel's own test (min <= 2 * ceil(max / 3)); recompute it independently, and
    // check that the Toom zones slice every refused shape while the Karatsuba zone follows the ratio test alone.
    for (const std::size_t m :
         {detail::toom_cook_3_cutoff, detail::toom_cook_3_cutoff + 1, detail::toom_cook_6_5_cutoff}) {
        for (std::size_t n = m; n <= 2 * m; ++n) {
            const std::size_t k = (n + 2) / 3;
            EXPECT_EQ(detail::toom_cook_3_refuses_shape(m, n), m <= 2 * k) << m << "x" << n;
            if (m <= 2 * k) {
                EXPECT_TRUE(detail::mul_should_slice(m, n)) << m << "x" << n;
            }
        }
    }
    for (std::size_t n = kc; kc < detail::toom_cook_3_cutoff && n <= 2 * kc; ++n) {
        EXPECT_EQ(detail::mul_should_slice(kc, n), n >= slice_entry) << kc << "x" << n;
    }
    // The slicing zone table: strictly sorted, the first zone at karatsuba_cutoff, every ratio at or above 9/8, and
    // the lookup returns each zone's own ratio from its first limb count up to the next zone's.
    EXPECT_GE(detail::mul_slice_zone_count, 1u);
    EXPECT_LE(detail::mul_slice_zone_count, detail::mul_slice_zones.size());
    EXPECT_EQ(to_size(detail::mul_slice_zones[0].min_limbs), detail::karatsuba_cutoff);
    for (std::size_t i = 0; i < detail::mul_slice_zone_count; ++i) {
        const auto& z = detail::mul_slice_zones[i];
        EXPECT_GE(z.num * 8, z.den * 9) << "zone " << i;
        const std::size_t lo = to_size(z.min_limbs);
        EXPECT_EQ(detail::mul_slice_zone_ratio(lo).num, z.num) << "zone " << i;
        EXPECT_EQ(detail::mul_slice_zone_ratio(lo).den, z.den) << "zone " << i;
        if (i > 0) {
            EXPECT_LT(detail::mul_slice_zones[i - 1].min_limbs, z.min_limbs) << "zone " << i;
            const auto below = detail::mul_slice_zone_ratio(lo - 1);
            EXPECT_EQ(below.num, detail::mul_slice_zones[i - 1].num) << "zone " << i;
            EXPECT_EQ(below.den, detail::mul_slice_zones[i - 1].den) << "zone " << i;
        }
        if (i + 1 < detail::mul_slice_zone_count) {
            const auto top = detail::mul_slice_zone_ratio(to_size(detail::mul_slice_zones[i + 1].min_limbs) - 1);
            EXPECT_EQ(top.num, z.num) << "zone " << i;
            EXPECT_EQ(top.den, z.den) << "zone " << i;
        }
    }
    // The scratch model must bound what each zone's kernel asks for.
    EXPECT_EQ(detail::toom_ladder_storage_size(detail::karatsuba_cutoff, 1000), detail::karatsuba_storage_size(1000));
    EXPECT_EQ(detail::toom_ladder_storage_size(detail::toom_cook_8_5_cutoff, 1000),
              detail::toom_cook_8_5_storage_size(1000));
}

// G-1, G, G+1 for every gate, with the long operand straddling the zone's slicing ratio.
TEST(MultiplicationUnbalanced, GateAndRatioBoundaries) {
    for (const std::size_t g : gates()) {
        for (const std::size_t m : {g - 1, g, g + 1}) {
            const auto [num, den] = zone_ratio(m);
            const std::size_t rm  = ceil_ratio(m, num, den);
            for (const std::size_t n : {m, m + 1, rm - 1, rm, rm + 1, 2 * m, 3 * m + 1}) {
                check_shape(m, n);
            }
        }
    }
}

// n = q*m + t: whole pieces plus a tail that is empty, tiny, just below or at (R-1)m, or a nearly full piece.
TEST(MultiplicationUnbalanced, QuotientAndTailGrid) {
    for (const std::size_t g : gates(false)) {
        const std::size_t m           = g;
        const auto [num, den]         = zone_ratio(m);
        const std::size_t r_minus_1_m = ceil_ratio(m, num - den, den);
        for (const std::size_t q : {1u, 2u, 3u, 4u, 8u, 16u, 64u}) {
            for (const std::size_t t : {std::size_t{0}, std::size_t{1}, r_minus_1_m - 1, r_minus_1_m, m - 1}) {
                check_shape(m, q * m + t, q * 31 + t);
            }
        }
    }
}

// The shape gates themselves, exempt from the work cap and checked with the cheap references only (residue and the
// FFT kernel), with every shape found through the real gate (fft_mul_worthwhile / square_fft_worthwhile) so they
// track retuning. Each search starts from a shape the gate REFUSES, so both sides are checked:
//   (a) the floor: the shortest max at which min = floor is taken, and the same max one limb below the floor;
//   (b) where the model is on: the first balanced m x m the gate refuses (at or above the floor), the last max it
//       refuses and the first max it takes for that m (an unbalanced shape taken below the balanced crossover);
//   (c) squares: the floor and, model on, the first refused n and the first taken n above it;
//   plus one sliced shape in each of the Toom-6.5 and Toom-8.5 zones.
// Shapes above 100000 limbs (e.g. the IFMA floor of 400000) are skipped as too slow under sanitizers. A shape that
// the dispatcher routes to the FFT is checked only by the residue: the FFT reference is the same kernel there.
TEST(MultiplicationUnbalanced, ShapeGatesCheap) {
    constexpr std::size_t gate_limb_cap = 100'000;
    if constexpr (limb_bits == 64) {
        const std::size_t f = detail::fft_mul_min_limbs;
        // First max in [m, 64 m] (about 1/16 steps) for which the gate takes m x max, and the step before it.
        const auto first_taken = [](const std::size_t m) {
            std::pair<std::size_t, std::size_t> r{0, 0}; // {previous refused max, first accepted max}
            for (std::size_t n = m; n <= 64 * m; n += n / 16 + 1) {
                if (detail::fft_mul_worthwhile(m, n)) {
                    r.second = n;
                    return r;
                }
                r.first = n;
            }
            return r;
        };
        {
            const auto [prev, n] = first_taken(f);
            if (n != 0 && f + n <= gate_limb_cap) {
                check_cheap(f, n);
                check_cheap(f - 1, n);
            }
        }
        // (b) where the model is on: the first balanced shape the gate refuses (it must exist), the unbalanced
        // shapes just either side of the model boundary for the smallest m whose max is taken within 64 m, and the
        // balanced crossover itself. On some configurations (x86-64) the gate only engages at large sizes, so each
        // search is bounded by the test's work budget and a shape beyond it is skipped rather than assumed.
        std::size_t refused_m = 0;
        for (std::size_t m = f; m <= 8 * f; m += m / 8 + 1) {
            if (!detail::fft_mul_worthwhile(m, m)) {
                refused_m = m;
                break;
            }
        }
        if constexpr (detail::fft_mul_model_den != 0) {
            EXPECT_NE(refused_m, 0u) << "the model refuses no balanced shape in [floor, 8 * floor]";
        }
        for (std::size_t m = f; m <= 64 * f; m += m / 8 + 1) {
            const auto [prev, n] = first_taken(m);
            if (n == 0) {
                continue;
            }
            if (m + n <= gate_limb_cap) {
                if (prev >= m) {
                    EXPECT_FALSE(detail::fft_mul_worthwhile(m, prev));
                    check_cheap(m, prev);
                }
                check_cheap(m, n);
            }
            break;
        }
        // The balanced crossover: the last refused and the first accepted balanced size (about 1/16 steps).
        {
            constexpr std::size_t balanced_cap = 200'000; // total limbs; above 100000 only the residue is checked
            std::size_t           refused_bal  = 0;
            for (std::size_t m = f; 2 * m <= balanced_cap; m += m / 16 + 1) {
                if (detail::fft_mul_worthwhile(m, m)) {
                    if (refused_bal != 0) {
                        EXPECT_FALSE(detail::fft_mul_worthwhile(refused_bal, refused_bal));
                        check_cheap(refused_bal, refused_bal, 1, 2 * refused_bal <= gate_limb_cap);
                    }
                    check_cheap(m, m, 1, 2 * m <= gate_limb_cap);
                    break;
                }
                refused_bal = m;
            }
        }
        // (c) squares.
        constexpr std::size_t sf = detail::square_fft_min_limbs;
        if constexpr (sf <= gate_limb_cap) {
            check_square_cheap(sf);
            check_square_cheap(sf - 1);
            if constexpr (detail::square_fft_model_den != 0) {
                std::size_t refused = 0;
                for (std::size_t n = sf; n <= 8 * sf; n += n / 16 + 1) {
                    if (!detail::square_fft_worthwhile(n)) {
                        refused = n;
                        break;
                    }
                }
                EXPECT_NE(refused, 0u) << "the square model refuses no n in [floor, 8 * floor]";
                for (std::size_t n = refused; refused != 0 && n <= 8 * sf && n <= gate_limb_cap; n += n / 16 + 1) {
                    if (detail::square_fft_worthwhile(n)) {
                        check_square_cheap(refused);
                        check_square_cheap(n);
                        break;
                    }
                }
            }
        }
    }
    for (const std::size_t c : {detail::toom_cook_6_5_cutoff, detail::toom_cook_8_5_cutoff}) {
        if (c > gate_limb_cap / 2) {
            continue;
        }
        const auto [num, den] = zone_ratio(c);
        // Enters slicing and leaves at least one whole piece plus a tail.
        std::size_t n = ceil_ratio(c, num, den) + c + 3;
        if constexpr (limb_bits == 64) {
            // Keep it out of the FFT gate: fall back to the bare entry ratio, or skip the shape.
            if (detail::fft_mul_worthwhile(c, n)) {
                n = ceil_ratio(c, num, den);
                if (detail::fft_mul_worthwhile(c, n)) {
                    continue;
                }
            }
        }
        check_cheap(c, n);
    }
}

// Slicing whose full pieces the FFT gate accepts: (m, n) with mul_should_slice true, the whole product refused by the
// gate, but the balanced m x m piece product accepted, so multiply_sliced sends its pieces to the FFT. Found through
// the gates within the work budget. With the model off, slicing is only entered below the floor and no such shape
// exists; where the model engages only at large sizes (x86-64) the shape may lie beyond the budget. Existence is
// therefore required only on AArch64, where the model was fitted from small sizes, and otherwise the test is a no-op.
TEST(MultiplicationUnbalanced, SlicedPiecesTakeFft) {
    if constexpr (limb_bits == 64) {
        constexpr std::size_t cap   = 100'000;
        bool                  found = false;
        for (std::size_t m = detail::fft_mul_min_limbs; 2 * m < cap && !found; m += m / 16 + 1) {
            if (!detail::fft_mul_worthwhile(m, m)) {
                continue;
            }
            for (std::size_t n = m + m / 2; n <= 64 * m && m + n <= cap; n += n / 16 + 1) {
                if (detail::mul_should_slice(m, n) && !detail::fft_mul_worthwhile(m, n)) {
                    check_cheap(m, n);
                    check_cheap(m, n + 1);
                    found = true;
                    break;
                }
            }
        }
#if defined(BEMAN_BIG_INT_ARCH_AARCH64)
        EXPECT_TRUE(found) << "no shape found whose sliced pieces the FFT gate accepts";
#else
        if (!found) {
            GTEST_SKIP() << "no shape within the work budget has sliced pieces that the FFT gate accepts";
        }
#endif
    }
}

// fft_model_worthwhile against an exact big_int recomputation, with the AArch64 constant pairs and others, over a
// grid of power-of-two lengths and operand sizes that reaches the 64-bit overflow region (config independent).
TEST(MultiplicationUnbalanced, FftModelWorthwhileIsExact) {
    using big                                              = ::beman::big_int::big_int;
    constexpr std::uint64_t                       u64_max  = std::numeric_limits<std::uint64_t>::max();
    const std::pair<std::uint64_t, std::uint64_t> ratios[] = {
        {13, 16}, {5, 8}, {7, 10}, {7, 16}, {1, 1}, {3, 1}, {1, 7}};
    const std::uint64_t sizes[] = {1,
                                   2,
                                   3,
                                   15,
                                   16,
                                   17,
                                   1000,
                                   4499,
                                   4500,
                                   65535,
                                   std::uint64_t{1} << 20,
                                   (std::uint64_t{1} << 32) - 1,
                                   std::uint64_t{1} << 32,
                                   (std::uint64_t{1} << 40) + 123,
                                   std::uint64_t{1} << 53,
                                   std::uint64_t{1} << 62,
                                   u64_max - 1,
                                   u64_max};
    for (unsigned shift = 0; shift < 63; ++shift) {
        const std::uint64_t length = std::uint64_t{1} << shift;
        const std::uint64_t k      = shift; // log2 of a power of two
        for (const auto& [num, den] : ratios) {
            const big lhs = big{length} * big{k} * big{den};
            for (const std::uint64_t max_size : sizes) {
                for (const std::uint64_t min_size : sizes) {
                    if (min_size > max_size) {
                        continue;
                    }
                    const std::uint64_t root = detail::isqrt_floor(min_size);
                    const big           rhs  = big{num} * big{max_size} * big{root};
                    EXPECT_EQ(detail::fft_model_worthwhile(length, num, den, max_size, min_size), lhs <= rhs)
                        << "L=" << length << " " << num << "/" << den << " max=" << max_size << " min=" << min_size;
                }
            }
        }
    }
    // The root itself is exact at the overflow-adjacent sizes: root^2 <= x < (root + 1)^2.
    for (const std::uint64_t x : sizes) {
        const big root{detail::isqrt_floor(x)};
        EXPECT_TRUE(root * root <= big{x} && big{x} < (root + 1) * (root + 1)) << x;
    }
}

// The rounding sliver just below 3:2: Toom-3's k = ceil(max / 3) makes it refuse max = 3j + 1 .. 3j + 3 with
// j = ceil(min / 2) - 1 (3j is still accepted), so these shapes slice through the Toom-3 refusal term whatever the
// zone ratio is.
TEST(MultiplicationUnbalanced, ToomRefusalSliver) {
    for (const std::size_t g :
         {detail::toom_cook_3_cutoff, detail::toom_cook_4_cutoff, detail::toom_cook_6_5_cutoff}) {
        for (const std::size_t m : {g, g + 1}) {
            const std::size_t j = (m + 1) / 2 - 1;
            for (const std::size_t n : {3 * j, 3 * j + 1, 3 * j + 2, 3 * j + 3}) {
                if (n < m) {
                    continue;
                }
                if (n > 3 * j) {
                    EXPECT_TRUE(detail::mul_should_slice(m, n)) << m << "x" << n;
                }
                if (work_ok(m, n)) {
                    check_shape(m, n, n);
                } else {
                    check_cheap(m, n, n);
                }
            }
        }
    }
}

// Fibonacci-proportioned shapes: every slice leaves a tail that slices the other way, nesting as deep as the
// ratios allow.
TEST(MultiplicationUnbalanced, FibonacciNesting) {
    for (const std::size_t unit : {detail::karatsuba_cutoff, detail::toom_cook_3_cutoff / 2 + 3}) {
        std::size_t f0 = 1;
        std::size_t f1 = 2;
        for (int k = 0; k < 14; ++k) {
            check_shape(f0 * unit, f1 * unit, static_cast<std::uint64_t>(k));
            check_shape(f0 * unit + 1, f1 * unit - 1, static_cast<std::uint64_t>(k) + 100);
            const std::size_t f2 = f0 + f1;
            f0                   = f1;
            f1                   = f2;
        }
    }
}

// Every product with max > min is forced through the slicer, including 2:1 to 9:1 at the smallest sizes.
TEST(MultiplicationUnbalanced, ForcedSlicingSmallShapes) {
    const std::size_t m = detail::karatsuba_cutoff;
    for (std::size_t n = m + 1; n <= 9 * m + 3; n += m / 3 + 1) {
        check_shape(m, n, n);
    }
}

namespace {

// Long/short buffers for the data edge cases: m and n straddle several pieces plus a real tail.
struct edge_shape {
    std::size_t m = detail::toom_cook_3_cutoff + 5;
    std::size_t n = 5 * (detail::toom_cook_3_cutoff + 5) + (detail::toom_cook_3_cutoff + 5) / 3;
};

} // namespace

TEST(MultiplicationUnbalanced, ZeroAndSparsePieces) {
    const edge_shape s;
    {
        // An all-zero middle piece.
        limbs_t lng = make_random(s.n, 11);
        std::fill(lng.begin() + static_cast<std::ptrdiff_t>(2 * s.m),
                  lng.begin() + static_cast<std::ptrdiff_t>(3 * s.m),
                  uint_t{0});
        check_product(lng, make_random(s.m, 12));
    }
    {
        // x * B^m: the low piece is zero.
        limbs_t lng = make_random(s.n, 13);
        std::fill(lng.begin(), lng.begin() + static_cast<std::ptrdiff_t>(s.m), uint_t{0});
        check_product(lng, make_random(s.m, 14));
    }
    {
        // A piece that trims to one limb, and one with only its top limb set.
        limbs_t lng = make_random(s.n, 15);
        std::fill(lng.begin() + static_cast<std::ptrdiff_t>(s.m),
                  lng.begin() + static_cast<std::ptrdiff_t>(2 * s.m),
                  uint_t{0});
        lng[s.m] = 12345;
        std::fill(lng.begin() + static_cast<std::ptrdiff_t>(3 * s.m),
                  lng.begin() + static_cast<std::ptrdiff_t>(4 * s.m),
                  uint_t{0});
        lng[4 * s.m - 1] = 99;
        check_product(lng, make_random(s.m, 16));
    }
    {
        // Sparse long operand: a single set limb per piece boundary.
        limbs_t lng(s.n, uint_t{0});
        for (std::size_t i = 0; i < s.n; i += s.m) {
            lng[i] = static_cast<uint_t>(i + 1);
        }
        lng.back() = 7;
        check_product(lng, make_random(s.m, 17));
    }
}

TEST(MultiplicationUnbalanced, AllOnes) {
    // (B^n - 1)(B^m - 1) carries through every window (the no-carry assertion in Debug builds).
    const edge_shape s;
    const limbs_t    lng(s.n, ~uint_t{0});
    const limbs_t    sht(s.m, ~uint_t{0});
    check_product(lng, sht);
    check_product(lng, make_random(s.m, 21));
    check_product(make_random(s.n, 22), sht);
}

TEST(MultiplicationUnbalanced, PowersOfTwoAndNeighbours) {
    const edge_shape s;
    // Build each operand mutable, then freeze it: 2^k, 2^k + 1 and 2^k - 1.
    const auto make_pow = [](const std::size_t n, const int kind) {
        limbs_t v(n, uint_t{0});
        if (kind == 0) {
            v.back() = uint_t{1} << 5;
        } else if (kind == 1) {
            v.back() = uint_t{1} << 5;
            v[0]     = 1;
        } else {
            std::fill(v.begin(), v.end(), ~uint_t{0});
            v.back() = (uint_t{1} << 5) - 1;
        }
        return v;
    };
    const limbs_t pow_long    = make_pow(s.n, 0);
    const limbs_t plus_long   = make_pow(s.n, 1);
    const limbs_t minus_long  = make_pow(s.n, 2);
    const limbs_t pow_short   = make_pow(s.m, 0);
    const limbs_t plus_short  = make_pow(s.m, 1);
    const limbs_t minus_short = make_pow(s.m, 2);

    const limbs_t rnd_long  = make_random(s.n, 31);
    const limbs_t rnd_short = make_random(s.m, 32);
    for (const limbs_t* l : {&pow_long, &plus_long, &minus_long, &rnd_long}) {
        for (const limbs_t* sh : {&pow_short, &plus_short, &minus_short, &rnd_short}) {
            check_product(*l, *sh);
        }
    }
}

TEST(MultiplicationUnbalanced, ShortOperandIsPrefixOfLong) {
    const edge_shape s;
    const limbs_t    lng = make_random(s.n, 41);
    check_product(lng, std::span<const uint_t>{lng}.first(s.m));
    check_product(lng, std::span<const uint_t>{lng}.last(s.m));
}

TEST(MultiplicationUnbalanced, UntrimmedOperands) {
    const edge_shape s;
    limbs_t          lng      = make_random(s.n, 51);
    limbs_t          sht      = make_random(s.m, 52);
    const limbs_t    lng_trim = lng;
    const limbs_t    sht_trim = sht;
    lng.insert(lng.end(), 3, uint_t{0});
    sht.insert(sht.end(), 1, uint_t{0});
    const limbs_t got = run(&runtime_any, lng, sht);
    EXPECT_EQ(trimmed(got), trimmed(run(&runtime_any, lng_trim, sht_trim)));
    EXPECT_EQ(trimmed(run(&runtime_any, sht, lng)), trimmed(got));
    // Zero-extended, both untrimmed and reversed.
    check_product(lng, sht);
}

namespace {

// Divides a by b with the public operators and checks q*b + r == a and 0 <= r < b.
void check_division(const std::size_t la, const std::size_t lb, const std::uint64_t seed) {
    SCOPED_TRACE("dividend limbs=" + std::to_string(la) + " divisor limbs=" + std::to_string(lb));
    const limbs_t                   va = make_random(la, seed);
    const limbs_t                   vb = make_random(lb, seed + 1);
    const ::beman::big_int::big_int a(va.begin(), va.end());
    const ::beman::big_int::big_int b(vb.begin(), vb.end());
    const auto                      q = a / b;
    const auto                      r = a % b;
    EXPECT_EQ(q * b + r, a);
    EXPECT_TRUE(r >= 0 && r < b);
}

} // namespace

// Division shapes that reach the Barrett tier (divide_dispatch): (16 s + 5, s) with s at least barrett_march_cutoff
// takes the full-depth march, and (8 m8 + 7, m8) at barrett_march8_cutoff takes the half-depth one. The march's
// per-block products are balanced; the reciprocal's Newton steps (src/divide.cpp) are about 2:1 and slice.
TEST(MultiplicationUnbalanced, DivisionShapes) {
    const std::size_t s = std::max(2 * detail::toom_cook_3_cutoff + 33, detail::barrett_march_cutoff);
    check_division(16 * s + 5, s, 61);
    const std::size_t m8 = detail::barrett_march8_cutoff;
    check_division(8 * m8 + 7, m8, 63);
}

// A few end-to-end signed operator* cases against Boost cpp_int.
TEST(MultiplicationUnbalanced, OperatorAgainstCppInt) {
    const std::size_t m = detail::toom_cook_3_cutoff + 7;
    for (const std::size_t n : {m + m / 2, 2 * m, 5 * m + 3, 9 * m}) {
        const std::string a = bmp::random_big_int(n * limb_bits, /*negative=*/true);
        const std::string b = bmp::random_big_int(m * limb_bits, /*negative=*/false);
        EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
        EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, b, a));
        const std::string c = bmp::random_big_int(m * limb_bits, /*negative=*/true);
        EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, c));
    }
}
