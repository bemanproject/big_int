// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Exercises `beman::big_int::pmr::basic_big_int` / `pmr::big_int`
// (the `namespace pmr` block in include/beman/big_int/big_int.hpp) through
// `import beman.big_int;`.
//
// Allocator propagation here is not assumed; it is read out of big_int.hpp and
// confirmed by these tests:
//   - the copy and move constructors copy/move `m_alloc` unconditionally
//     (`basic_big_int(const basic_big_int&)` / `basic_big_int(basic_big_int&&)`),
//     so constructing one pmr::big_int from another preserves the source's
//     resource;
//   - copy and move ASSIGNMENT go through `assign_value`, which only touches
//     `m_alloc` when `propagate_on_container_{copy,move}_assignment` holds.
//     `std::pmr::polymorphic_allocator` sets both to `false`, so assigning one
//     pmr::big_int into another changes its VALUE but never its resource;
//   - the free `operator+` (and the other binary arithmetic operators), for two
//     lvalue operands, default-constructs its result (`Result r;`) rather than
//     inheriting either operand's allocator. For `std::pmr::polymorphic_allocator`
//     that means the sum of two pmr::big_int lvalues lands on
//     `std::pmr::get_default_resource()`, regardless of which resource the
//     operands used;
//   - compound assignment (`operator+=` et al.) always folds `rhs` into `*this`
//     in place and never touches `m_alloc`, so `*this` keeps its own resource.
//
// `big_int` and `pmr::big_int` are different basic_big_int specializations (they
// differ only in Allocator), and none of the operators (==, <=>, +, ...) mix two
// different basic_big_int specializations -- only (basic_big_int, plain integer)
// and (basic_big_int, same basic_big_int) are supported (see
// detail::common_big_int_type_impl). Cross-type comparisons below go through
// to_string() instead.

#include <array>
#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <vector>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;
namespace {

using beman::big_int::big_int;
using beman::big_int::uint_multiprecision_t;
using namespace beman::big_int::literals;

// A memory_resource that counts bytes and allocation requests it services,
// delegating the actual work to the process's new/delete resource. Used below
// to confirm that a wide pmr::big_int genuinely allocates through the resource
// it was given, rather than merely reporting the right resource pointer.
class counting_resource : public std::pmr::memory_resource {
  public:
    std::size_t allocations     = 0;
    std::size_t bytes_allocated = 0;

  private:
    void* do_allocate(const std::size_t bytes, const std::size_t alignment) override {
        ++allocations;
        bytes_allocated += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* const p, const std::size_t bytes, const std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

} // namespace

// [type identities] ============================================================

TEST(Pmr, TypeIdentities) {
    using pmr_big_int_128 = beman::big_int::pmr::basic_big_int<128>;
    static_assert(
        std::is_same_v<pmr_big_int_128::allocator_type, std::pmr::polymorphic_allocator<uint_multiprecision_t>>);
    static_assert(std::is_same_v<beman::big_int::pmr::big_int::allocator_type,
                                 std::pmr::polymorphic_allocator<uint_multiprecision_t>>);
    // pmr::big_int uses the same limb type and inplace width as big_int, only the allocator differs.
    static_assert(
        std::is_same_v<beman::big_int::pmr::big_int, beman::big_int::pmr::basic_big_int<big_int::inplace_bits>>);
}

// [construction / resource identity] ==========================================

TEST(Pmr, ConstructsOverMonotonicBufferResource) {
    std::array<std::byte, 256>          buffer{};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());

    const beman::big_int::pmr::big_int x(42, &resource);
    EXPECT_EQ(x.get_allocator().resource(), &resource);
    EXPECT_EQ(x, 42);
}

// The copy constructor goes through
// `select_on_container_copy_construction`, which for polymorphic_allocator
// returns a default-constructed allocator: the copy lands on the default
// resource, as it does for the pmr containers. Naming the allocator is how a
// caller keeps a copy on the source's resource.
TEST(Pmr, CopyConstructionDoesNotPreserveResource) {
    std::array<std::byte, 256>          buffer{};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());

    const beman::big_int::pmr::big_int x(7, &resource);
    const beman::big_int::pmr::big_int y(x);
    EXPECT_EQ(y.get_allocator().resource(), std::pmr::get_default_resource());
    EXPECT_EQ(y, x);

    const beman::big_int::pmr::big_int z(x, &resource);
    EXPECT_EQ(z.get_allocator().resource(), &resource);
    EXPECT_EQ(z, x);
}

TEST(Pmr, MoveConstructionPreservesResource) {
    std::array<std::byte, 256>          buffer{};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());

    beman::big_int::pmr::big_int       x(7, &resource);
    const beman::big_int::pmr::big_int y(std::move(x));
    EXPECT_EQ(y.get_allocator().resource(), &resource);
    EXPECT_EQ(y, 7);
}

// Naming the allocator moves a value onto that resource instead.
TEST(Pmr, AllocatorExtendedMoveConstructionUsesNamedResource) {
    std::array<std::byte, 256>          buffer_a{};
    std::array<std::byte, 256>          buffer_b{};
    std::pmr::monotonic_buffer_resource resource_a(buffer_a.data(), buffer_a.size());
    std::pmr::monotonic_buffer_resource resource_b(buffer_b.data(), buffer_b.size());

    beman::big_int::pmr::big_int       x(7, &resource_a);
    const beman::big_int::pmr::big_int y(std::move(x), &resource_b);
    EXPECT_EQ(y.get_allocator().resource(), &resource_b);
    EXPECT_EQ(y, 7);
}

// [allocator propagation on assignment] ========================================

// std::pmr::polymorphic_allocator does not propagate on copy assignment, so the
// destination keeps its own resource; only the value changes.
TEST(Pmr, CopyAssignmentDoesNotPropagateResource) {
    std::array<std::byte, 256>          buffer_a{};
    std::array<std::byte, 256>          buffer_b{};
    std::pmr::monotonic_buffer_resource resource_a(buffer_a.data(), buffer_a.size());
    std::pmr::monotonic_buffer_resource resource_b(buffer_b.data(), buffer_b.size());

    const beman::big_int::pmr::big_int source(123, &resource_a);
    beman::big_int::pmr::big_int       dest(0, &resource_b);

    dest = source;
    EXPECT_EQ(dest.get_allocator().resource(), &resource_b);
    EXPECT_EQ(dest, source);
}

// Same for move assignment: the value moves in, but the destination's resource
// is untouched.
TEST(Pmr, MoveAssignmentDoesNotPropagateResource) {
    std::array<std::byte, 256>          buffer_a{};
    std::array<std::byte, 256>          buffer_b{};
    std::pmr::monotonic_buffer_resource resource_a(buffer_a.data(), buffer_a.size());
    std::pmr::monotonic_buffer_resource resource_b(buffer_b.data(), buffer_b.size());

    beman::big_int::pmr::big_int source(123, &resource_a);
    beman::big_int::pmr::big_int dest(0, &resource_b);

    dest = std::move(source);
    EXPECT_EQ(dest.get_allocator().resource(), &resource_b);
    EXPECT_EQ(dest, 123);
}

// [arithmetic across different resources] ======================================

// Compound assignment always folds the right-hand side into `*this` in place,
// so `*this` keeps its own resource no matter which resource `rhs` used.
TEST(Pmr, CompoundAssignmentKeepsLeftHandResource) {
    std::array<std::byte, 256>          buffer_a{};
    std::array<std::byte, 256>          buffer_b{};
    std::pmr::monotonic_buffer_resource resource_a(buffer_a.data(), buffer_a.size());
    std::pmr::monotonic_buffer_resource resource_b(buffer_b.data(), buffer_b.size());

    beman::big_int::pmr::big_int       x(100, &resource_a);
    const beman::big_int::pmr::big_int y(23, &resource_b);

    x += y;
    EXPECT_EQ(x.get_allocator().resource(), &resource_a);
    EXPECT_EQ(x, 123);
}

// The free `operator+`, given two lvalue operands, selects its result's
// allocator through `select_on_container_copy_construction` rather than
// adopting either operand's; for polymorphic_allocator that lands the sum on
// the process default resource, which is neither of the two resources the
// operands used.
TEST(Pmr, FreeOperatorPlusLandsOnDefaultResource) {
    std::array<std::byte, 256>          buffer_a{};
    std::array<std::byte, 256>          buffer_b{};
    std::pmr::monotonic_buffer_resource resource_a(buffer_a.data(), buffer_a.size());
    std::pmr::monotonic_buffer_resource resource_b(buffer_b.data(), buffer_b.size());

    const beman::big_int::pmr::big_int x(100, &resource_a);
    const beman::big_int::pmr::big_int y(23, &resource_b);

    const beman::big_int::pmr::big_int sum = x + y;
    EXPECT_EQ(sum, 123);
    EXPECT_EQ(sum.get_allocator().resource(), std::pmr::get_default_resource());
    EXPECT_NE(sum.get_allocator().resource(), &resource_a);
    EXPECT_NE(sum.get_allocator().resource(), &resource_b);
}

// [hashing] =====================================================================

TEST(Pmr, HashAgreesWithBigInt) {
    std::array<std::byte, 256>          buffer{};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());

    const big_int                      plain = 98765432109876543210_n;
    const beman::big_int::pmr::big_int mirrored(plain, &resource);

    EXPECT_EQ(std::hash<big_int>{}(plain), (std::hash<beman::big_int::pmr::big_int>{}(mirrored)));
}

// [vector on a custom resource] =================================================

TEST(Pmr, VectorOnCustomResource) {
    std::array<std::byte, 4096>         buffer{};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());

    std::pmr::vector<beman::big_int::pmr::big_int> values(&resource);
    values.emplace_back(1);
    values.emplace_back(2);
    values.emplace_back(3);

    ASSERT_EQ(values.size(), 3u);
    for (const auto& v : values) {
        // Elements constructed by std::pmr::vector receive the vector's own
        // allocator through uses-allocator construction.
        EXPECT_EQ(v.get_allocator().resource(), &resource);
    }
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
}

// [swap] ========================================================================

// `swap` participates the allocator only when `propagate_on_container_swap`
// holds; polymorphic_allocator sets that to `false`, and [container.reqmts]
// then requires the two allocators to already compare equal. Both operands
// here share one resource, so that precondition holds.
TEST(Pmr, SwapOnSharedResource) {
    std::array<std::byte, 256>          buffer{};
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());

    beman::big_int::pmr::big_int a(1, &resource);
    beman::big_int::pmr::big_int b(2, &resource);

    swap(a, b);
    EXPECT_EQ(a, 2);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(a.get_allocator().resource(), &resource);
    EXPECT_EQ(b.get_allocator().resource(), &resource);
}

// [genuine allocation through a custom resource] ===============================

// A value wide enough that it cannot live in inplace storage, constructed with
// an explicit allocator, so the resource given to it is the one that actually
// services the allocation.
TEST(Pmr, WideValueAllocatesThroughItsResource) {
    counting_resource resource;

    const big_int mersenne = (big_int{1} << 4096) - 1_n;
    EXPECT_EQ(resource.allocations, 0u);

    const beman::big_int::pmr::big_int wide(mersenne, beman::big_int::pmr::big_int::allocator_type(&resource));

    EXPECT_GT(resource.allocations, 0u);
    EXPECT_GT(resource.bytes_allocated, 0u);
    EXPECT_EQ(wide.get_allocator().resource(), &resource);
    // `big_int` and `pmr::big_int` are different basic_big_int specializations
    // (they differ in Allocator), so no comparison operator applies directly
    // between them -- compare through their decimal representations instead.
    EXPECT_EQ(to_string(wide), to_string(mersenne));
}
