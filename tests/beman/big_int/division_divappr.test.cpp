// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises divide_unsigned_approx against the exact schoolbook kernel:
// the approximate quotient must satisfy q <= q' <= q + 1 on every shape,
// including the patterns that stress the truncated-divisor slack (zero low
// divisor halves), the saturated 3-by-2 fast path (top window pairs equal
// to the divisor's), exact multiples, and near-misses on both sides.

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

constexpr uint_t limb_max = std::numeric_limits<uint_t>::max();

using scratch_for_test = detail::scratch_allocator<std::allocator<uint_t>>;

std::vector<uint_t> random_limbs(const std::size_t size, std::mt19937_64& rng) {
    std::vector<uint_t> v(size);
    for (auto& limb : v) {
        limb = static_cast<uint_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// q' - q must be 0 or 1; returns the difference so sweeps can confirm the
// +1 case actually occurs.
std::size_t check_divappr(const std::vector<uint_t>& dividend, const std::vector<uint_t>& divisor) {
    const std::size_t m = dividend.size();
    const std::size_t s = divisor.size();
    EXPECT_GE(s, 3u);
    EXPECT_GT(m, s);

    std::allocator<uint_t> alloc;

    std::vector<uint_t> q_ref(m - s + 1, 0);
    std::vector<uint_t> r_ref(m + 1, 0);
    {
        scratch_for_test scratch(detail::divide_unsigned_storage_size(m, s), alloc);
        detail::divide_unsigned(std::span<uint_t>{q_ref},
                                std::span<uint_t>{r_ref},
                                std::span<const uint_t>{dividend},
                                std::span<const uint_t>{divisor},
                                scratch);
    }

    std::vector<uint_t> q_apx(m - s + 1, 0);
    {
        scratch_for_test scratch(detail::divide_unsigned_storage_size(m, s), alloc);
        detail::divide_unsigned_approx(
            std::span<uint_t>{q_apx}, std::span<const uint_t>{dividend}, std::span<const uint_t>{divisor}, scratch);
    }

    // diff = q' - q, which must not borrow and must be at most 1.
    std::vector<uint_t> diff   = q_apx;
    const bool          borrow = detail::subtract_unsigned_spans_borrow_out(
        std::span<uint_t>{diff}, std::span<const uint_t>{diff}, std::span<const uint_t>{q_ref});
    EXPECT_FALSE(borrow) << "approximate quotient below the true quotient at m=" << m << " s=" << s;
    if (detail::is_span_zero(std::span<const uint_t>{diff})) {
        return 0;
    }
    const std::size_t diff_size = detail::trimmed_size_span(std::span<const uint_t>{diff});
    EXPECT_EQ(diff_size, 1u) << "m=" << m << " s=" << s;
    EXPECT_EQ(diff[0], 1u) << "m=" << m << " s=" << s;
    return static_cast<std::size_t>(diff[0]);
}

TEST(DivisionDivappr, RandomShapeSweep) {
    // For random operands the +1 slack fires with probability ~1/B per
    // division, so this sweep checks only the bound; ConstructedPlusOne
    // below pins the slack deterministically.
    std::mt19937_64 rng{0x5a1u};
    for (std::size_t s = 3; s <= 24; ++s) {
        for (std::size_t extra = 1; extra <= 40; extra += (extra < 8 ? 1 : 5)) {
            for (int trial = 0; trial < 20; ++trial) {
                check_divappr(random_limbs(s + extra, rng), random_limbs(s, rng));
            }
        }
    }
}

TEST(DivisionDivappr, ConstructedPlusOne) {
    // d = (d2, d1, d0) = (2^63, 0, B - 1), N = 3d - 1: the true quotient is
    // 2, but the chain that drops d0 and the bottom dividend limb computes
    // floor(floor((3d - 1)/B) / (d2 B + d1)) = floor((3 d_eff + 2)/d_eff)
    // = 3. This is the +1 slack made deterministic.
    constexpr uint_t    top_bit = uint_t{1} << (detail::width_v<uint_t> - 1);
    std::vector<uint_t> d{limb_max, 0, top_bit};

    std::vector<uint_t>    n3d(4, 0); // 3 * d
    std::allocator<uint_t> alloc;
    detail::multiply_dispatch(
        std::span<uint_t>{n3d}, std::span<const uint_t>{std::vector<uint_t>{3}}, std::span<const uint_t>{d}, alloc);
    detail::subtract_unsigned_spans(
        std::span<uint_t>{n3d}, std::span<const uint_t>{n3d}, std::span<const uint_t>{std::vector<uint_t>{1}});
    ASSERT_EQ(detail::trimmed_size_span(std::span<const uint_t>{n3d}), 4u);

    EXPECT_EQ(check_divappr(n3d, d), 1u);
}

TEST(DivisionDivappr, LargerShapes) {
    std::mt19937_64 rng{0x5a2u};
    for (const auto& [s, m] : {std::pair<std::size_t, std::size_t>{50, 100},
                               {100, 173},
                               {128, 1024},
                               {300, 421},
                               {500, 1000},
                               {37, 2000}}) {
        for (int trial = 0; trial < 4; ++trial) {
            check_divappr(random_limbs(m, rng), random_limbs(s, rng));
        }
    }
}

TEST(DivisionDivappr, AdversarialPatterns) {
    std::mt19937_64 rng{0x5a3u};
    for (const std::size_t s : {std::size_t{3}, std::size_t{5}, std::size_t{9}, std::size_t{16}}) {
        for (const std::size_t m : {s + 1, s + 2, 2 * s, 3 * s + 1}) {
            // All-ones everything: every digit saturates.
            check_divappr(std::vector<uint_t>(m, limb_max), std::vector<uint_t>(s, limb_max));
            // Divisor with a zero low half: the truncated limbs are the ones
            // that matter most.
            {
                std::vector<uint_t> d(s, 0);
                for (std::size_t i = s / 2; i < s; ++i) {
                    d[i] = static_cast<uint_t>(rng());
                }
                if (d.back() == 0) {
                    d.back() = 1;
                }
                check_divappr(random_limbs(m, rng), d);
                check_divappr(std::vector<uint_t>(m, limb_max), d);
            }
            // Divisor just above a power of two and dividend saturated: long
            // runs of (d1, d0) pairs in the windows.
            {
                std::vector<uint_t> d(s, 0);
                d.back() = uint_t{1} << (BEMAN_BIG_INT_NAMESPACE::detail::width_v<uint_t> - 1);
                d[0]     = 1;
                check_divappr(std::vector<uint_t>(m, limb_max), d);
            }
        }
    }
}

TEST(DivisionDivappr, SparseDivisorTruncationStress) {
    // Regression class for a tuning bug: with the divisor truncated (the
    // quotient much shorter than the divisor) and zero limbs scattered
    // through the divisor, keeping only quotient-digits + 1 limbs lets the
    // accumulated tail slack reach +2. The kernel must keep one more.
    std::mt19937_64 rng{0x5a5u};
    for (std::size_t s = 8; s <= 32; s += 4) {
        for (std::size_t extra = 2; extra <= 8; extra += 3) {
            for (int trial = 0; trial < 400; ++trial) {
                std::vector<uint_t> d(s);
                for (auto& limb : d) {
                    limb = (rng() & 1u) ? 0 : static_cast<uint_t>(rng());
                }
                if (d.back() == 0) {
                    d.back() = 1 + static_cast<uint_t>(rng() >> 1);
                }
                check_divappr(random_limbs(s + extra, rng), d);
            }
        }
    }
}

TEST(DivisionDivappr, ExactMultiplesAndNeighbors) {
    std::mt19937_64        rng{0x5a4u};
    std::allocator<uint_t> alloc;
    for (const std::size_t s : {std::size_t{3}, std::size_t{7}, std::size_t{20}, std::size_t{64}}) {
        for (const std::size_t qn : {std::size_t{2}, std::size_t{5}, std::size_t{3 * s}}) {
            for (int trial = 0; trial < 6; ++trial) {
                const auto          d = random_limbs(s, rng);
                const auto          q = random_limbs(qn, rng);
                std::vector<uint_t> product(s + qn, 0);
                detail::multiply_dispatch(
                    std::span<uint_t>{product}, std::span<const uint_t>{q}, std::span<const uint_t>{d}, alloc);
                product.resize(detail::trimmed_size_span(std::span<const uint_t>{product}));
                if (product.size() <= s) {
                    continue;
                }
                // N = q * d exactly, then one below (q - 1 remainder d - 1)
                // and one above (remainder 1).
                check_divappr(product, d);
                std::vector<uint_t> above = product;
                if (!detail::add_unsigned_spans(std::span<uint_t>{above},
                                                std::span<const uint_t>{above},
                                                std::span<const uint_t>{std::vector<uint_t>{1}})) {
                    check_divappr(above, d);
                }
                std::vector<uint_t> below = product;
                detail::subtract_unsigned_spans(std::span<uint_t>{below},
                                                std::span<const uint_t>{below},
                                                std::span<const uint_t>{std::vector<uint_t>{1}});
                below.resize(detail::trimmed_size_span(std::span<const uint_t>{below}));
                if (below.size() > s) {
                    check_divappr(below, d);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// divide_dc_divappr: differential against the exact divide_dc_2n1n on the
// same window. The quotient must satisfy q <= q' <= q + slack.
// ---------------------------------------------------------------------------

// Runs both kernels on copies of `working` (2n limbs, high half < b) and
// returns q' - q.
std::size_t
check_dc_divappr(const std::vector<uint_t>& working, const std::vector<uint_t>& b, const std::size_t threshold) {
    const std::size_t n = b.size();
    EXPECT_EQ(working.size(), 2 * n);
    EXPECT_NE(b.back() >> (detail::width_v<uint_t> - 1), 0u);

    std::allocator<uint_t> alloc;
    const std::size_t      thr = threshold != 0 ? threshold : detail::burnikel_ziegler_cutoff;

    std::vector<uint_t> a_exact = working;
    std::vector<uint_t> q_exact(n, 0);
    {
        scratch_for_test scratch(detail::burnikel_ziegler_storage_size(n, 2, thr) + 8 * n + 64, alloc);
        detail::divide_dc_2n1n(
            std::span<uint_t>{a_exact}, std::span<const uint_t>{b}, std::span<uint_t>{q_exact}, scratch, thr);
    }

    std::vector<uint_t> a_appr = working;
    std::vector<uint_t> q_appr(n, 0);
    {
        scratch_for_test scratch(detail::burnikel_ziegler_storage_size(n, 2, thr) + 8 * n + 64, alloc);
        detail::divide_dc_divappr(
            std::span<uint_t>{a_appr}, std::span<const uint_t>{b}, std::span<uint_t>{q_appr}, scratch, thr);
    }

    std::vector<uint_t> diff   = q_appr;
    const bool          borrow = detail::subtract_unsigned_spans_borrow_out(
        std::span<uint_t>{diff}, std::span<const uint_t>{diff}, std::span<const uint_t>{q_exact});
    EXPECT_FALSE(borrow) << "approximate dc quotient below the true quotient at n=" << n << " thr=" << thr;
    if (detail::is_span_zero(std::span<const uint_t>{diff})) {
        return 0;
    }
    EXPECT_EQ(detail::trimmed_size_span(std::span<const uint_t>{diff}), 1u) << "n=" << n << " thr=" << thr;
    EXPECT_LE(diff[0], detail::divappr_quotient_slack(n, thr)) << "n=" << n << " thr=" << thr;
    return static_cast<std::size_t>(diff[0]);
}

// 2n-limb window with high half strictly below b.
std::vector<uint_t> random_window(const std::vector<uint_t>& b, std::mt19937_64& rng) {
    const std::size_t   n = b.size();
    std::vector<uint_t> w(2 * n);
    for (auto& limb : w) {
        limb = static_cast<uint_t>(rng());
    }
    while (detail::compare_unsigned_spans(std::span<const uint_t>{w.data() + n, n}, std::span<const uint_t>{b}) !=
           std::strong_ordering::less) {
        for (std::size_t i = n; i < 2 * n; ++i) {
            w[i] = static_cast<uint_t>(rng());
        }
    }
    return w;
}

std::vector<uint_t> random_normalized_divisor(const std::size_t n, std::mt19937_64& rng) {
    std::vector<uint_t> b(n);
    for (auto& limb : b) {
        limb = static_cast<uint_t>(rng());
    }
    b.back() |= uint_t{1} << (detail::width_v<uint_t> - 1);
    return b;
}

TEST(DivisionDcDivappr, RandomWindows) {
    std::mt19937_64 rng{0x5b1u};
    std::size_t     max_diff = 0;
    for (const std::size_t thr : {std::size_t{0}, std::size_t{2}, std::size_t{3}, std::size_t{5}}) {
        for (const std::size_t n : {std::size_t{4},
                                    std::size_t{6},
                                    std::size_t{8},
                                    std::size_t{12},
                                    std::size_t{16},
                                    std::size_t{24},
                                    std::size_t{48},
                                    std::size_t{64}}) {
            for (int trial = 0; trial < 25; ++trial) {
                const auto b = random_normalized_divisor(n, rng);
                max_diff     = std::max(max_diff, check_dc_divappr(random_window(b, rng), b, thr));
            }
        }
    }
    // Record-keeping expectation: the documented bound must hold for the
    // deepest shape in the sweep.
    EXPECT_LE(max_diff, detail::divappr_quotient_slack(64, 2));
}

TEST(DivisionDcDivappr, DeepRecursionAccumulation) {
    // Six halving levels at threshold 2: the level-by-level truncation
    // slack must stay within the documented constant rather than growing
    // with depth.
    std::mt19937_64 rng{0x5b2u};
    std::size_t     max_diff = 0;
    for (const std::size_t n : {std::size_t{64}, std::size_t{128}, std::size_t{192}}) {
        for (int trial = 0; trial < 30; ++trial) {
            const auto b = random_normalized_divisor(n, rng);
            max_diff     = std::max(max_diff, check_dc_divappr(random_window(b, rng), b, 2));
        }
    }
    EXPECT_LE(max_diff, detail::divappr_quotient_slack(192, 2));
    // The accumulation is real: deep recursion must actually exceed the
    // single-level slack, or the depth dependence is fiction.
    EXPECT_GT(max_diff, 2u);
}

TEST(DivisionDcDivappr, SaturationPatterns) {
    std::mt19937_64 rng{0x5b3u};
    std::size_t     max_diff = 0;
    for (const std::size_t thr : {std::size_t{2}, std::size_t{4}}) {
        for (const std::size_t n : {std::size_t{4}, std::size_t{8}, std::size_t{16}, std::size_t{32}}) {
            // Maximal window under a maximal divisor: high half = b - 1,
            // low half all-ones -- every level's estimate saturates.
            {
                std::vector<uint_t> b(n, limb_max);
                std::vector<uint_t> w(2 * n, limb_max);
                detail::subtract_unsigned_spans(std::span<uint_t>{w.data() + n, n},
                                                std::span<const uint_t>{w.data() + n, n},
                                                std::span<const uint_t>{std::vector<uint_t>{1}});
                max_diff = std::max(max_diff, check_dc_divappr(w, b, thr));
            }
            // Minimal normalized divisor (2^63 * B^(n-1)): quotients ride
            // the all-ones estimate path.
            {
                std::vector<uint_t> b(n, 0);
                b.back() = uint_t{1} << (detail::width_v<uint_t> - 1);
                std::vector<uint_t> w(2 * n, limb_max);
                for (std::size_t i = n; i < 2 * n; ++i) {
                    w[i] = static_cast<uint_t>(rng());
                }
                w[2 * n - 1] = b.back() - 1;
                max_diff     = std::max(max_diff, check_dc_divappr(w, b, thr));
            }
            // Divisor with an all-ones low half and minimal high half: the
            // inner window's high part can reach b1 exactly (the equality
            // saturation branch).
            {
                std::vector<uint_t> b(n, limb_max);
                for (std::size_t i = n / 2; i < n; ++i) {
                    b[i] = 0;
                }
                b.back() = uint_t{1} << (detail::width_v<uint_t> - 1);
                max_diff = std::max(max_diff, check_dc_divappr(random_window(b, rng), b, thr));
            }
        }
    }
    EXPECT_LE(max_diff, detail::divappr_quotient_slack(32, 2));
}

// ---------------------------------------------------------------------------
// divide_quotient: must equal the exact divmod quotient on every input.
// ---------------------------------------------------------------------------

void check_div_q(const std::vector<uint_t>& dividend,
                 const std::vector<uint_t>& divisor,
                 const std::size_t          threshold) {
    const std::size_t m = dividend.size();
    const std::size_t s = divisor.size();

    std::allocator<uint_t> alloc;

    std::vector<uint_t> q_ref(m - s + 1, 0);
    std::vector<uint_t> r_ref(m + 1, 0);
    detail::divide_burnikel_ziegler(std::span<uint_t>{q_ref},
                                    std::span<uint_t>{r_ref},
                                    std::span<const uint_t>{dividend},
                                    std::span<const uint_t>{divisor},
                                    alloc,
                                    threshold);

    std::vector<uint_t> q_only(m - s + 1, 0);
    detail::divide_quotient(std::span<uint_t>{q_only},
                            std::span<const uint_t>{dividend},
                            std::span<const uint_t>{divisor},
                            alloc,
                            threshold);

    EXPECT_EQ(detail::compare_unsigned_spans(std::span<const uint_t>{q_only}, std::span<const uint_t>{q_ref}),
              std::strong_ordering::equal)
        << "m=" << m << " s=" << s << " thr=" << threshold;
}

TEST(DivisionDivQ, MatchesDivmod) {
    std::mt19937_64 rng{0x5c1u};
    for (const std::size_t thr : {std::size_t{0}, std::size_t{2}, std::size_t{4}}) {
        for (std::size_t s = 2; s <= 20; s += 3) {
            for (std::size_t extra = 0; extra <= 36; extra += 4) {
                for (int trial = 0; trial < 12; ++trial) {
                    check_div_q(random_limbs(s + extra, rng), random_limbs(s, rng), thr);
                }
            }
        }
        check_div_q(random_limbs(700, rng), random_limbs(300, rng), thr);
        check_div_q(random_limbs(1024, rng), random_limbs(256, rng), thr);
        check_div_q(random_limbs(401, rng), random_limbs(400, rng), thr);
    }
}

TEST(DivisionDivQ, ExactMultiplesHitTheVerifyPath) {
    // N = q * D has a zero true fraction, so the ambiguous branch (one
    // multiply and compare) fires whenever the approximation is exact too;
    // the +-1 neighbors land on both sides of it.
    std::mt19937_64        rng{0x5c2u};
    std::allocator<uint_t> alloc;
    for (const std::size_t thr : {std::size_t{0}, std::size_t{3}}) {
        for (const std::size_t s : {std::size_t{4}, std::size_t{9}, std::size_t{50}, std::size_t{128}}) {
            for (const std::size_t qn : {std::size_t{1}, std::size_t{6}, std::size_t{2 * s}}) {
                for (int trial = 0; trial < 5; ++trial) {
                    const auto          d  = random_limbs(s, rng);
                    const auto          qv = random_limbs(qn, rng);
                    std::vector<uint_t> product(s + qn, 0);
                    detail::multiply_dispatch(
                        std::span<uint_t>{product}, std::span<const uint_t>{qv}, std::span<const uint_t>{d}, alloc);
                    product.resize(detail::trimmed_size_span(std::span<const uint_t>{product}));
                    if (product.size() < s) {
                        continue;
                    }
                    check_div_q(product, d, thr);
                    std::vector<uint_t> above = product;
                    above.push_back(0);
                    detail::add_unsigned_spans(std::span<uint_t>{above},
                                               std::span<const uint_t>{above},
                                               std::span<const uint_t>{std::vector<uint_t>{1}});
                    above.resize(detail::trimmed_size_span(std::span<const uint_t>{above}));
                    if (above.size() >= s) {
                        check_div_q(above, d, thr);
                    }
                    std::vector<uint_t> below = product;
                    detail::subtract_unsigned_spans(std::span<uint_t>{below},
                                                    std::span<const uint_t>{below},
                                                    std::span<const uint_t>{std::vector<uint_t>{1}});
                    below.resize(detail::trimmed_size_span(std::span<const uint_t>{below}));
                    if (below.size() >= s) {
                        check_div_q(below, d, thr);
                    }
                }
            }
        }
    }
}

TEST(DivisionDivQ, MaximalQuotientCorners) {
    // q = all-ones with the maximal remainder D - 1: the extended quotient
    // sits at B^(qn+1) - 1, the spill/saturation corner of the fraction
    // argument.
    std::allocator<uint_t> alloc;
    std::mt19937_64        rng{0x5c3u};
    for (const std::size_t s : {std::size_t{3}, std::size_t{8}, std::size_t{40}}) {
        for (const std::size_t qn : {std::size_t{1}, std::size_t{4}, std::size_t{17}}) {
            const auto          d = random_limbs(s, rng);
            std::vector<uint_t> q_target(qn, limb_max);
            std::vector<uint_t> n_max(s + qn + 1, 0);
            detail::multiply_dispatch(
                std::span<uint_t>{n_max}, std::span<const uint_t>{q_target}, std::span<const uint_t>{d}, alloc);
            // + (d - 1)
            std::vector<uint_t> d_minus_1 = d;
            detail::subtract_unsigned_spans(std::span<uint_t>{d_minus_1},
                                            std::span<const uint_t>{d_minus_1},
                                            std::span<const uint_t>{std::vector<uint_t>{1}});
            detail::add_unsigned_spans(
                std::span<uint_t>{n_max}, std::span<const uint_t>{n_max}, std::span<const uint_t>{d_minus_1});
            n_max.resize(detail::trimmed_size_span(std::span<const uint_t>{n_max}));
            if (n_max.size() >= s) {
                check_div_q(n_max, d, 0);
                check_div_q(n_max, d, 2);
            }
        }
    }
}

TEST(DivisionDivQ, OperatorSlashAgreesWithDivRem) {
    // Public-path check at sizes inside the divide-and-conquer band on
    // every architecture (s past the x86-64 gates), all sign combinations.
    using big_int = BEMAN_BIG_INT_NAMESPACE::big_int;
    std::mt19937_64 rng{0x5c4u};

    const auto make = [&](const std::size_t limbs, const bool negative) {
        big_int x = 1;
        for (std::size_t i = 0; i < limbs; ++i) {
            x <<= 64;
            x += rng();
        }
        return negative ? -x : x;
    };

    for (const bool a_neg : {false, true}) {
        for (const bool b_neg : {false, true}) {
            const big_int a  = make(620, a_neg);
            const big_int b  = make(200, b_neg);
            const auto    qr = div_rem_to_zero(a, b);
            EXPECT_EQ(a / b, qr.quotient);
            EXPECT_EQ(b / a, big_int{0});
            EXPECT_EQ((a / b) * b + a % b, a);
        }
    }
}

} // namespace
