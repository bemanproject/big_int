// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// A stateful counting allocator shared by the allocation tests. The counters live in a counting_state owned by the
// test, so every rebound copy of the allocator reports into the same place. Allocators that were default constructed
// (state == nullptr) forward to std::allocator without counting.

#ifndef BEMAN_BIG_INT_TEST_UTIL_COUNTING_ALLOCATOR_HPP
#define BEMAN_BIG_INT_TEST_UTIL_COUNTING_ALLOCATOR_HPP

#include <cstddef>
#include <map>
#include <memory>
#include <typeindex>
#include <typeinfo>

#include <beman/big_int/detail/config.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace test_util {

struct counting_state {
    std::map<std::type_index, std::size_t> allocations;
    std::map<std::type_index, std::size_t> live_elements;
    std::size_t                            total_allocations{0};
    std::size_t                            total_deallocations{0};

    void reset_totals() noexcept {
        total_allocations   = 0;
        total_deallocations = 0;
    }
};

template <class T>
struct counting_allocator {
    using value_type = T;

    counting_state* state = nullptr;

    counting_allocator() = default;
    explicit counting_allocator(counting_state* s) : state(s) {}
    template <class U>
    counting_allocator(const counting_allocator<U>& other) : state(other.state) {}

    T* allocate(const std::size_t n) {
        if (state != nullptr) {
            ++state->allocations[std::type_index(typeid(T))];
            state->live_elements[std::type_index(typeid(T))] += n;
            ++state->total_allocations;
        }
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, const std::size_t n) noexcept {
        if (state != nullptr) {
            state->live_elements[std::type_index(typeid(T))] -= n;
            ++state->total_deallocations;
        }
        std::allocator<T>{}.deallocate(p, n);
    }

    template <class U>
    bool operator==(const counting_allocator<U>& other) const {
        return state == other.state;
    }
};

} // namespace test_util
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_TEST_UTIL_COUNTING_ALLOCATOR_HPP
