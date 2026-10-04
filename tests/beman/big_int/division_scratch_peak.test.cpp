// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Probes the scratch_allocator high-water mark of divide_burnikel_ziegler
// across operand shapes (production threshold and small overrides forcing
// deep recursion) and asserts it never exceeds the
// burnikel_ziegler_storage_size budget the production entry point allocates.
// Fast enough to run as a normal test; the analogous multiplication probe is
// the opt-in scratch_peak_bench.

#define BEMAN_BIG_INT_INSTRUMENT

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

using scratch_for_test = detail::scratch_allocator<std::allocator<uint_t>>;

// NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
std::mt19937_64 rng{0x5c4a7cULL};

std::vector<uint_t> random_limbs(const std::size_t size) {
    std::vector<uint_t> v(size);
    for (auto& limb : v) {
        limb = static_cast<uint_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// Runs one division in an over-provisioned instrumented scratch and asserts
// the recorded peak stays within the production budget for the same plan.
// Returns peak / budget for headroom reporting.
double probe(const std::size_t dividend_limbs, const std::size_t divisor_limbs, const std::size_t threshold_override) {
    const auto dividend = random_limbs(dividend_limbs);
    const auto divisor  = random_limbs(divisor_limbs);
    const auto a_view   = std::span<const uint_t>{dividend};
    const auto b_view   = std::span<const uint_t>{divisor};

    std::vector<uint_t> quotient(dividend_limbs - divisor_limbs + 1, 0);
    std::vector<uint_t> remainder(dividend_limbs + 1, 0);

    const detail::burnikel_ziegler_params plan = detail::burnikel_ziegler_plan(a_view, b_view, threshold_override);
    const std::size_t budget = detail::burnikel_ziegler_storage_size(plan.block_limbs, plan.blocks, plan.threshold);

    std::allocator<uint_t> alloc;
    scratch_for_test       scratch(3 * budget + 64, alloc);
    detail::divide_burnikel_ziegler(
        std::span<uint_t>{quotient}, std::span<uint_t>{remainder}, a_view, b_view, scratch, plan);

    EXPECT_LE(scratch.peak(), budget) << "m=" << dividend_limbs << " s=" << divisor_limbs
                                      << " thr=" << threshold_override << " n=" << plan.block_limbs
                                      << " t=" << plan.blocks;
    return static_cast<double>(scratch.peak()) / static_cast<double>(budget);
}

TEST(DivisionScratchPeak, ProductionThresholdWithinBudget) {
    constexpr std::pair<std::size_t, std::size_t> shapes[] = {
        {60, 40},
        {61, 41},
        {80, 40},
        {85, 64},
        {101, 100},
        {120, 48},
        {150, 100},
        {200, 96},
        {300, 41},
        {400, 100},
        {600, 128},
        {1000, 128},
        {1200, 50},
        {2048, 1024},
    };
    double worst = 0.0;
    for (const auto& [m, s] : shapes) {
        for (int sample = 0; sample < 3; ++sample) {
            worst = std::max(worst, probe(m, s, 0));
        }
    }
    // Headroom indicator for retuning the storage formula; see the comment on
    // burnikel_ziegler_storage_size.
    RecordProperty("worst_peak_per_budget", testing::PrintToString(worst));
}

TEST(DivisionScratchPeak, ForcedDeepRecursionWithinBudget) {
    constexpr std::pair<std::size_t, std::size_t> shapes[] = {
        {10, 5},
        {20, 7},
        {40, 16},
        {60, 24},
        {100, 33},
        {40, 5}, // leaf temporaries dominate the recursion term here
    };
    for (const std::size_t thr : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}}) {
        for (const auto& [m, s] : shapes) {
            for (int sample = 0; sample < 3; ++sample) {
                probe(m, s, thr);
            }
        }
    }
}

// Same probe for the Barrett driver: budget from barrett_storage_size with
// the matching block count and reciprocal threshold.
void probe_barrett(const std::size_t dividend_limbs,
                   const std::size_t divisor_limbs,
                   const std::size_t invert_override) {
    const auto dividend = random_limbs(dividend_limbs);
    const auto divisor  = random_limbs(divisor_limbs);
    const auto a_view   = std::span<const uint_t>{dividend};
    const auto b_view   = std::span<const uint_t>{divisor};

    std::vector<uint_t> quotient(dividend_limbs - divisor_limbs + 1, 0);
    std::vector<uint_t> remainder(dividend_limbs + 1, 0);

    const std::size_t thr = invert_override != 0 ? invert_override : detail::reciprocal_span_cutoff;
    const std::size_t budget =
        detail::barrett_storage_size(divisor_limbs, detail::barrett_blocks(a_view, b_view), thr);

    std::allocator<uint_t> alloc;
    scratch_for_test       scratch(3 * budget + 64, alloc);
    detail::divide_barrett(
        std::span<uint_t>{quotient}, std::span<uint_t>{remainder}, a_view, b_view, scratch, invert_override);

    EXPECT_LE(scratch.peak(), budget) << "m=" << dividend_limbs << " s=" << divisor_limbs
                                      << " inv_thr=" << invert_override;
}

TEST(DivisionScratchPeak, BarrettWithinBudget) {
    constexpr std::pair<std::size_t, std::size_t> shapes[] = {
        {12, 4},
        {60, 24},
        {101, 100},
        {200, 96},
        {400, 100},
        {600, 128},
        {1200, 50},
        {1024, 512},
    };
    for (const std::size_t thr : {std::size_t{0}, std::size_t{2}, std::size_t{5}}) {
        for (const auto& [m, s] : shapes) {
            for (int sample = 0; sample < 3; ++sample) {
                probe_barrett(m, s, thr);
            }
        }
    }
}

// Shapes at the Barrett march gate itself (divisor = barrett_march_cutoff), where the default dispatch marches.
TEST(DivisionScratchPeak, BarrettAtMarchGateWithinBudget) {
    constexpr std::size_t c = detail::barrett_march_cutoff;
    for (const std::size_t thr : {std::size_t{0}, std::size_t{2}}) {
        probe_barrett(16 * c + 5, c, thr);
        probe_barrett(std::min<std::size_t>(1000 * c, 60000), c, thr);
    }
}

} // namespace
