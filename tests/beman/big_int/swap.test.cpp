// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <utility>

#include <beman/big_int/big_int.hpp>

#include <gtest/gtest.h>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::to_string;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// A wide instantiation: inplace_capacity == 256 / 64 == 4, so values up to four
// limbs live inline and the element-wise inline-swap loop runs over four limbs.
using big_int_256 = basic_big_int<256>;

// `big_int` keeps a single inline limb, so any value >= 2^64 lives on the heap.
constexpr big_int two_pow(unsigned e) {
    big_int x{1};
    x <<= e;
    return x;
}

constexpr big_int_256 two_pow_256(unsigned e) {
    big_int_256 x{1};
    x <<= e;
    return x;
}

// ----- type-level checks -----

// swap returns void and is found as a non-const member.
static_assert(std::is_void_v<decltype(std::declval<big_int&>().swap(std::declval<big_int&>()))>);

// std::allocator is always-equal, so member swap is unconditionally noexcept.
static_assert(noexcept(std::declval<big_int&>().swap(std::declval<big_int&>())));

// std::pmr::polymorphic_allocator neither propagates on swap nor is always-equal,
// so its member swap carries a narrow (potentially-throwing) contract.
using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;
static_assert(!noexcept(std::declval<pmr_big_int&>().swap(std::declval<pmr_big_int&>())));

// The non-member overload returns void and is found by argument-dependent lookup,
// without `beman::big_int` being nominated.
static_assert(std::is_void_v<decltype(swap(std::declval<big_int&>(), std::declval<big_int&>()))>);

// `std` is an associated namespace of every `basic_big_int` whose allocator is a
// standard one, so `std::swap` is always a candidate; the non-member overload is
// more specialized and wins. With pmr that choice is observable: the move-based
// `std::swap` would be nothrow, while the narrow contract the non-member inherits
// from the member is not.
static_assert(std::is_swappable_v<big_int>);
static_assert(std::is_nothrow_swappable_v<big_int>);
static_assert(std::is_swappable_v<pmr_big_int>);
static_assert(!std::is_nothrow_swappable_v<pmr_big_int>);

// Both operands must name the same specialization, and neither may be const.
static_assert(!std::is_swappable_with_v<big_int&, big_int_256&>);
static_assert(!std::is_swappable_with_v<const big_int&, const big_int&>);

// The `using std::swap` idiom generic code relies on, written as a template so the
// call is resolved exactly as a library algorithm would resolve it.
template <class T>
constexpr void generic_swap(T& a, T& b) {
    using std::swap;
    swap(a, b);
}

// ----- compile-time tests -----

// Two inline operands exchange limbs and stay inline.
consteval bool test_swap_inplace_inplace() {
    big_int a{5};
    big_int b{42};
    a.swap(b);
    return a == 42 && b == 5 && is_inplace(a) && is_inplace(b);
}
static_assert(test_swap_inplace_inplace());

// Two heap operands exchange their buffer pointers.
consteval bool test_swap_heap_heap() {
    big_int a = two_pow(100);
    big_int b = two_pow(200);
    a.swap(b);
    return a == two_pow(200) && b == two_pow(100) && !is_inplace(a) && !is_inplace(b);
}
static_assert(test_swap_heap_heap());

// Mixed: an inline operand and a heap operand exchange storage models.
consteval bool test_swap_mixed() {
    big_int small{7};
    big_int large = two_pow(150);
    small.swap(large);
    // `small` adopts the heap buffer; `large` becomes inline.
    return small == two_pow(150) && large == 7 && !is_inplace(small) && is_inplace(large);
}
static_assert(test_swap_mixed());

// Signs are carried with the value.
consteval bool test_swap_signs() {
    big_int a{-13};
    big_int b{99};
    a.swap(b);
    return a == 99 && b == -13;
}
static_assert(test_swap_signs());

// Zero (inline) against a heap value is a mixed swap.
consteval bool test_swap_zero() {
    big_int a{0};
    big_int b = two_pow(130);
    a.swap(b);
    return a == two_pow(130) && b == 0 && !is_inplace(a) && is_inplace(b);
}
static_assert(test_swap_zero());

// Multi-limb inline swap: both operands fit within the four inline limbs.
consteval bool test_swap_multilimb_inline() {
    big_int_256 a = two_pow_256(200) + 12345; // four inline limbs
    big_int_256 b{7};                         // one inline limb
    a.swap(b);
    return a == 7 && b == two_pow_256(200) + 12345 && is_inplace(a) && is_inplace(b);
}
static_assert(test_swap_multilimb_inline());

// Mixed swap for the wide instantiation: inline (<= 4 limbs) against heap (> 4).
consteval bool test_swap_mixed_256() {
    big_int_256 inline_val = two_pow_256(100); // two limbs, inline
    big_int_256 heap_val   = two_pow_256(400); // seven limbs, heap
    inline_val.swap(heap_val);
    return inline_val == two_pow_256(400) && heap_val == two_pow_256(100) && !is_inplace(inline_val) &&
           is_inplace(heap_val);
}
static_assert(test_swap_mixed_256());

// Self-swap is a well-defined no-op for both storage models.
consteval bool test_self_swap() {
    big_int inline_val{42};
    inline_val.swap(inline_val);
    big_int heap_val = two_pow(90);
    heap_val.swap(heap_val);
    return inline_val == 42 && heap_val == two_pow(90);
}
static_assert(test_self_swap());

// ----- compile-time tests, non-member swap -----

// The unqualified call performs the same exchange as the member.
consteval bool test_free_swap_inplace_inplace() {
    big_int a{5};
    big_int b{42};
    swap(a, b);
    return a == 42 && b == 5 && is_inplace(a) && is_inplace(b);
}
static_assert(test_free_swap_inplace_inplace());

consteval bool test_free_swap_heap_heap() {
    big_int a = two_pow(100);
    big_int b = two_pow(200);
    swap(a, b);
    return a == two_pow(200) && b == two_pow(100) && !is_inplace(a) && !is_inplace(b);
}
static_assert(test_free_swap_heap_heap());

// Mixed storage models, and the signs travel with the values.
consteval bool test_free_swap_mixed() {
    big_int small{-7};
    big_int large = two_pow(150);
    swap(small, large);
    return small == two_pow(150) && large == -7 && !is_inplace(small) && is_inplace(large);
}
static_assert(test_free_swap_mixed());

// The wide instantiation resolves to its own specialization.
consteval bool test_free_swap_256() {
    big_int_256 inline_val = two_pow_256(100);
    big_int_256 heap_val   = two_pow_256(400);
    swap(inline_val, heap_val);
    return inline_val == two_pow_256(400) && heap_val == two_pow_256(100);
}
static_assert(test_free_swap_256());

// Both operands naming the same object is a no-op, as for the member.
consteval bool test_free_swap_self() {
    big_int inline_val{42};
    swap(inline_val, inline_val);
    big_int heap_val = two_pow(90);
    swap(heap_val, heap_val);
    return inline_val == 42 && heap_val == two_pow(90);
}
static_assert(test_free_swap_self());

// The two ways generic code reaches a customized swap.
consteval bool test_free_swap_generic_idiom() {
    big_int a{5};
    big_int b = two_pow(200);
    generic_swap(a, b);
    return a == two_pow(200) && b == 5;
}
static_assert(test_free_swap_generic_idiom());

consteval bool test_free_swap_ranges_swap() {
    big_int a{5};
    big_int b = two_pow(200);
    std::ranges::swap(a, b);
    return a == two_pow(200) && b == 5;
}
static_assert(test_free_swap_ranges_swap());

// ----- runtime tests -----

TEST(Swap, InplaceInplace) {
    big_int a{5};
    big_int b{42};
    a.swap(b);
    EXPECT_EQ(a, 42);
    EXPECT_EQ(b, 5);
    EXPECT_TRUE(is_inplace(a));
    EXPECT_TRUE(is_inplace(b));
}

TEST(Swap, HeapHeap) {
    big_int a = two_pow(100);
    big_int b = two_pow(200);
    a.swap(b);
    EXPECT_EQ(a, two_pow(200));
    EXPECT_EQ(b, two_pow(100));
    EXPECT_FALSE(is_inplace(a));
    EXPECT_FALSE(is_inplace(b));
}

TEST(Swap, Mixed) {
    big_int small{7};
    big_int large = two_pow(150);
    small.swap(large);
    EXPECT_EQ(small, two_pow(150));
    EXPECT_EQ(large, 7);
    EXPECT_FALSE(is_inplace(small));
    EXPECT_TRUE(is_inplace(large));
}

TEST(Swap, SignsAndZero) {
    big_int neg{-13};
    big_int pos{99};
    neg.swap(pos);
    EXPECT_EQ(neg, 99);
    EXPECT_EQ(pos, -13);

    big_int zero{0};
    big_int big = two_pow(130);
    zero.swap(big);
    EXPECT_EQ(zero, two_pow(130));
    EXPECT_EQ(big, 0);
    EXPECT_TRUE(is_inplace(big));
}

TEST(Swap, MultiLimbInline) {
    big_int_256 a = two_pow_256(200) + 12345;
    big_int_256 b{7};
    a.swap(b);
    EXPECT_EQ(a, 7);
    EXPECT_EQ(b, two_pow_256(200) + 12345);
    EXPECT_TRUE(is_inplace(a));
    EXPECT_TRUE(is_inplace(b));
}

TEST(Swap, Mixed256) {
    big_int_256 inline_val = two_pow_256(100);
    big_int_256 heap_val   = two_pow_256(400);
    inline_val.swap(heap_val);
    EXPECT_EQ(inline_val, two_pow_256(400));
    EXPECT_EQ(heap_val, two_pow_256(100));
    EXPECT_FALSE(is_inplace(inline_val));
    EXPECT_TRUE(is_inplace(heap_val));
}

TEST(Swap, SelfSwapIsNoOp) {
    big_int inline_val{42};
    inline_val.swap(inline_val);
    EXPECT_EQ(inline_val, 42);

    big_int heap_val = two_pow(90);
    heap_val.swap(heap_val);
    EXPECT_EQ(heap_val, two_pow(90));
}

TEST(Swap, CapacitiesFollowStorage) {
    big_int inline_val{42};
    big_int heap_val = two_pow(200);
    ASSERT_TRUE(is_inplace(inline_val));
    const std::size_t heap_cap = heap_val.representation_capacity();
    ASSERT_NE(heap_cap, 0U);

    inline_val.swap(heap_val);
    EXPECT_EQ(inline_val.representation_capacity(), heap_cap);
    EXPECT_TRUE(is_inplace(heap_val));
}

// The buffers adopted across a swap are fully owned: further growth and
// arithmetic on both operands behaves normally.
TEST(Swap, SwappedValuesRemainUsable) {
    big_int a = two_pow(200);
    big_int b{42};
    a.swap(b);
    a += 1;
    b += 1;
    EXPECT_EQ(a, 43);
    EXPECT_EQ(b, two_pow(200) + 1);
}

// ----- runtime tests, non-member swap -----

TEST(Swap, FreeSwapAllStorageModels) {
    big_int a{5};
    big_int b{42};
    swap(a, b);
    EXPECT_EQ(a, 42);
    EXPECT_EQ(b, 5);
    EXPECT_TRUE(is_inplace(a));
    EXPECT_TRUE(is_inplace(b));

    big_int heap_a = two_pow(100);
    big_int heap_b = two_pow(200);
    swap(heap_a, heap_b);
    EXPECT_EQ(heap_a, two_pow(200));
    EXPECT_EQ(heap_b, two_pow(100));
    EXPECT_FALSE(is_inplace(heap_a));
    EXPECT_FALSE(is_inplace(heap_b));

    big_int small{-7};
    big_int large = two_pow(150);
    swap(small, large);
    EXPECT_EQ(small, two_pow(150));
    EXPECT_EQ(large, -7);
    EXPECT_FALSE(is_inplace(small));
    EXPECT_TRUE(is_inplace(large));
}

TEST(Swap, FreeSwapSelfIsNoOp) {
    big_int inline_val{42};
    swap(inline_val, inline_val);
    EXPECT_EQ(inline_val, 42);

    big_int heap_val = two_pow(90);
    swap(heap_val, heap_val);
    EXPECT_EQ(heap_val, two_pow(90));
}

TEST(Swap, FreeSwap256) {
    big_int_256 inline_val = two_pow_256(100);
    big_int_256 heap_val   = two_pow_256(400);
    swap(inline_val, heap_val);
    EXPECT_EQ(inline_val, two_pow_256(400));
    EXPECT_EQ(heap_val, two_pow_256(100));
    EXPECT_FALSE(is_inplace(inline_val));
    EXPECT_TRUE(is_inplace(heap_val));
}

TEST(Swap, FreeSwapGenericIdiomAndRangesSwap) {
    big_int a{5};
    big_int b = two_pow(200);
    generic_swap(a, b);
    EXPECT_EQ(a, two_pow(200));
    EXPECT_EQ(b, 5);

    std::ranges::swap(a, b);
    EXPECT_EQ(a, 5);
    EXPECT_EQ(b, two_pow(200));
}

// Standard algorithms that exchange elements route through the overload.
TEST(Swap, FreeSwapFromStandardAlgorithm) {
    std::array<big_int, 3> lhs{big_int{1}, two_pow(200), big_int{3}};
    std::array<big_int, 3> rhs{two_pow(300), big_int{4}, two_pow(400)};
    std::swap_ranges(lhs.begin(), lhs.end(), rhs.begin());
    EXPECT_EQ(lhs[0], two_pow(300));
    EXPECT_EQ(lhs[1], 4);
    EXPECT_EQ(lhs[2], two_pow(400));
    EXPECT_EQ(rhs[0], 1);
    EXPECT_EQ(rhs[1], two_pow(200));
    EXPECT_EQ(rhs[2], 3);
}

// ----- allocator-aware behaviour -----

// A counting pmr resource: every allocation is matched by a deallocation, so a
// leak or double-free in swap shows up as an imbalance or as nonzero live bytes.
class counting_resource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t alloc_count() const noexcept { return m_allocs; }
    [[nodiscard]] std::size_t dealloc_count() const noexcept { return m_deallocs; }
    [[nodiscard]] std::size_t live_bytes() const noexcept { return m_live_bytes; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        ++m_allocs;
        m_live_bytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        ++m_deallocs;
        m_live_bytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::size_t m_allocs{0};
    std::size_t m_deallocs{0};
    std::size_t m_live_bytes{0};
};

// pmr does not propagate on swap, so the two operands must share a resource.
// The swap is then a pure exchange of storage: no allocation, no deallocation,
// the allocators are left in place, and every buffer is reclaimed at scope exit.
TEST(Swap, PmrSharedResourceNoLeak) {
    counting_resource cr;
    {
        pmr_big_int a{1, &cr};
        pmr_big_int b{1, &cr};
        a <<= 200; // heap, served by cr
        b <<= 300; // heap, served by cr

        const std::size_t allocs = cr.alloc_count();
        ASSERT_GE(allocs, 2U);

        a.swap(b);

        EXPECT_EQ(cr.alloc_count(), allocs); // swap allocated nothing
        EXPECT_EQ(cr.dealloc_count(), 0U);   // swap deallocated nothing
        EXPECT_EQ(to_string(a), to_string(two_pow(300)));
        EXPECT_EQ(to_string(b), to_string(two_pow(200)));
        EXPECT_EQ(a.get_allocator().resource(), &cr); // allocators not swapped
        EXPECT_EQ(b.get_allocator().resource(), &cr);
    }
    EXPECT_EQ(cr.alloc_count(), cr.dealloc_count());
    EXPECT_EQ(cr.live_bytes(), 0U);
}

// A stateful allocator that propagates on swap. The id lets the test observe
// that the allocators themselves are exchanged. Sized to a machine word so the
// owning basic_big_int has no trailing padding (keeps GCC's -Wpadded happy).
template <class T>
struct pocs_alloc {
    using value_type                  = T;
    using propagate_on_container_swap = std::true_type;

    std::size_t id = 0;

    pocs_alloc() = default;
    explicit pocs_alloc(std::size_t allocator_id) noexcept : id{allocator_id} {}
    template <class U>
    pocs_alloc(const pocs_alloc<U>& other) noexcept : id{other.id} {}

    [[nodiscard]] T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void             deallocate(T* p, std::size_t n) noexcept { std::allocator<T>{}.deallocate(p, n); }

    template <class U>
    bool operator==(const pocs_alloc<U>& other) const noexcept {
        return id == other.id;
    }
};

using pocs_big_int = basic_big_int<64, uint_multiprecision_t, pocs_alloc<uint_multiprecision_t>>;

// propagate_on_container_swap is true and the allocator is stateful, so the
// member swap is still unconditionally noexcept.
static_assert(noexcept(std::declval<pocs_big_int&>().swap(std::declval<pocs_big_int&>())));

TEST(Swap, PropagateOnContainerSwapExchangesAllocators) {
    pocs_big_int a{5, pocs_alloc<uint_multiprecision_t>{1U}};
    pocs_big_int b{42, pocs_alloc<uint_multiprecision_t>{2U}};
    a.swap(b);
    EXPECT_EQ(a, 42);
    EXPECT_EQ(b, 5);
    // POCS == true: the allocators travel with the values.
    EXPECT_EQ(a.get_allocator().id, 2U);
    EXPECT_EQ(b.get_allocator().id, 1U);
}

// The overload is nothrow for this allocator too, since the member is.
static_assert(std::is_nothrow_swappable_v<pocs_big_int>);

// A stateful allocator makes the choice of overload visible: the move-based
// `std::swap` leaves the allocators where they are, so the exchanged ids show
// that the `using std::swap` idiom selected the non-member overload.
TEST(Swap, FreeSwapPropagatesAllocators) {
    pocs_big_int a{5, pocs_alloc<uint_multiprecision_t>{1U}};
    pocs_big_int b{42, pocs_alloc<uint_multiprecision_t>{2U}};
    generic_swap(a, b);
    EXPECT_EQ(a, 42);
    EXPECT_EQ(b, 5);
    EXPECT_EQ(a.get_allocator().id, 2U);
    EXPECT_EQ(b.get_allocator().id, 1U);

    pocs_big_int heap_a{1, pocs_alloc<uint_multiprecision_t>{3U}};
    pocs_big_int heap_b{1, pocs_alloc<uint_multiprecision_t>{4U}};
    heap_a <<= 200; // heap, produced by allocator id 3
    heap_b <<= 300; // heap, produced by allocator id 4
    swap(heap_a, heap_b);
    EXPECT_EQ(to_string(heap_a), to_string(two_pow(300)));
    EXPECT_EQ(to_string(heap_b), to_string(two_pow(200)));
    EXPECT_EQ(heap_a.get_allocator().id, 4U);
    EXPECT_EQ(heap_b.get_allocator().id, 3U);
}

// With propagation on, each heap buffer ends up paired with the allocator that
// produced it, so destruction deallocates through the matching allocator.
// The non-member overload neither allocates nor deallocates, and leaves the
// non-propagating pmr allocators in place.
TEST(Swap, FreeSwapPmrSharedResourceNoLeak) {
    counting_resource cr;
    {
        pmr_big_int a{1, &cr};
        pmr_big_int b{1, &cr};
        a <<= 200;
        b <<= 300;

        const std::size_t allocs = cr.alloc_count();
        ASSERT_GE(allocs, 2U);

        swap(a, b);

        EXPECT_EQ(cr.alloc_count(), allocs);
        EXPECT_EQ(cr.dealloc_count(), 0U);
        EXPECT_EQ(to_string(a), to_string(two_pow(300)));
        EXPECT_EQ(to_string(b), to_string(two_pow(200)));
        EXPECT_EQ(a.get_allocator().resource(), &cr);
        EXPECT_EQ(b.get_allocator().resource(), &cr);
    }
    EXPECT_EQ(cr.alloc_count(), cr.dealloc_count());
    EXPECT_EQ(cr.live_bytes(), 0U);
}

TEST(Swap, PropagateOnContainerSwapHeapBuffers) {
    pocs_big_int a{1, pocs_alloc<uint_multiprecision_t>{1U}};
    pocs_big_int b{1, pocs_alloc<uint_multiprecision_t>{2U}};
    a <<= 200; // heap, produced by allocator id 1
    b <<= 300; // heap, produced by allocator id 2
    a.swap(b);
    EXPECT_EQ(to_string(a), to_string(two_pow(300)));
    EXPECT_EQ(to_string(b), to_string(two_pow(200)));
    EXPECT_EQ(a.get_allocator().id, 2U);
    EXPECT_EQ(b.get_allocator().id, 1U);
}

} // namespace
