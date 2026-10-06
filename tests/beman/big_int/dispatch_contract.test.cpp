// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Result-span contract of the multiply and divide dispatchers: they must produce the correct product, quotient and
// remainder into spans that are NOT pre-zeroed (callers may hand over uninitialized storage), and must leave limbs
// past the product alone. Every case runs a dispatcher into a poison-filled span and into a zeroed span, and the
// two must agree limb for limb; products are also checked against multiply_long where the work is small.

#include <beman/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace detail = ::BEMAN_BIG_INT_NAMESPACE::detail;

namespace {

using uint_t  = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using alloc_t = std::allocator<uint_t>;
using limbs_t = std::vector<uint_t>;
using cspan_t = std::span<const uint_t>;
using span_t  = std::span<uint_t>;

constexpr uint_t      poison       = static_cast<uint_t>(0xA5A5A5A5A5A5A5A5ULL);
constexpr uint_t      guard_value  = static_cast<uint_t>(0x5A5A5A5A5A5A5A5AULL);
constexpr std::size_t guard_limbs  = 3;
constexpr std::size_t limb_bits    = std::numeric_limits<uint_t>::digits;
constexpr std::size_t max_ref_work = 4'000'000;
constexpr std::size_t max_limbs    = 20'000;

limbs_t random_limbs(const std::size_t n, std::mt19937_64& rng) {
    limbs_t v(n);
    for (auto& x : v) {
        x = static_cast<uint_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// A power of two spanning `n` limbs: the single set bit sits in the top limb at position `bit`.
limbs_t power_of_two(const std::size_t n, const unsigned bit) {
    limbs_t v(n, uint_t{0});
    v.back() = uint_t{1} << (bit % limb_bits);
    return v;
}

// Calls `run(result, a, b)` into a poisoned and into a zeroed result of a.size() + b.size() limbs plus guards.
// Checks that the two agree, that guard limbs are untouched, that the returned size is the trimmed size, and
// (when affordable) that the product matches multiply_long.
template <class Run>
void check_product(const char* what, const cspan_t a, const cspan_t b, Run&& run) {
    const std::size_t total = a.size() + b.size();
    limbs_t           dirty(total + guard_limbs, poison);
    limbs_t           clean(total + guard_limbs, uint_t{0});
    std::fill(dirty.begin() + static_cast<std::ptrdiff_t>(total), dirty.end(), guard_value);
    std::fill(clean.begin() + static_cast<std::ptrdiff_t>(total), clean.end(), guard_value);

    const std::size_t n_dirty = run(span_t{dirty.data(), total}, a, b);
    const std::size_t n_clean = run(span_t{clean.data(), total}, a, b);

    for (std::size_t i = total; i < dirty.size(); ++i) {
        EXPECT_EQ(dirty[i], guard_value)
            << what << ": guard limb " << (i - total) << " overwritten, la=" << a.size() << " lb=" << b.size();
    }
    EXPECT_EQ(n_dirty, n_clean) << what << ": la=" << a.size() << " lb=" << b.size();
    EXPECT_EQ(n_dirty, detail::trimmed_size_span(cspan_t{dirty.data(), total}))
        << what << ": returned size is not the trimmed size, la=" << a.size() << " lb=" << b.size();
    const bool same = std::equal(dirty.begin(), dirty.begin() + static_cast<std::ptrdiff_t>(total), clean.begin());
    EXPECT_TRUE(same) << what << ": poisoned result differs from zeroed result, la=" << a.size() << " lb=" << b.size();

    if (a.size() * b.size() <= max_ref_work) {
        limbs_t ref(total, uint_t{0});
        detail::multiply_long(span_t{ref}, a, b);
        const bool matches = std::equal(ref.begin(), ref.end(), dirty.begin());
        EXPECT_TRUE(matches) << what << ": differs from multiply_long, la=" << a.size() << " lb=" << b.size();
    }
}

// Each runner returns the trimmed size and takes (result, a, b); `a` and `b` are trimmed with a.size() >= 2 where
// the runner is a direct tier entry point.
std::size_t run_dispatch(const span_t r, const cspan_t a, const cspan_t b) {
    alloc_t alloc;
    return detail::multiply_dispatch(r, a, b, alloc);
}

std::size_t run_square_dispatch(const span_t r, const cspan_t a, const cspan_t) {
    alloc_t alloc;
    return detail::square_dispatch(r, a, alloc);
}

std::size_t run_runtime(const span_t r, const cspan_t a, const cspan_t b) {
    alloc_t                                  alloc;
    const detail::scratch_allocator<alloc_t> hooks(alloc);
    return detail::multiply_runtime(r, a, b, hooks.heap());
}

std::size_t run_sliced(const span_t r, const cspan_t a, const cspan_t b) {
    alloc_t                                  alloc;
    const detail::scratch_allocator<alloc_t> hooks(alloc);
    return detail::multiply_runtime_sliced(r, a, b, hooks.heap());
}

std::size_t run_unsliced(const span_t r, const cspan_t a, const cspan_t b) {
    alloc_t                                  alloc;
    const detail::scratch_allocator<alloc_t> hooks(alloc);
    return detail::multiply_runtime_unsliced(r, a, b, hooks.heap());
}

std::size_t run_any(const span_t r, const cspan_t a, const cspan_t b) {
    alloc_t                                  alloc;
    const detail::scratch_allocator<alloc_t> hooks(alloc);
    return detail::multiply_runtime_any(r, a, b, hooks.heap());
}

std::size_t run_square_runtime(const span_t r, const cspan_t a, const cspan_t) {
    alloc_t                                  alloc;
    const detail::scratch_allocator<alloc_t> hooks(alloc);
    return detail::square_runtime(r, a, hooks.heap());
}

std::size_t run_basecase(const span_t r, const cspan_t a, const cspan_t b) {
    return detail::multiply_basecase_runtime(r, a, b);
}

// All entry points for one operand pair (a, b). `squaring` means b is the same span as a.
void check_all_entry_points(const cspan_t a, const cspan_t b, const bool squaring) {
    check_product("multiply_dispatch", a, b, run_dispatch);
    check_product("multiply_runtime_any", a, b, run_any);
    if (a.size() >= 2 && b.size() >= 2) {
        check_product("multiply_runtime", a, b, run_runtime);
        check_product("multiply_runtime_unsliced", a, b, run_unsliced);
        check_product("multiply_runtime_sliced", a, b, run_sliced);
        if (std::min(a.size(), b.size()) < detail::mul_header_basecase_limbs) {
            check_product("multiply_basecase_runtime", a, b, run_basecase);
        }
        if (squaring) {
            check_product("square_dispatch", a, b, run_square_dispatch);
            check_product("square_runtime", a, b, run_square_runtime);
        }
    }
}

std::size_t sat_sub(const std::size_t x, const std::size_t d) { return x > d ? x - d : 1; }

// Operand sizes straddling every cutoff and basecase boundary.
std::vector<std::size_t> boundary_sizes() {
    std::vector<std::size_t> v         = {1, 2, 3, 4, 5, 8, 16, 31, 32, 33};
    const std::size_t        cutoffs[] = {detail::square_long_cutoff,
                                          detail::karatsuba_cutoff,
                                          detail::square_karatsuba_cutoff,
                                          detail::toom_cook_3_cutoff,
                                          detail::square_toom_cook_3_cutoff,
                                          detail::toom_cook_4_cutoff,
                                          detail::square_toom_cook_4_cutoff,
                                          detail::toom_cook_6_5_cutoff,
                                          detail::square_toom_cook_6_5_cutoff,
                                          detail::toom_cook_8_5_cutoff,
                                          detail::square_toom_cook_8_5_cutoff};
    for (const std::size_t c : cutoffs) {
        v.push_back(sat_sub(c, 1));
        v.push_back(c);
        v.push_back(c + 1);
    }
    std::ranges::sort(v);
    v.erase(std::unique(v.begin(), v.end()), v.end());
    std::erase_if(v, [](const std::size_t n) { return n > max_limbs; });
    return v;
}

TEST(DispatchContract, BalancedProductsAtEveryCutoff) {
    std::mt19937_64 rng{0xd15ca7c4u};
    for (const std::size_t n : boundary_sizes()) {
        const limbs_t a = random_limbs(n, rng);
        const limbs_t b = random_limbs(n, rng);
        check_all_entry_points(a, b, false);
        if (n + 1 <= max_limbs) {
            const limbs_t c = random_limbs(n + 1, rng);
            check_all_entry_points(a, c, false);
            check_all_entry_points(c, a, false);
        }
    }
}

TEST(DispatchContract, SquaresAtEveryCutoff) {
    std::mt19937_64 rng{0x5ca1ab1eu};
    for (const std::size_t n : boundary_sizes()) {
        const limbs_t a = random_limbs(n, rng);
        check_all_entry_points(a, a, true);
    }
}

TEST(DispatchContract, SingleLimbOperands) {
    std::mt19937_64 rng{0x51a61eu};
    for (const std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{17}, std::size_t{200}}) {
        const limbs_t a = random_limbs(n, rng);
        // No carry out of the top limb: the product still has to define limb n.
        const limbs_t small(1, uint_t{3});
        const limbs_t carry(1, ~uint_t{0});
        limbs_t       low_top = a;
        low_top.back()        = 1;
        check_all_entry_points(low_top, small, false);
        check_all_entry_points(small, low_top, false);
        check_all_entry_points(a, carry, false);
        check_all_entry_points(carry, a, false);
    }
}

TEST(DispatchContract, PowerOfTwoOperands) {
    std::mt19937_64 rng{0xb17b17u};
    for (const std::size_t n : boundary_sizes()) {
        if (n < 2) {
            continue;
        }
        for (const unsigned bit : {0u, 1u, 17u, static_cast<unsigned>(limb_bits - 1)}) {
            const limbs_t p = power_of_two(n, bit);
            for (const std::size_t other : {std::size_t{2}, std::size_t{7}, n, n + 5}) {
                if (other > max_limbs) {
                    continue;
                }
                const limbs_t a = random_limbs(other, rng);
                check_all_entry_points(a, p, false);
                check_all_entry_points(p, a, false);
            }
            check_all_entry_points(p, p, true);
            const limbs_t q = power_of_two(n + 1, bit);
            check_all_entry_points(p, q, false);
        }
    }
}

TEST(DispatchContract, PowerOfTwoKernelWritesEveryLimb) {
    std::mt19937_64 rng{0x9072u};
    for (const std::size_t pn : {std::size_t{1}, std::size_t{2}, std::size_t{5}}) {
        for (const unsigned bit : {0u, 3u, static_cast<unsigned>(limb_bits - 1)}) {
            const limbs_t p = power_of_two(pn, bit);
            for (const std::size_t an : {std::size_t{1}, std::size_t{2}, std::size_t{9}}) {
                limbs_t a = random_limbs(an, rng);
                // Force a top limb that does not carry out for small shifts.
                a.back() = 1;
                check_product("multiply_power_of_two", a, p, [](const span_t r, const cspan_t x, const cspan_t y) {
                    return detail::multiply_power_of_two(r, x, y);
                });
            }
        }
    }
}

// Sliced shapes: a long operand against a short one at or above karatsuba_cutoff.
TEST(DispatchContract, SlicedUnbalancedShapes) {
    std::mt19937_64                           rng{0x511ceu};
    const std::size_t                         kc       = detail::karatsuba_cutoff;
    const std::pair<std::size_t, std::size_t> shapes[] = {{kc, 3 * kc + 1},
                                                          {kc, 4 * kc + 3},
                                                          {kc + 5, 9 * kc},
                                                          {kc + 1, 2 * kc},
                                                          {2 * kc, 7 * kc + 2},
                                                          {kc - 1, 6 * kc}};
    for (const auto& [s, l] : shapes) {
        if (l > max_limbs) {
            continue;
        }
        const limbs_t a = random_limbs(s, rng);
        const limbs_t b = random_limbs(l, rng);
        check_all_entry_points(a, b, false);
        check_all_entry_points(b, a, false);
    }
}

// The first balanced size the FFT gate takes (64-bit limbs), when it is small enough to run quickly.
TEST(DispatchContract, FirstFftSize) {
    if constexpr (limb_bits == 64) {
        std::mt19937_64 rng{0xff7u};
        for (std::size_t n = detail::karatsuba_cutoff; n <= 6000; ++n) {
            if (detail::fft_mul_worthwhile(n, n)) {
                const limbs_t a = random_limbs(n, rng);
                const limbs_t b = random_limbs(n, rng);
                check_all_entry_points(a, b, false);
                break;
            }
        }
        for (std::size_t n = detail::karatsuba_cutoff; n <= 6000; ++n) {
            if (detail::square_fft_worthwhile(n)) {
                const limbs_t a = random_limbs(n, rng);
                check_all_entry_points(a, a, true);
                break;
            }
        }
    }
}

// Untrimmed operands: the high zero limbs are dropped before dispatch and limbs past the trimmed sum stay untouched.
TEST(DispatchContract, UntrimmedOperands) {
    std::mt19937_64 rng{0x07ca11edu};
    for (const std::size_t n : {std::size_t{2}, std::size_t{5}, std::size_t{40}}) {
        limbs_t a = random_limbs(n, rng);
        limbs_t b = random_limbs(n + 1, rng);
        a.resize(n + 3, uint_t{0});
        b.resize(n + 2, uint_t{0});
        const std::size_t trimmed_total = n + (n + 1);
        const std::size_t total         = a.size() + b.size();
        limbs_t           dirty(total, poison);
        limbs_t           clean(total, uint_t{0});
        alloc_t           alloc;
        const std::size_t n_dirty = detail::multiply_dispatch(span_t{dirty}, cspan_t{a}, cspan_t{b}, alloc);
        const std::size_t n_clean = detail::multiply_dispatch(span_t{clean}, cspan_t{a}, cspan_t{b}, alloc);
        EXPECT_EQ(n_dirty, n_clean);
        EXPECT_TRUE(
            std::equal(dirty.begin(), dirty.begin() + static_cast<std::ptrdiff_t>(trimmed_total), clean.begin()));
        for (std::size_t i = trimmed_total; i < total; ++i) {
            EXPECT_EQ(dirty[i], poison) << "limb " << i << " past the trimmed product was written";
        }
    }
}

// ---------------------------------------------------------------------------
// Division
// ---------------------------------------------------------------------------

// Quotient/remainder spans are fully written by every tier: poison-fill both and compare against a zeroed run.
void check_division(const std::size_t m, const std::size_t s, std::mt19937_64& rng) {
    const limbs_t dividend = random_limbs(m, rng);
    const limbs_t divisor  = random_limbs(s, rng);
    const cspan_t a{dividend};
    const cspan_t b{divisor};
    alloc_t       alloc;

    const std::size_t qn = m - s + 1;
    const std::size_t rn = m + 1;

    auto run_full = [&](const uint_t fill) {
        limbs_t                            q(qn, fill);
        limbs_t                            r(rn, fill);
        detail::scratch_allocator<alloc_t> scratch(detail::divide_schoolbook_storage_size(m, s, false), alloc);
        detail::divide_dispatch(span_t{q}, span_t{r}, a, b, scratch, alloc);
        return std::pair{std::move(q), std::move(r)};
    };
    auto run_q = [&](const uint_t fill) {
        limbs_t                            q(qn, fill);
        detail::scratch_allocator<alloc_t> scratch(detail::divide_schoolbook_storage_size(m, s, true), alloc);
        detail::divide_dispatch_q(span_t{q}, a, b, scratch, alloc);
        return q;
    };

    const auto [qd, rd] = run_full(poison);
    const auto [qc, rc] = run_full(uint_t{0});
    EXPECT_EQ(qd, qc) << "divide_dispatch quotient, m=" << m << " s=" << s;
    EXPECT_EQ(rd, rc) << "divide_dispatch remainder, m=" << m << " s=" << s;
    EXPECT_EQ(run_q(poison), run_q(uint_t{0})) << "divide_dispatch_q quotient, m=" << m << " s=" << s;
    EXPECT_EQ(run_q(poison), qc) << "divide_dispatch_q differs from divide_dispatch, m=" << m << " s=" << s;

    // q * divisor + r == dividend, checked through the (already contract-tested) multiply_long.
    limbs_t prod(qn + s, uint_t{0});
    detail::multiply_long(span_t{prod}, cspan_t{qc.data(), detail::trimmed_size_span(cspan_t{qc})}, b);
    limbs_t sum(std::max(prod.size(), rd.size()) + 1, uint_t{0});
    std::copy(prod.begin(), prod.end(), sum.begin());
    const bool carry = detail::add_unsigned_spans(span_t{sum}.first(sum.size() - 1),
                                                  span_t{sum}.first(sum.size() - 1),
                                                  cspan_t{rd.data(), std::min(rd.size(), sum.size() - 1)});
    EXPECT_FALSE(carry);
    for (std::size_t i = 0; i < sum.size(); ++i) {
        const uint_t want = i < dividend.size() ? dividend[i] : uint_t{0};
        ASSERT_EQ(sum[i], want) << "q*d+r != dividend at limb " << i << ", m=" << m << " s=" << s;
    }
}

std::vector<std::pair<std::size_t, std::size_t>> division_shapes() {
    std::vector<std::pair<std::size_t, std::size_t>> v;
    const std::size_t                                bz  = detail::burnikel_ziegler_cutoff;
    const std::size_t                                off = detail::burnikel_ziegler_offset;
    for (const std::size_t s : {std::size_t{2}, std::size_t{3}, std::size_t{4}, bz - 1, bz, bz + 1}) {
        for (const std::size_t m : {s, s + 1, s + off - 1, s + off, s + off + 1, 3 * s + 1}) {
            v.emplace_back(m, s);
        }
    }
    v.emplace_back(8, 4);
    v.emplace_back(9, 2);
    // Barrett march gate: below, at and above the m/16 line for the compiled march cutoff.
    const std::size_t c = detail::barrett_march_cutoff;
    for (const std::size_t s : {c - 1, c, c + 1}) {
        for (const std::size_t m : {16 * s - 1, 16 * s, 16 * s + 5}) {
            if (s >= 2 && m <= max_limbs) {
                v.emplace_back(m, s);
            }
        }
    }
    return v;
}

TEST(DispatchContract, DivisionAcrossTierGates) {
    std::mt19937_64 rng{0xd1f1de5u};
    bool            saw_schoolbook = false;
    bool            saw_barrett    = false;
    bool            saw_other      = false;
    for (const auto& [m, s] : division_shapes()) {
        check_division(m, s, rng);
        if (detail::divide_takes_schoolbook(m, s)) {
            saw_schoolbook = true;
        } else if (detail::divide_takes_barrett(m, s)) {
            saw_barrett = true;
        } else {
            saw_other = true;
        }
    }
    EXPECT_TRUE(saw_schoolbook);
    EXPECT_TRUE(saw_barrett);
    EXPECT_TRUE(saw_other);
}

TEST(DispatchContract, DivideTakesSchoolbookMatchesGates) {
    for (std::size_t s = 2; s < 80; ++s) {
        for (const std::size_t m : {s, s + 1, s + 9, s + 10, s + 64, 4 * s, 16 * s, 16 * s + 5}) {
            const bool barrett = (s >= detail::barrett_march_cutoff && m / 16 >= s) ||
                                 (s >= detail::barrett_march8_cutoff && m / 8 >= s) ||
                                 (m >= detail::barrett_quarter_cutoff && m / 4 >= s) ||
                                 (m >= detail::barrett_balanced_cutoff && m - s >= s);
            const bool bz      = s >= detail::burnikel_ziegler_cutoff && m - s >= detail::burnikel_ziegler_offset;
            EXPECT_EQ(detail::divide_takes_schoolbook(m, s), !barrett && !bz) << "m=" << m << " s=" << s;
        }
    }
}

// divide_unsigned reads the dividend only while copying it to scratch, so the quotient may start at its first limb.
TEST(DispatchContract, SchoolbookQuotientMayAliasDividend) {
    std::mt19937_64 rng{0xa11a5u};
    for (const auto& [m, s] : {std::pair<std::size_t, std::size_t>{8, 4}, {9, 2}, {20, 3}, {5, 5}, {33, 17}}) {
        const limbs_t     dividend = random_limbs(m, rng);
        const limbs_t     divisor  = random_limbs(s, rng);
        const std::size_t qn       = m - s + 1;
        alloc_t           alloc;

        limbs_t q_ref(qn, uint_t{0});
        limbs_t r_ref(m + 1, uint_t{0});
        {
            detail::scratch_allocator<alloc_t> scratch(detail::divide_unsigned_storage_size(m, s), alloc);
            detail::divide_unsigned(span_t{q_ref}, span_t{r_ref}, cspan_t{dividend}, cspan_t{divisor}, scratch);
        }

        limbs_t buf = dividend;
        limbs_t r(m + 1, poison);
        {
            detail::scratch_allocator<alloc_t> scratch(detail::divide_unsigned_storage_size(m, s), alloc);
            detail::divide_unsigned(
                span_t{buf.data(), qn}, span_t{r}, cspan_t{buf.data(), m}, cspan_t{divisor}, scratch);
        }
        EXPECT_TRUE(std::equal(q_ref.begin(), q_ref.end(), buf.begin())) << "m=" << m << " s=" << s;
        EXPECT_EQ(r, r_ref) << "m=" << m << " s=" << s;
    }
}

// ---------------------------------------------------------------------------
// Constant evaluation: the dispatcher must not rely on a pre-zeroed result there either.
// ---------------------------------------------------------------------------

constexpr bool constexpr_product_ok(const std::span<const uint_t> a, const std::span<const uint_t> b) {
    std::array<uint_t, 16> dirty{};
    std::array<uint_t, 16> ref{};
    for (auto& x : dirty) {
        x = ~uint_t{0};
    }
    std::allocator<uint_t> alloc;
    const std::size_t      total = a.size() + b.size();
    const std::size_t      n     = detail::multiply_dispatch(std::span<uint_t>{dirty}, a, b, alloc);
    detail::multiply_long(std::span<uint_t>{ref}, a, b);
    for (std::size_t i = 0; i < total; ++i) {
        if (dirty[i] != ref[i]) {
            return false;
        }
    }
    return n == detail::trimmed_size_span(std::span<const uint_t>{ref.data(), total});
}

constexpr bool constexpr_cases() {
    constexpr uint_t            max = ~uint_t{0};
    const std::array<uint_t, 1> one{7};
    const std::array<uint_t, 1> three{3};
    const std::array<uint_t, 3> wide{1, 2, 4};
    const std::array<uint_t, 3> full{max, max, max};
    const std::array<uint_t, 3> p2{0, 0, 4};
    const std::array<uint_t, 3> p2_unit{0, 0, 1};
    const std::array<uint_t, 2> pair{5, 6};
    const std::array<uint_t, 3> sq{7, 9, 11};
    const std::array<uint_t, 1> top{max};
    return constexpr_product_ok(one, three) && constexpr_product_ok(wide, three) && constexpr_product_ok(full, top) &&
           constexpr_product_ok(top, full) && constexpr_product_ok(wide, pair) && constexpr_product_ok(p2, pair) &&
           constexpr_product_ok(pair, p2) && constexpr_product_ok(p2_unit, wide) && constexpr_product_ok(p2, p2) &&
           constexpr_product_ok(full, full) && constexpr_product_ok(sq, sq);
}

static_assert(constexpr_cases());

} // namespace
