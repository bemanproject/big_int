// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory_resource>
#include <new>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using beman::big_int::basic_big_int;
using beman::big_int::big_int;
using beman::big_int::from_chars;
using beman::big_int::to_chars;
using beman::big_int::to_string;
using beman::big_int::uint_multiprecision_t;
using namespace beman::big_int::literals;

using pmr_big_int = beman::big_int::pmr::big_int;
template <std::size_t b>
using pmr_basic_big_int = beman::big_int::pmr::basic_big_int<b>;
using poly_alloc        = std::pmr::polymorphic_allocator<uint_multiprecision_t>;

// ----- A counting/tracking pmr resource -----
// Used so tests can verify that allocations and deallocations actually flow
// through the user-supplied memory_resource (rather than e.g. the global heap)
// and that the deallocation count matches the allocation count when a big_int
// goes out of scope.
class counting_resource final : public std::pmr::memory_resource {
  public:
    explicit counting_resource(std::pmr::memory_resource* upstream = std::pmr::new_delete_resource()) noexcept
        : m_upstream{upstream} {}

    // The resource doesn't own the bytes, so this is a total failure if they are leaked
    ~counting_resource() override {
        if (m_live_bytes != 0) {
            ADD_FAILURE() << "counting_resource destroyed with " << m_live_bytes << " live bytes ("
                          << (m_allocs - m_deallocs) << " outstanding allocations)";
        }
    }

    counting_resource(const counting_resource&)            = delete;
    counting_resource& operator=(const counting_resource&) = delete;
    counting_resource(counting_resource&&)                 = delete;
    counting_resource& operator=(counting_resource&&)      = delete;

    [[nodiscard]] std::size_t alloc_count() const noexcept { return m_allocs; }
    [[nodiscard]] std::size_t dealloc_count() const noexcept { return m_deallocs; }
    [[nodiscard]] std::size_t live_bytes() const noexcept { return m_live_bytes; }

  private:
    [[nodiscard]] void* do_allocate(const std::size_t bytes, const std::size_t align) override {
        ++m_allocs;
        m_live_bytes += bytes;
        return m_upstream->allocate(bytes, align);
    }

    void do_deallocate(void* p, const std::size_t bytes, const std::size_t align) override {
        ++m_deallocs;
        m_live_bytes -= bytes;
        m_upstream->deallocate(p, bytes, align);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* m_upstream;
    std::size_t                m_allocs{0};
    std::size_t                m_deallocs{0};
    std::size_t                m_live_bytes{0};
};

// ----- Type-level checks -----

static_assert(std::is_same_v<pmr_big_int, basic_big_int<64, uint_multiprecision_t, poly_alloc>>);
static_assert(std::is_same_v<typename pmr_big_int::allocator_type, poly_alloc>);
static_assert(std::is_same_v<pmr_basic_big_int<256>, basic_big_int<256, uint_multiprecision_t, poly_alloc>>);
static_assert(std::is_same_v<pmr_basic_big_int<1024>, basic_big_int<1024, uint_multiprecision_t, poly_alloc>>);

// Allocator-aware: enables uses-allocator construction inside pmr containers.
static_assert(std::uses_allocator_v<pmr_big_int, poly_alloc>);
static_assert(std::uses_allocator_v<pmr_big_int, std::pmr::polymorphic_allocator<std::byte>>);
static_assert(std::uses_allocator_v<pmr_basic_big_int<256>, poly_alloc>);

// std::hash specialization works on the pmr instantiation.
static_assert(std::is_default_constructible_v<std::hash<pmr_big_int>>);
static_assert(std::is_copy_constructible_v<std::hash<pmr_big_int>>);
static_assert(std::is_nothrow_invocable_r_v<std::size_t, std::hash<pmr_big_int>, const pmr_big_int&>);
static_assert(std::is_default_constructible_v<std::hash<pmr_basic_big_int<256>>>);

// Copy/move semantics carry over from basic_big_int unchanged.
static_assert(std::is_default_constructible_v<pmr_big_int>);
static_assert(std::is_nothrow_default_constructible_v<pmr_big_int>);
static_assert(std::is_copy_constructible_v<pmr_big_int>);
static_assert(std::is_move_constructible_v<pmr_big_int>);
static_assert(std::is_copy_assignable_v<pmr_big_int>);
static_assert(std::is_move_assignable_v<pmr_big_int>);
static_assert(std::is_nothrow_move_constructible_v<pmr_big_int>);
static_assert(std::is_nothrow_move_assignable_v<pmr_big_int>);
static_assert(std::is_destructible_v<pmr_big_int>);

// Implicit conversion from a non-pmr big_int is disallowed (different allocator
// type); explicit construction must be used.
static_assert(!std::is_convertible_v<big_int, pmr_big_int>);
static_assert(std::is_constructible_v<pmr_big_int, const big_int&>);
static_assert(std::is_constructible_v<pmr_big_int, const big_int&, const poly_alloc&>);

// Implicit integral construction is preserved.
static_assert(std::is_convertible_v<int, pmr_big_int>);
static_assert(std::is_convertible_v<std::uint64_t, pmr_big_int>);

// ----- Default construction uses the default pmr resource -----

TEST(Pmr, DefaultConstructionUsesDefaultResource) {
    const pmr_big_int x;
    EXPECT_EQ(x.size(), 0U);
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x.get_allocator().resource(), std::pmr::get_default_resource());
}

TEST(Pmr, AllocatorOnlyConstruction) {
    counting_resource cr;
    const pmr_big_int x{poly_alloc{&cr}};
    EXPECT_EQ(x.size(), 0U);
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    EXPECT_EQ(cr.alloc_count(), 0U);
}

// ----- Construction from integers with explicit resource -----

TEST(Pmr, IntegralConstructionWithResource) {
    counting_resource cr;
    const pmr_big_int x{42, &cr};
    EXPECT_EQ(static_cast<int>(x), 42);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    EXPECT_EQ(cr.alloc_count(), 0U); // 42 fits in inline storage
}

TEST(Pmr, IntegralConstructionNegativeWithResource) {
    counting_resource cr;
    const pmr_big_int x{-12345, &cr};
    EXPECT_EQ(static_cast<int>(x), -12345);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, UnsignedIntegralConstructionWithResource) {
    counting_resource cr;
    const pmr_big_int x{std::numeric_limits<std::uint64_t>::max(), &cr};
    EXPECT_EQ(static_cast<std::uint64_t>(x), std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    // Single limb still fits inline.
    EXPECT_EQ(cr.alloc_count(), 0U);
}

// ----- Construction from another (non-pmr) big_int -----
//
// `basic_big_int<64, uint_multiprecision_t, std::pmr::polymorphic_allocator<...>>` is a distinct type
// from `basic_big_int<64, uint_multiprecision_t, std::allocator<...>>`, so this conversion goes
// through the explicit cross-instantiation constructor.

TEST(Pmr, ConstructFromNonPmrBigInt) {
    const big_int     src{12345};
    counting_resource cr;
    const pmr_big_int dst{src, &cr};
    EXPECT_EQ(static_cast<int>(dst), 12345);
    EXPECT_EQ(dst.get_allocator().resource(), &cr);
}

TEST(Pmr, ConstructFromLargeNonPmrBigInt) {
    // A literal value that needs more than one 64-bit limb.
    const big_int     big_src  = 0x1234'5678'9ABC'DEF0'1234'5678'9ABC'DEF0_n;
    const std::string expected = to_string(big_src);
    counting_resource cr;
    const pmr_big_int dst{big_src, &cr};
    EXPECT_EQ(to_string(dst), expected);
    EXPECT_EQ(dst.get_allocator().resource(), &cr);
    // Multi-limb construction routed allocation through cr.
    EXPECT_GE(cr.alloc_count(), 1U);
}

TEST(Pmr, ConstructFromNonPmrBigIntWithDifferentInplaceBits) {
    const basic_big_int<256> src{0xDEADBEEFU};
    counting_resource        cr;
    const pmr_big_int        dst{src, &cr};
    EXPECT_EQ(static_cast<std::uint32_t>(dst), 0xDEADBEEFU);
    EXPECT_EQ(dst.get_allocator().resource(), &cr);
}

// ----- Construction from a range / iterator pair -----

TEST(Pmr, FromIteratorPairWithResource) {
    counting_resource                                    cr;
    std::array<beman::big_int::uint_multiprecision_t, 3> limbs{
        0x1111'1111'1111'1111ULL, 0x2222'2222'2222'2222ULL, 0x3333'3333'3333'3333ULL};
    const pmr_big_int x{limbs.begin(), limbs.end(), &cr};
    EXPECT_EQ(x.representation().size(), 3U);
    EXPECT_EQ(x.representation()[0], 0x1111'1111'1111'1111ULL);
    EXPECT_EQ(x.representation()[1], 0x2222'2222'2222'2222ULL);
    EXPECT_EQ(x.representation()[2], 0x3333'3333'3333'3333ULL);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    // 3 limbs > 1-limb inline => allocation goes through cr.
    EXPECT_GE(cr.alloc_count(), 1U);
}

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
TEST(Pmr, FromRangeWithResource) {
    counting_resource                                    cr;
    std::array<beman::big_int::uint_multiprecision_t, 2> limbs{0xDEADBEEFU, 0xCAFEBABEU};
    const pmr_big_int                                    x{std::from_range, limbs, &cr};
    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0xDEADBEEFU);
    EXPECT_EQ(x.representation()[1], 0xCAFEBABEU);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    EXPECT_GE(cr.alloc_count(), 1U);
}
#endif

// ----- Allocator propagation through copy/move -----

TEST(Pmr, CopyConstructionSelectsDefaultResource) {
    // The copy constructor goes through
    // `select_on_container_copy_construction`, which for
    // `polymorphic_allocator` returns a default-constructed allocator: the copy
    // lands on the default resource, as it does for the pmr containers.
    counting_resource cr;
    const pmr_big_int src{0xFFFF'FFFF'FFFF'FFFFULL, &cr};
    const pmr_big_int copy{src};
    EXPECT_EQ(copy.get_allocator().resource(), std::pmr::get_default_resource());
    EXPECT_EQ(copy, src);
}

TEST(Pmr, CopyConstructionLargeValueSelectsDefaultResource) {
    counting_resource cr;
    pmr_big_int       src{1, &cr};
    src <<= 200; // forces heap allocation
    EXPECT_GE(cr.alloc_count(), 1U);
    const auto alloc_count_before_copy = cr.alloc_count();

    const pmr_big_int copy{src};
    EXPECT_EQ(copy.get_allocator().resource(), std::pmr::get_default_resource());
    EXPECT_EQ(copy, src);
    // The copy allocates from the default resource, so `cr` sees nothing.
    EXPECT_EQ(cr.alloc_count(), alloc_count_before_copy);
}

TEST(Pmr, AllocatorExtendedCopyConstructionKeepsResource) {
    // Naming the allocator is how a caller keeps a copy on the same resource.
    counting_resource cr;
    pmr_big_int       src{1, &cr};
    src <<= 200;
    const auto alloc_count_before_copy = cr.alloc_count();

    const pmr_big_int copy{src, &cr};
    EXPECT_EQ(copy.get_allocator().resource(), &cr);
    EXPECT_EQ(copy, src);
    EXPECT_GT(cr.alloc_count(), alloc_count_before_copy);
}

TEST(Pmr, UnaryOperatorsSelectDefaultResource) {
    // The `const&` overloads copy, so the result's allocator comes from
    // `select_on_container_copy_construction`, just as the copy constructor's
    // does.
    counting_resource cr;
    pmr_big_int       x{1, &cr};
    x <<= 200;
    auto* const default_resource = std::pmr::get_default_resource();

    EXPECT_EQ((+x).get_allocator().resource(), default_resource);
    EXPECT_EQ((-x).get_allocator().resource(), default_resource);
    EXPECT_EQ((~x).get_allocator().resource(), default_resource);
    EXPECT_EQ((x++).get_allocator().resource(), default_resource);
    EXPECT_EQ((x--).get_allocator().resource(), default_resource);
    // The operand itself is untouched by the selection.
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, UnaryOperatorsOnRvalueKeepResource) {
    // The `&&` overloads take over the operand's storage, so the resource comes
    // along with it.
    counting_resource cr;
    pmr_big_int       x{1, &cr};
    x <<= 200;
    const pmr_big_int expected = -x;

    const pmr_big_int negated = -std::move(x);

    EXPECT_EQ(negated, expected);
    EXPECT_EQ(negated.get_allocator().resource(), &cr);
}

TEST(Pmr, MoveConstructionPropagatesResource) {
    counting_resource cr;
    pmr_big_int       src{1, &cr};
    src <<= 200; // heap
    const auto alloc_count_before_move = cr.alloc_count();

    const pmr_big_int moved{std::move(src)};
    EXPECT_EQ(moved.get_allocator().resource(), &cr);
    // Move steals the heap buffer rather than allocating fresh storage.
    EXPECT_EQ(cr.alloc_count(), alloc_count_before_move);
}

TEST(Pmr, AllocatorExtendedCopyConstructionOntoOtherResource) {
    counting_resource src_cr;
    counting_resource dst_cr;
    pmr_big_int       src{1, &src_cr};
    src <<= 200;
    const auto src_alloc_count = src_cr.alloc_count();

    const pmr_big_int copy{src, &dst_cr};
    EXPECT_EQ(copy.get_allocator().resource(), &dst_cr);
    EXPECT_EQ(copy, src);
    EXPECT_EQ(dst_cr.alloc_count(), 1U);
    EXPECT_EQ(src_cr.alloc_count(), src_alloc_count);
}

TEST(Pmr, AllocatorExtendedMoveConstructionOnSameResourceTakesBuffer) {
    counting_resource cr;
    pmr_big_int       src{1, &cr};
    src <<= 200;
    const pmr_big_int expected{src, &cr};
    const auto        alloc_count_before_move = cr.alloc_count();

    const pmr_big_int moved{std::move(src), &cr};
    EXPECT_EQ(moved.get_allocator().resource(), &cr);
    EXPECT_EQ(moved, expected);
    EXPECT_EQ(cr.alloc_count(), alloc_count_before_move);
}

TEST(Pmr, AllocatorExtendedMoveConstructionOntoOtherResourceCopies) {
    // A resource that did not allocate the buffer cannot free it, so the value
    // is copied onto the named resource instead of being taken over.
    counting_resource src_cr;
    counting_resource dst_cr;
    pmr_big_int       src{1, &src_cr};
    src <<= 200;
    const pmr_big_int expected{src, &src_cr};

    const pmr_big_int moved{std::move(src), &dst_cr};
    EXPECT_EQ(moved.get_allocator().resource(), &dst_cr);
    EXPECT_EQ(moved, expected);
    EXPECT_EQ(dst_cr.alloc_count(), 1U);
}

TEST(Pmr, AllocatorExtendedConstructionDeducesTypeFromValue) {
    // The allocator parameter is a non-deduced context, so class template
    // argument deduction takes the type from `x` and converts the resource.
    counting_resource cr;
    pmr_big_int       x{7, &cr};

    const beman::big_int::basic_big_int copy(x, &cr);
    const beman::big_int::basic_big_int moved(std::move(x), &cr);
    static_assert(std::is_same_v<decltype(copy), const pmr_big_int>);
    static_assert(std::is_same_v<decltype(moved), const pmr_big_int>);
    EXPECT_EQ(copy, 7);
    EXPECT_EQ(moved, 7);
    EXPECT_EQ(moved.get_allocator().resource(), &cr);
}

// ----- Resource lifecycle: every allocation is matched by a deallocation -----

TEST(Pmr, ScopeExitDeallocatesAllAllocations) {
    counting_resource cr;
    {
        pmr_big_int x{42, &cr};
        x.reserve_representation(16);
    }
    EXPECT_GE(cr.alloc_count(), 1U);
    EXPECT_EQ(cr.alloc_count(), cr.dealloc_count());
    EXPECT_EQ(cr.live_bytes(), 0U);
}

TEST(Pmr, MultipleObjectsShareSingleResource) {
    counting_resource cr;
    {
        pmr_big_int a{1, &cr};
        pmr_big_int b{2, &cr};
        pmr_big_int c{3, &cr};
        a <<= 200;
        b <<= 300;
        c <<= 400;
    }
    EXPECT_GE(cr.alloc_count(), 3U);
    EXPECT_EQ(cr.alloc_count(), cr.dealloc_count());
    EXPECT_EQ(cr.live_bytes(), 0U);
}

// ----- get_allocator() returns the configured allocator -----

TEST(Pmr, GetAllocatorReturnsConfiguredResource) {
    counting_resource cr;
    const pmr_big_int x{42, &cr};
    const poly_alloc  a = x.get_allocator();
    EXPECT_EQ(a.resource(), &cr);
}

TEST(Pmr, AllocatorEqualityFollowsResourceEquality) {
    counting_resource cr1;
    counting_resource cr2;
    const pmr_big_int x{1, &cr1};
    const pmr_big_int y{1, &cr1};
    const pmr_big_int z{1, &cr2};
    EXPECT_TRUE(x.get_allocator() == y.get_allocator());
    EXPECT_FALSE(x.get_allocator() == z.get_allocator());
}

// ----- reserve() / shrink_to_fit() use the configured resource -----

TEST(Pmr, ReserveAllocatesOnConfiguredResource) {
    counting_resource cr;
    pmr_big_int       x{42, &cr};
    EXPECT_EQ(cr.alloc_count(), 0U);
    x.reserve_representation(16);
    EXPECT_EQ(cr.alloc_count(), 1U);
    EXPECT_GE(x.representation_capacity(), 16U);
}

TEST(Pmr, ReserveBeyondCurrentReallocatesOnSameResource) {
    counting_resource cr;
    pmr_big_int       x{42, &cr};
    x.reserve_representation(8);
    EXPECT_EQ(cr.alloc_count(), 1U);
    x.reserve_representation(64);
    // A second allocation occurred on the same resource (and the first was freed).
    EXPECT_GE(cr.alloc_count(), 2U);
}

TEST(Pmr, ShrinkToFitDeallocatesViaConfiguredResource) {
    counting_resource cr;
    pmr_big_int       x{42, &cr};
    x.reserve_representation(16);
    EXPECT_EQ(cr.alloc_count(), 1U);
    EXPECT_EQ(cr.dealloc_count(), 0U);
    x.shrink_to_fit(); // 42 fits inline; the heap buffer is returned to cr.
    EXPECT_EQ(cr.dealloc_count(), 1U);
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(static_cast<int>(x), 42);
}

// ----- Arithmetic operations: value correctness -----

TEST(Pmr, AdditionSameResource) {
    counting_resource cr;
    const pmr_big_int a{100, &cr};
    const pmr_big_int b{42, &cr};
    const pmr_big_int sum = a + b;
    EXPECT_EQ(static_cast<int>(sum), 142);
}

TEST(Pmr, SubtractionSameResource) {
    counting_resource cr;
    const pmr_big_int a{200, &cr};
    const pmr_big_int b{75, &cr};
    EXPECT_EQ(static_cast<int>(a - b), 125);
}

TEST(Pmr, MultiplicationSameResource) {
    counting_resource cr;
    const pmr_big_int a{12345, &cr};
    const pmr_big_int b{6789, &cr};
    EXPECT_EQ(static_cast<long long>(a * b), 83'810'205LL);
}

TEST(Pmr, DivisionAndModulusSameResource) {
    counting_resource cr;
    const pmr_big_int a{1000, &cr};
    const pmr_big_int b{7, &cr};
    EXPECT_EQ(static_cast<int>(a / b), 142);
    EXPECT_EQ(static_cast<int>(a % b), 6);
}

TEST(Pmr, DivideAndConquerDivisionSameResource) {
    // Operand sizes above the burnikel_ziegler_cutoff/offset gates on every
    // architecture (the x86-64 gates sit at 160/64 limbs). Compound
    // assignment divides through *this, so the divide-and-conquer working
    // memory must flow through the configured resource (counting_resource
    // fails the test on any leak at destruction).
    counting_resource cr;
    const big_int     ref_a = ((1_n << 25600) + 987654321_n) * ((1_n << 4801) + 12345_n);
    const big_int     ref_b = (1_n << 12800) + 192837465_n;

    const pmr_big_int b{ref_b, &cr};

    pmr_big_int       q{ref_a, &cr};
    const std::size_t allocs_before = cr.alloc_count();
    q /= b;
    EXPECT_GT(cr.alloc_count(), allocs_before);
    EXPECT_EQ(to_string(q), to_string(ref_a / ref_b));

    pmr_big_int r{ref_a, &cr};
    r %= b;
    EXPECT_EQ(to_string(r), to_string(ref_a % ref_b));
}

TEST(Pmr, MultiLimbArithmeticMatchesNonPmr) {
    counting_resource cr;
    // Use the same numeric value in pmr and non-pmr forms; arithmetic must agree.
    const big_int ref_a   = 1'234'567'890'123'456'789_n * 11_n;
    const big_int ref_b   = 9'876'543'210'987'654'321_n;
    const big_int ref_sum = ref_a + ref_b;
    const big_int ref_mul = ref_a * ref_b;

    const pmr_big_int a{ref_a, &cr};
    const pmr_big_int b{ref_b, &cr};
    EXPECT_EQ(to_string(a + b), to_string(ref_sum));
    EXPECT_EQ(to_string(a * b), to_string(ref_mul));
}

TEST(Pmr, UnaryOperators) {
    const pmr_big_int x{42};
    EXPECT_EQ(static_cast<int>(+x), 42);
    EXPECT_EQ(static_cast<int>(-x), -42);
    EXPECT_EQ(static_cast<int>(-(-x)), 42);
    EXPECT_EQ(static_cast<int>(~x), ~42);
}

TEST(Pmr, IncrementDecrement) {
    pmr_big_int x{10};
    EXPECT_EQ(static_cast<int>(++x), 11);
    EXPECT_EQ(static_cast<int>(x), 11);
    EXPECT_EQ(static_cast<int>(x++), 11);
    EXPECT_EQ(static_cast<int>(x), 12);
    EXPECT_EQ(static_cast<int>(--x), 11);
    EXPECT_EQ(static_cast<int>(x--), 11);
    EXPECT_EQ(static_cast<int>(x), 10);
}

// ----- Compound assignment preserves the destination allocator -----
//
// `add_in_place` (the core of `+=` / `-=`) mutates `*this` directly, so the
// lhs's allocator survives the assignment.

TEST(Pmr, CompoundAddPreservesAllocator) {
    counting_resource cr;
    pmr_big_int       x{100, &cr};
    const pmr_big_int y{42, &cr};
    x += y;
    EXPECT_EQ(static_cast<int>(x), 142);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, CompoundSubPreservesAllocator) {
    counting_resource cr;
    pmr_big_int       x{100, &cr};
    x -= pmr_big_int{42, &cr};
    EXPECT_EQ(static_cast<int>(x), 58);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, CompoundAddWithIntegerPreservesAllocator) {
    counting_resource cr;
    pmr_big_int       x{100, &cr};
    x += 42;
    EXPECT_EQ(static_cast<int>(x), 142);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, CompoundAddGrowsViaSameResource) {
    counting_resource cr;
    pmr_big_int       x{std::numeric_limits<std::uint64_t>::max(), &cr};
    EXPECT_EQ(cr.alloc_count(), 0U); // inline
    x += pmr_big_int{1, &cr};        // grows to 2 limbs -> heap allocation on cr
    EXPECT_GE(cr.alloc_count(), 1U);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    EXPECT_EQ(x.representation().size(), 2U);
}

// ----- Bitwise / shift operations -----

TEST(Pmr, BitwiseAndOrXor) {
    counting_resource cr;
    const pmr_big_int a{0xF0F0, &cr};
    const pmr_big_int b{0x0FF0, &cr};
    EXPECT_EQ(static_cast<int>(a & b), 0x00F0);
    EXPECT_EQ(static_cast<int>(a | b), 0xFFF0);
    EXPECT_EQ(static_cast<int>(a ^ b), 0xFF00);
}

TEST(Pmr, CompoundBitwisePreserveAllocator) {
    counting_resource cr;
    pmr_big_int       x{0xF0F0, &cr};
    x &= pmr_big_int{0x0FF0, &cr};
    EXPECT_EQ(static_cast<int>(x), 0x00F0);
    EXPECT_EQ(x.get_allocator().resource(), &cr);

    x = pmr_big_int{0xF0F0, &cr};
    x |= pmr_big_int{0x0FF0, &cr};
    EXPECT_EQ(static_cast<int>(x), 0xFFF0);
    EXPECT_EQ(x.get_allocator().resource(), &cr);

    x = pmr_big_int{0xF0F0, &cr};
    x ^= pmr_big_int{0x0FF0, &cr};
    EXPECT_EQ(static_cast<int>(x), 0xFF00);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, ShiftLeftAllocatesOnConfiguredResource) {
    counting_resource cr;
    pmr_big_int       x{1, &cr};
    EXPECT_EQ(cr.alloc_count(), 0U);
    x <<= 200;
    EXPECT_GE(cr.alloc_count(), 1U);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
    EXPECT_EQ(x.size(), 201U);
}

TEST(Pmr, ShiftRightPreservesAllocator) {
    counting_resource cr;
    pmr_big_int       x{1, &cr};
    x <<= 200;
    x >>= 100;
    EXPECT_EQ(x.size(), 101U);
    EXPECT_EQ(x.get_allocator().resource(), &cr);
}

TEST(Pmr, ShiftOperatorsMatchNonPmr) {
    const pmr_big_int x   = pmr_big_int{1} << 250;
    const big_int     ref = big_int{1} << 250;
    EXPECT_EQ(to_string(x), to_string(ref));
}

// ----- Comparisons between pmr big_ints, and between pmr and integers -----

TEST(Pmr, EqualityAndOrdering) {
    const pmr_big_int a{42};
    const pmr_big_int b{42};
    const pmr_big_int c{100};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a < c);
    EXPECT_TRUE(c > a);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(a >= b);
    EXPECT_EQ(a <=> b, std::strong_ordering::equal);
    EXPECT_EQ(a <=> c, std::strong_ordering::less);
}

TEST(Pmr, ComparisonAgainstInteger) {
    const pmr_big_int x{42};
    EXPECT_TRUE(x == 42);
    EXPECT_TRUE(x != 43);
    EXPECT_TRUE(x < 100);
    EXPECT_TRUE(x > 10);
    EXPECT_TRUE(42 == x);
    EXPECT_TRUE(100 > x);
}

TEST(Pmr, ComparisonAcrossDifferentResources) {
    // Two pmr_big_ints holding the same numeric value but allocated on
    // different resources must still compare equal (comparison is value-based).
    counting_resource cr1;
    counting_resource cr2;
    pmr_big_int       a{1, &cr1};
    pmr_big_int       b{1, &cr2};
    a <<= 200;
    b <<= 200;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a < b);
}

// ----- Conversions to fundamental types -----

TEST(Pmr, ExplicitConversionsToFundamentalTypes) {
    const pmr_big_int x{42};
    const pmr_big_int neg{-42};
    const pmr_big_int zero;

    EXPECT_EQ(static_cast<int>(x), 42);
    EXPECT_EQ(static_cast<int>(neg), -42);
    EXPECT_EQ(static_cast<long long>(x), 42LL);
    EXPECT_EQ(static_cast<unsigned int>(x), 42U);
    EXPECT_FALSE(static_cast<bool>(zero));
    EXPECT_TRUE(static_cast<bool>(x));
    EXPECT_TRUE(static_cast<bool>(neg));
}

// ----- String / charconv compatibility -----

TEST(Pmr, ToString) {
    const pmr_big_int x{12345};
    EXPECT_EQ(to_string(x), "12345");
    EXPECT_EQ(to_string(pmr_big_int{-12345}), "-12345");
    EXPECT_EQ(to_string(pmr_big_int{}), "0");
}

TEST(Pmr, ToStringMultiLimb) {
    counting_resource cr;
    pmr_big_int       x{1, &cr};
    x <<= 200;
    const big_int ref = big_int{1} << 200;
    EXPECT_EQ(to_string(x), to_string(ref));
}

TEST(Pmr, ToCharsRoundTrip) {
    counting_resource cr;
    pmr_big_int       x{1, &cr};
    x <<= 250;
    x += pmr_big_int{12345, &cr};

    std::array<char, 200> buf{};
    const auto [out_ptr, ec_out] = to_chars(buf.data(), buf.data() + buf.size(), x, 10);
    ASSERT_EQ(ec_out, std::errc{});

    pmr_big_int y{0, &cr};
    const auto [in_ptr, ec_in] = from_chars(buf.data(), out_ptr, y, 10);
    EXPECT_EQ(ec_in, std::errc{});
    EXPECT_EQ(in_ptr, out_ptr);
    EXPECT_EQ(x, y);
    EXPECT_EQ(y.get_allocator().resource(), &cr);
}

TEST(Pmr, FromCharsParseLargeIntoPmr) {
    const std::string s = to_string(big_int{1} << 300);
    counting_resource cr;
    pmr_big_int       y{0, &cr};
    const auto [p, ec] = from_chars(s.data(), s.data() + s.size(), y, 10);
    EXPECT_EQ(ec, std::errc{});
    EXPECT_EQ(p, s.data() + s.size());
    EXPECT_EQ(to_string(y), s);
    EXPECT_EQ(y.get_allocator().resource(), &cr);
    // Parsing a 300-bit value must have allocated on cr.
    EXPECT_GE(cr.alloc_count(), 1U);
}

// ----- std::hash for pmr_big_int -----

TEST(Pmr, HashIsDeterministic) {
    const std::hash<pmr_big_int> h{};
    counting_resource            cr;
    const pmr_big_int            x{12345, &cr};
    EXPECT_EQ(h(x), h(x));
}

TEST(Pmr, HashEqualsAcrossResources) {
    counting_resource            cr1;
    counting_resource            cr2;
    const pmr_big_int            a{0xCAFE'BABEU, &cr1};
    const pmr_big_int            b{0xCAFE'BABEU, &cr2};
    const std::hash<pmr_big_int> h{};
    EXPECT_EQ(h(a), h(b));
}

TEST(Pmr, HashEqualsBetweenPmrAndNonPmr) {
    // The hash is computed over the value alone, so a pmr value
    // and a non-pmr value that hold the same number must hash identically
    // (modulo the allocator type parameter on the std::hash<...> instantiation).
    const big_int     non_pmr{0xDEAD'BEEFU};
    const pmr_big_int pmr{0xDEAD'BEEFU};
    EXPECT_EQ(std::hash<big_int>{}(non_pmr), std::hash<pmr_big_int>{}(pmr));
}

TEST(Pmr, HashMultiLimbAcrossInplaceBits) {
    counting_resource cr;
    pmr_big_int       small{1, &cr};
    small <<= 200;
    pmr_basic_big_int<1024> wide{1, &cr};
    wide <<= 200;
    EXPECT_EQ(std::hash<pmr_big_int>{}(small), std::hash<pmr_basic_big_int<1024>>{}(wide));
}

// ----- Unordered containers using pmr_big_int as a key -----

TEST(Pmr, UnorderedSetWithPmrBigIntKeys) {
    std::unordered_set<pmr_big_int> s;
    s.insert(pmr_big_int{1});
    s.insert(pmr_big_int{2});
    s.insert(pmr_big_int{1}); // duplicate
    EXPECT_EQ(s.size(), 2U);
    EXPECT_TRUE(s.contains(pmr_big_int{1}));
    EXPECT_TRUE(s.contains(pmr_big_int{2}));
    EXPECT_FALSE(s.contains(pmr_big_int{0}));
}

TEST(Pmr, UnorderedMapMultiLimbKeys) {
    counting_resource                    cr;
    std::unordered_map<pmr_big_int, int> m;
    pmr_big_int                          k1{1, &cr};
    pmr_big_int                          k2{2, &cr};
    k1 <<= 200;
    k2 <<= 200;
    m[k1] = 10;
    m[k2] = 20;
    EXPECT_EQ(m.size(), 2U);
    EXPECT_EQ(m.at(k1), 10);
    EXPECT_EQ(m.at(k2), 20);
}

// ----- std::pmr container integration: uses-allocator construction -----
//
// When a pmr_big_int is emplaced into a std::pmr::vector<pmr_big_int>, the
// vector's allocator should propagate into the constructed element. Verify
// this by counting allocations on the resource shared with the vector.

TEST(Pmr, PmrVectorPropagatesResourceToElements) {
    counting_resource             cr;
    std::pmr::vector<pmr_big_int> v{&cr};
    v.reserve(4);

    // Emplace values that force heap allocation per element (> 1 limb).
    v.emplace_back(); // default-constructed
    v.back() = pmr_big_int{1};
    v.back() <<= 200;
    v.emplace_back();
    v.back() = pmr_big_int{1};
    v.back() <<= 250;
    v.emplace_back();
    v.back() = pmr_big_int{1};
    v.back() <<= 300;

    // Every element should report the vector's allocator's resource.
    for (const auto& x : v) {
        EXPECT_EQ(x.get_allocator().resource(), &cr);
    }
    // At least one allocation per multi-limb element, all on cr.
    EXPECT_GE(cr.alloc_count(), 3U);
}

TEST(Pmr, PmrVectorEmplaceBackWithInts) {
    counting_resource             cr;
    std::pmr::vector<pmr_big_int> v{&cr};
    v.emplace_back(42);
    v.emplace_back(-7);
    v.emplace_back(std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(static_cast<int>(v[0]), 42);
    EXPECT_EQ(static_cast<int>(v[1]), -7);
    EXPECT_EQ(static_cast<std::uint64_t>(v[2]), std::numeric_limits<std::uint64_t>::max());
    for (const auto& x : v) {
        EXPECT_EQ(x.get_allocator().resource(), &cr);
    }
}

TEST(Pmr, PmrVectorCopyBetweenResources) {
    counting_resource             src_cr;
    counting_resource             dst_cr;
    std::pmr::vector<pmr_big_int> src{&src_cr};
    pmr_big_int                   big_value{1, &src_cr};
    big_value <<= 200;
    src.push_back(big_value);
    src.emplace_back(42);

    // Copy-construct into a vector backed by a different resource. The pmr
    // container contract is that elements should be re-constructed using the
    // destination vector's allocator -- no element should still reference
    // src_cr after the copy.
    std::pmr::vector<pmr_big_int> dst{src, &dst_cr};
    EXPECT_EQ(dst.size(), src.size());
    for (const auto& x : dst) {
        EXPECT_EQ(x.get_allocator().resource(), &dst_cr);
    }
    EXPECT_EQ(dst[0], src[0]);
    EXPECT_EQ(dst[1], src[1]);
}

TEST(Pmr, PmrVectorMovesElementsWithoutAllocating) {
    // Uses-allocator construction builds an element as `T(std::move(x), alloc)`.
    // On the vector's own resource that takes the buffer over, so neither
    // inserting nor reallocating allocates anything for the elements.
    counting_resource             cr;
    std::pmr::vector<pmr_big_int> v{&cr};
    v.reserve(2);
    pmr_big_int x{1, &cr};
    x <<= 200;
    pmr_big_int y{1, &cr};
    y <<= 300;
    const auto alloc_count_before_insert = cr.alloc_count();

    v.push_back(std::move(x));
    v.push_back(std::move(y));
    EXPECT_EQ(cr.alloc_count(), alloc_count_before_insert);

    v.reserve(16); // only the new element array is allocated
    EXPECT_EQ(cr.alloc_count(), alloc_count_before_insert + 1U);
    EXPECT_EQ(v[0], pmr_big_int{1} << 200);
    EXPECT_EQ(v[1], pmr_big_int{1} << 300);
    for (const auto& element : v) {
        EXPECT_EQ(element.get_allocator().resource(), &cr);
    }
}

TEST(Pmr, PmrVectorMoveFromOtherResourceCopiesOntoVectorResource) {
    counting_resource             vector_cr;
    counting_resource             value_cr;
    std::pmr::vector<pmr_big_int> v{&vector_cr};
    v.reserve(1);
    pmr_big_int x{1, &value_cr};
    x <<= 200;
    const auto alloc_count_before_insert = vector_cr.alloc_count();

    v.push_back(std::move(x));
    EXPECT_EQ(v[0].get_allocator().resource(), &vector_cr);
    EXPECT_EQ(v[0], pmr_big_int{1} << 200);
    EXPECT_EQ(vector_cr.alloc_count(), alloc_count_before_insert + 1U);
}

// ----- Specialized resource types -----

TEST(Pmr, MonotonicBufferResource) {
    // A monotonic_buffer_resource trivially returns sub-allocations from a
    // pre-sized buffer; deallocate is a no-op, and release() frees everything.
    std::array<std::byte, 4096>         buffer{};
    std::pmr::monotonic_buffer_resource mbr{buffer.data(), buffer.size(), std::pmr::null_memory_resource()};
    {
        pmr_big_int x{1, &mbr};
        x <<= 500; // forces heap, served by mbr
        const std::string s = to_string(x);
        EXPECT_EQ(s, to_string(big_int{1} << 500));
        EXPECT_EQ(x.get_allocator().resource(), &mbr);
    }
    // No exceptions and no fallthrough to null_memory_resource is the success
    // criterion here -- the buffer was large enough for the value.
}

TEST(Pmr, UnsynchronizedPoolResource) {
    std::pmr::unsynchronized_pool_resource pool;
    pmr_big_int                            x{1, &pool};
    for (int i = 0; i < 16; ++i) {
        x += x; // doubles the value, eventually grows past inline
    }
    EXPECT_EQ(x.get_allocator().resource(), &pool);
    EXPECT_EQ(to_string(x), to_string(big_int{1} << 16));
}

TEST(Pmr, ThrowingAllocationDuringAssignmentLeavesValueUnchanged) {
    // A throwing allocation inside assignment leaves the destination untouched.
    pmr_big_int wide{};
    wide = 1;
    wide <<= 4000;

    {
        alignas(uint_multiprecision_t) unsigned char buffer[64];
        std::pmr::monotonic_buffer_resource          res{buffer, sizeof buffer, std::pmr::null_memory_resource()};
        pmr_big_int                                  x{&res};
        x = 1;
        x <<= 100;
        ASSERT_FALSE(is_inplace(x));
        const std::string before = to_string(x);

        EXPECT_THROW(x = wide, std::bad_alloc);

        EXPECT_EQ(to_string(x), before);
        x = 42;
        EXPECT_EQ(x, 42);
    }
    {
        std::pmr::monotonic_buffer_resource res{std::pmr::null_memory_resource()};
        pmr_big_int                         x{&res};
        x = 7;
        ASSERT_TRUE(is_inplace(x));

        EXPECT_THROW(x = wide, std::bad_alloc);

        EXPECT_EQ(x, 7);
        x = 42;
        EXPECT_EQ(x, 42);
    }
}

TEST(Pmr, NullMemoryResourceForcesInlineOnly) {
    // null_memory_resource throws bad_alloc on any allocation, so as long as
    // we keep the value within the single inline limb, no heap is touched.
    pmr_big_int x{42, std::pmr::null_memory_resource()};
    EXPECT_EQ(static_cast<int>(x), 42);
    x += pmr_big_int{100, std::pmr::null_memory_resource()};
    EXPECT_EQ(static_cast<int>(x), 142);
    // Triggering an allocation must throw.
    EXPECT_THROW(x.reserve_representation(8), std::bad_alloc);
}

// ----- Stream output via testing.hpp -----

TEST(Pmr, OstreamOutput) {
    const pmr_big_int  x{12345};
    std::ostringstream oss;
    oss << x;
    EXPECT_EQ(oss.str(), "12345");
}

} // namespace
