// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_TESTS_MUL_KERNEL_TESTING_HPP
#define BEMAN_BIG_INT_TESTS_MUL_KERNEL_TESTING_HPP

// Helpers for driving one Toom-Cook kernel directly with cutoff_override = 1, so it runs (or falls back on its
// own ratio gate) at any size, and comparing against the portable schoolbook multiply_long.

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/mul_impl.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace kernel_testing {

using kernel_uint = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

inline std::vector<kernel_uint> make_random_limbs(const std::size_t limbs, const std::uint64_t seed) {
    std::mt19937_64                            rng{seed};
    std::uniform_int_distribution<kernel_uint> dist;
    std::vector<kernel_uint>                   v(limbs);
    for (auto& x : v) {
        x = dist(rng);
    }
    if (!v.empty() && v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// `kernel(result, a, b, scratch, cutoff_override)` is one of the multiply_toom_cook_* functions.
template <class Kernel>
void expect_kernel_matches_long(Kernel&& kernel, const std::size_t na, const std::size_t nb) {
    SCOPED_TRACE("kernel na=" + std::to_string(na) + " nb=" + std::to_string(nb));
    using alloc_t = std::allocator<kernel_uint>;
    const auto a  = make_random_limbs(na, 0x9E3779B97F4A7C15ULL ^ (na * 1315423911U));
    const auto b  = make_random_limbs(nb, 0xC2B2AE3D27D4EB4FULL ^ (nb * 2654435761U));

    std::vector<kernel_uint> got(na + nb, kernel_uint{0});
    std::vector<kernel_uint> ref(na + nb, kernel_uint{0});

    alloc_t                                                       alloc;
    ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<alloc_t> scratch(16 * std::max(na, nb) + 4096, alloc);
    kernel(std::span<kernel_uint>{got},
           std::span<const kernel_uint>{a},
           std::span<const kernel_uint>{b},
           scratch,
           std::size_t{1});
    ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_long(
        std::span<kernel_uint>{ref}, std::span<const kernel_uint>{a}, std::span<const kernel_uint>{b});
    EXPECT_EQ(got, ref);
}

// `kernel` and `reference` are two different multiply_* kernels taking
// (result, a, b, scratch, cutoff_override); both are forced at the top level (cutoff_override = 1) and must agree.
template <class Kernel, class Reference>
void expect_kernels_match(Kernel&& kernel, Reference&& reference, const std::size_t na, const std::size_t nb) {
    SCOPED_TRACE("kernel vs kernel na=" + std::to_string(na) + " nb=" + std::to_string(nb));
    using alloc_t = std::allocator<kernel_uint>;
    const auto a  = make_random_limbs(na, 0x51ED270B0B5A7E15ULL ^ (na * 1315423911U));
    const auto b  = make_random_limbs(nb, 0x2545F4914F6CDD1DULL ^ (nb * 2654435761U));

    std::vector<kernel_uint> got(na + nb, kernel_uint{0});
    std::vector<kernel_uint> ref(na + nb, kernel_uint{0});

    alloc_t                                                       alloc;
    ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<alloc_t> scratch(16 * std::max(na, nb) + 4096, alloc);
    kernel(std::span<kernel_uint>{got},
           std::span<const kernel_uint>{a},
           std::span<const kernel_uint>{b},
           scratch,
           std::size_t{1});
    ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<alloc_t> ref_scratch(16 * std::max(na, nb) + 4096, alloc);
    reference(std::span<kernel_uint>{ref},
              std::span<const kernel_uint>{a},
              std::span<const kernel_uint>{b},
              ref_scratch,
              std::size_t{1});
    EXPECT_EQ(got, ref);
}

} // namespace kernel_testing
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_TESTS_MUL_KERNEL_TESTING_HPP
