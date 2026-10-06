// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises the type-erased heap hooks on scratch_allocator_base: every
// hook pair must route through the owning allocator's rebind (so wrappers
// like counting or arena allocators observe the traffic), elements must be
// usable immediately, and deallocation must balance.

#include <beman/big_int/detail/scratch_allocator.hpp>

#include "util/util_counting_allocator.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <typeindex>

namespace {

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

using BEMAN_BIG_INT_NAMESPACE::test_util::counting_allocator;
using BEMAN_BIG_INT_NAMESPACE::test_util::counting_state;

TEST(ScratchHeap, HooksRouteThroughTheOwningAllocatorsRebinds) {
    counting_state                                        state;
    counting_allocator<uint_t>                            alloc(&state);
    detail::scratch_allocator<counting_allocator<uint_t>> scratch(64, alloc);

    const detail::scratch_heap_source& heap = scratch.heap();
    ASSERT_NE(heap.allocate_limbs, nullptr);
    ASSERT_NE(heap.allocate_u64, nullptr);
    ASSERT_NE(heap.allocate_f64, nullptr);

    {
        detail::scratch_heap_array<uint_t> limbs(heap, 100);
        ASSERT_EQ(limbs.span().size(), 100u);
        std::iota(limbs.span().begin(), limbs.span().end(), uint_t{1});
        EXPECT_EQ(limbs.span()[99], 100u);

        detail::scratch_heap_array<double> doubles(heap, 33);
        ASSERT_EQ(reinterpret_cast<std::uintptr_t>(doubles.data()) % alignof(double), 0u);
        for (auto& d : doubles.span()) {
            d = 2.5;
        }

        detail::scratch_heap_array<std::uint64_t> words(heap, 17);
        for (auto& w : words.span()) {
            w = ~std::uint64_t{0};
        }

        EXPECT_GT(state.live_elements[std::type_index(typeid(double))], 0u);
    }

    // Everything released; only the scratch limb buffer itself remains.
    EXPECT_EQ(state.live_elements[std::type_index(typeid(double))], 0u);
    for (const auto& [type, live] : state.live_elements) {
        if (type != std::type_index(typeid(uint_t))) {
            EXPECT_EQ(live, 0u);
        }
    }
    EXPECT_GE(state.allocations[std::type_index(typeid(double))], 1u);
}

TEST(ScratchHeap, HookOnlyConstructionCarriesNoLimbBuffer) {
    counting_state                                        state;
    counting_allocator<uint_t>                            alloc(&state);
    detail::scratch_allocator<counting_allocator<uint_t>> scratch(alloc);

    EXPECT_EQ(scratch.m_capacity, 0u);
    detail::scratch_heap_array<uint_t> limbs(scratch.heap(), 8);
    EXPECT_EQ(limbs.span().size(), 8u);
    EXPECT_EQ(state.allocations[std::type_index(typeid(uint_t))], 1u);
}

TEST(ScratchHeap, PeakTracksUnconditionally) {
    std::allocator<uint_t>                            alloc;
    detail::scratch_allocator<std::allocator<uint_t>> scratch(32, alloc);
    [[maybe_unused]] auto                             a = scratch.allocate(10);
    [[maybe_unused]] auto                             b = scratch.allocate(12);
    scratch.deallocate(12);
    EXPECT_EQ(scratch.peak(), 22u);
    [[maybe_unused]] auto c = scratch.allocate(4);
    EXPECT_EQ(scratch.peak(), 22u);
}

} // namespace
