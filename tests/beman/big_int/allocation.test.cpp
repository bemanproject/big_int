// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

// ----- compile-time tests -----

consteval bool test_size_default() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    return x.size() == 0; // size() for zero returns 0 (consistent with D4444)
}
static_assert(test_size_default());

consteval bool test_size_from_value() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    return x.size() == 6; // size() returns msb + 1
}
static_assert(test_size_from_value());

consteval bool test_size_from_value_neg() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{-42};
    return x.size() == 6; // size() for x negative returns (-x).size()
}
static_assert(test_size_from_value_neg());

consteval bool test_size_from_value_big() {
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;
    BEMAN_BIG_INT_NAMESPACE::big_int x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    return x.size() == 3324; // size() returns msb + 1
}
static_assert(test_size_from_value_big());

consteval bool test_size_from_value_big_neg() {
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;
    BEMAN_BIG_INT_NAMESPACE::big_int x{
        -31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    return x.size() == 3324; // size() for x negative returns (-x).size()
}
static_assert(test_size_from_value_big_neg());

consteval bool test_max_size() {
    // max_size() is a bit count: max_representation_size() limbs times digits-per-limb.
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    constexpr std::size_t            digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    return x.max_size() == x.max_representation_size() * digits;
}
static_assert(test_max_size());

consteval bool test_reserve_bits_translates_to_limbs() {
    // reserve(n) treats n as a bit count: it reserves ceil(n / digits) limbs.
    constexpr std::size_t digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve(4U * digits); // four limbs' worth of bits
    return x.representation_capacity() >= 4U;
}
static_assert(test_reserve_bits_translates_to_limbs());

consteval bool test_capacity_default() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    return is_inplace(x); // inline storage, no allocation
}
static_assert(test_capacity_default());

consteval bool test_capacity_is_inplace_bits() {
    // capacity() is a bit count: in place it equals inplace_bits and tracks representation_capacity().
    constexpr std::size_t digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    return x.capacity() == BEMAN_BIG_INT_NAMESPACE::big_int::inplace_bits &&
           x.capacity() == x.representation_capacity() * digits;
}
static_assert(test_capacity_is_inplace_bits());

consteval bool test_reserve_within_inline() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(1); // fits in inline storage, should be a no-op
    return is_inplace(x);
}
static_assert(test_reserve_within_inline());

consteval bool test_reserve_beyond_inline() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(4);
    return x.representation_capacity() >= 4;
}
static_assert(test_reserve_beyond_inline());

consteval bool test_reserve_preserves_value() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    return x.representation()[0] == 42U && x.representation_capacity() >= 8;
}
static_assert(test_reserve_preserves_value());

consteval bool test_reserve_doubling() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(3); // first allocation: max(3, 1) = 3
    return x.representation_capacity() >= 3;
}
static_assert(test_reserve_doubling());

consteval bool test_reserve_grows_geometrically() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(4); // cap = max(4, 1)   = 4
    x.reserve_representation(5); // cap = max(5, 2*4) = 8
    return x.representation_capacity() == 8;
}
static_assert(test_reserve_grows_geometrically());

consteval bool test_reserve_no_shrink() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(10);
    auto cap = x.representation_capacity();
    x.reserve_representation(2); // should not shrink
    return x.representation_capacity() == cap;
}
static_assert(test_reserve_no_shrink());

consteval bool test_shrink_to_fit_noop_inline() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.shrink_to_fit(); // no-op on inline storage
    return is_inplace(x);
}
static_assert(test_shrink_to_fit_noop_inline());

// ----- representation_size / max_representation_size / representation_capacity / reserve_representation -----

consteval bool test_representation_size_zero() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    // A zero value occupies a single limb, matching representation().size().
    return x.representation_size() == 1U && x.representation_size() == x.representation().size();
}
static_assert(test_representation_size_zero());

consteval bool test_representation_size_small() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    return x.representation_size() == 1U && x.representation_size() == x.representation().size();
}
static_assert(test_representation_size_small());

consteval bool test_representation_size_negative() {
    // The magnitude, not the sign, determines representation_size().
    BEMAN_BIG_INT_NAMESPACE::big_int pos{42U};
    BEMAN_BIG_INT_NAMESPACE::big_int neg{-42};
    return neg.representation_size() == pos.representation_size();
}
static_assert(test_representation_size_negative());

consteval bool test_representation_size_matches_formula() {
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;
    BEMAN_BIG_INT_NAMESPACE::big_int x{18446744073709551616_n}; // 2^64, size() == 65
    constexpr std::size_t            digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    const std::size_t expected = (x.size() + digits - 1U) / digits; // ceil(size() / digits)
    return x.representation_size() == expected && x.representation_size() == x.representation().size() &&
           x.representation_size() >= 2U;
}
static_assert(test_representation_size_matches_formula());

consteval bool test_max_representation_size() {
    // Limb-count limit, bounded by the 31-bit control word that stores the limb count.
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    return x.max_representation_size() >= 1U && x.max_representation_size() <= ((std::size_t{1} << 31U) - 1U);
}
static_assert(test_max_representation_size());

consteval bool test_representation_capacity_inline() {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    // In place, representation_capacity() reports the in-place limb count (never 0).
    return x.representation_capacity() == BEMAN_BIG_INT_NAMESPACE::big_int::inplace_capacity;
}
static_assert(test_representation_capacity_inline());

consteval bool test_representation_capacity_heap() {
    constexpr std::size_t digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(8);
    // On the heap, capacity() (bits) equals representation_capacity() (limbs) times digits.
    return x.representation_capacity() >= 8U && x.capacity() == x.representation_capacity() * digits;
}
static_assert(test_representation_capacity_heap());

consteval bool test_reserve_representation_preserves_value() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    return x.representation()[0] == 42U && x.representation_capacity() >= 8U;
}
static_assert(test_reserve_representation_preserves_value());

// ----- allocator-extended copy and move construction -----

consteval bool test_allocator_extended_copy() {
    const BEMAN_BIG_INT_NAMESPACE::big_int::allocator_type alloc;
    const BEMAN_BIG_INT_NAMESPACE::big_int                 small{-42};
    BEMAN_BIG_INT_NAMESPACE::big_int                       big{-1};
    big <<= 4000;

    const BEMAN_BIG_INT_NAMESPACE::big_int small_copy{small, alloc};
    const BEMAN_BIG_INT_NAMESPACE::big_int big_copy{big, alloc};
    return small_copy == small && big_copy == big && big_copy.representation().data() != big.representation().data();
}
static_assert(test_allocator_extended_copy());

consteval bool test_allocator_extended_move() {
    // std::allocator is always equal, so the buffer is taken over rather than copied.
    const BEMAN_BIG_INT_NAMESPACE::big_int::allocator_type alloc;
    BEMAN_BIG_INT_NAMESPACE::big_int                       small{-42};
    BEMAN_BIG_INT_NAMESPACE::big_int                       big{-1};
    big <<= 4000;
    const BEMAN_BIG_INT_NAMESPACE::big_int expected = big;
    const auto* const                      data     = big.representation().data();

    const BEMAN_BIG_INT_NAMESPACE::big_int small_moved{std::move(small), alloc};
    const BEMAN_BIG_INT_NAMESPACE::big_int big_moved{std::move(big), alloc};
    return small_moved == -42 && big_moved == expected && big_moved.representation().data() == data;
}
static_assert(test_allocator_extended_move());

// ----- runtime tests -----

// A stateful allocator that propagates on copy and move assignment. Because it
// holds state it is not always-equal, so a growing assignment cannot reuse or
// steal the source's buffer and has to allocate. `id` lets the test observe
// which allocator ends up owning the destination, and `fail` arms a throw.
// Two words wide, so the owning basic_big_int has no trailing padding.
template <class T>
struct pocca_alloc {
    using value_type                             = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    std::size_t id   = 0;
    bool*       fail = nullptr;

    pocca_alloc() = default;
    explicit pocca_alloc(std::size_t allocator_id, bool* fail_flag = nullptr) noexcept
        : id{allocator_id}, fail{fail_flag} {}
    template <class U>
    pocca_alloc(const pocca_alloc<U>& other) noexcept : id{other.id}, fail{other.fail} {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (fail != nullptr && *fail) {
            throw std::bad_alloc{};
        }
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, std::size_t n) noexcept { std::allocator<T>{}.deallocate(p, n); }

    template <class U>
    bool operator==(const pocca_alloc<U>& other) const noexcept {
        return id == other.id;
    }
};

using pocca_big_int =
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<64,
                                           BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t,
                                           pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>>;

TEST(Allocation, PropagatingAssignmentAllocatesThroughSourceAllocator) {
    pocca_big_int dst{7, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{1U}};
    pocca_big_int src{1, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{2U}};
    src <<= 4000;

    dst = src;

    EXPECT_EQ(dst, src);
    EXPECT_EQ(dst.get_allocator().id, 2U);
}

TEST(Allocation, PropagatingMoveAssignmentStealsAndPublishesCount) {
    pocca_big_int dst{7, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{1U}};
    pocca_big_int src{1, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{2U}};
    src <<= 4000;
    const pocca_big_int expected = src;

    dst = std::move(src);

    EXPECT_EQ(dst, expected);
    EXPECT_EQ(dst.get_allocator().id, 2U);
}

TEST(Allocation, PropagatingAssignmentIsStrongWhenAllocationThrows) {
    bool          fail = false;
    pocca_big_int dst{7, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{1U}};
    pocca_big_int src{1, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{2U, &fail}};
    src <<= 4000;

    fail = true;
    EXPECT_THROW(dst = src, std::bad_alloc);

    // Strong: the value and the allocator both survive the failed assignment.
    EXPECT_EQ(dst, 7);
    EXPECT_EQ(dst.get_allocator().id, 1U);
    fail = false;
    dst  = src;
    EXPECT_EQ(dst, src);
}

// Compound multiplication, division and modulus keep the object's own allocator. These objects use a
// propagating-on-move-assignment allocator, so replacing `*this` with a default-constructed one would
// reset its id.
TEST(Allocation, CompoundMultiplyDivideModulusKeepPropagatingAllocator) {
    using alloc_t  = pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>;
    const auto big = [](std::size_t id, unsigned shift) {
        pocca_big_int x{1, alloc_t{id}};
        x <<= shift;
        x += 12345;
        return x;
    };
    // Small (inline), a few limbs (stack product), and large (fresh-buffer product) operands.
    for (const unsigned shift : {10U, 200U, 700U, 6000U}) {
        pocca_big_int       a  = big(7U, shift);
        pocca_big_int       b  = big(7U, 130U);
        pocca_big_int       c  = big(7U, 64U);
        const pocca_big_int a0 = a;

        a *= b;
        EXPECT_EQ(a.get_allocator().id, 7U) << "*= shift " << shift;
        EXPECT_EQ(a, a0 * b);

        a /= b;
        EXPECT_EQ(a.get_allocator().id, 7U) << "/= shift " << shift;
        EXPECT_EQ(a, a0);

        a *= a;
        EXPECT_EQ(a.get_allocator().id, 7U) << "x *= x shift " << shift;
        EXPECT_EQ(a, a0 * a0);

        a %= b;
        EXPECT_EQ(a.get_allocator().id, 7U) << "%= shift " << shift;
        EXPECT_EQ(a, (a0 * a0) % b);

        a = a0;
        a /= a;
        EXPECT_EQ(a.get_allocator().id, 7U) << "x /= x shift " << shift;
        EXPECT_EQ(a, 1);

        a = a0;
        a %= a;
        EXPECT_EQ(a.get_allocator().id, 7U) << "x %= x shift " << shift;
        EXPECT_EQ(a, 0);

        a = a0;
        a *= 3;
        a /= 3;
        a %= 1000003;
        EXPECT_EQ(a.get_allocator().id, 7U) << "integer rhs shift " << shift;

        // Divisors and multipliers that fit one limb take the in-place paths.
        a = a0;
        a *= c;
        a /= c;
        EXPECT_EQ(a.get_allocator().id, 7U);
        EXPECT_EQ(a, a0);
    }
}

TEST(Allocation, MultiplyAndDivideResultsTakeAllocatorFromOperand) {
    using alloc_t = pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>;
    pocca_big_int a{1, alloc_t{3U}};
    a <<= 500;
    pocca_big_int b{1, alloc_t{3U}};
    b <<= 190;
    EXPECT_EQ((a * b).get_allocator().id, 3U);
    EXPECT_EQ((a / b).get_allocator().id, 3U);
    EXPECT_EQ((a % b).get_allocator().id, 3U);
    const auto both = div_rem_to_zero(a + 1, b + 1);
    EXPECT_EQ(both.quotient.get_allocator().id, 3U);
    EXPECT_EQ(both.remainder.get_allocator().id, 3U);
}

// A stateful allocator whose `select_on_container_copy_construction` hands back a
// distinct allocator (`id + 1`) instead of a copy. A copy-constructed container
// must adopt that allocator, so `id` shows whether the constructor consulted the
// trait or just copied the source's allocator.
template <class T>
struct soccc_alloc {
    using value_type = T;

    std::size_t id = 0;

    soccc_alloc() = default;
    explicit soccc_alloc(std::size_t allocator_id) noexcept : id{allocator_id} {}
    template <class U>
    soccc_alloc(const soccc_alloc<U>& other) noexcept : id{other.id} {}

    [[nodiscard]] soccc_alloc select_on_container_copy_construction() const noexcept { return soccc_alloc{id + 1U}; }

    [[nodiscard]] T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void             deallocate(T* p, std::size_t n) noexcept { std::allocator<T>{}.deallocate(p, n); }

    template <class U>
    bool operator==(const soccc_alloc<U>& other) const noexcept {
        return id == other.id;
    }
};

using soccc_big_int =
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<64,
                                           BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t,
                                           soccc_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>>;

TEST(Allocation, CopyConstructionSelectsAllocator) {
    const soccc_big_int src{7, soccc_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{1U}};
    const soccc_big_int dst = src;

    EXPECT_EQ(dst, src);
    EXPECT_EQ(src.get_allocator().id, 1U);
    EXPECT_EQ(dst.get_allocator().id, 2U);
}

TEST(Allocation, CopyConstructionSelectsAllocatorForHeapValue) {
    soccc_big_int src{1, soccc_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{1U}};
    src <<= 4000;
    const soccc_big_int dst = src;

    EXPECT_EQ(dst, src);
    EXPECT_GT(dst.representation_size(), soccc_big_int::inplace_capacity);
    EXPECT_EQ(dst.get_allocator().id, 2U);
}

TEST(Allocation, CopyConstructionKeepsAllocatorWhenTraitCopies) {
    // `pocca_alloc` has no `select_on_container_copy_construction`, so the default
    // `allocator_traits` behavior copies the source allocator.
    pocca_big_int src{1, pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>{2U}};
    src <<= 4000;
    const pocca_big_int dst = src;

    EXPECT_EQ(dst, src);
    EXPECT_EQ(dst.get_allocator().id, 2U);
}

// `soccc_alloc` separates the three allocators a result could plausibly get --
// the operand's (id 1), the trait's choice (id 2) and a value-initialized one
// (id 0) -- which `std::pmr::polymorphic_allocator` cannot, because there the
// trait's choice and a value-initialized allocator are the same thing.
using soccc_alloc_type = soccc_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>;

TEST(Allocation, AbsSelectsAllocator) {
    soccc_big_int x{-1, soccc_alloc_type{1U}};
    x <<= 4000;

    const soccc_big_int from_lvalue = abs(x);
    EXPECT_EQ(from_lvalue, -x);
    EXPECT_EQ(from_lvalue.get_allocator().id, 2U);

    // Handed over rather than copied: the storage comes along, so the operand's
    // own allocator does too.
    const soccc_big_int from_rvalue = abs(std::move(x));
    EXPECT_EQ(from_rvalue, from_lvalue);
    EXPECT_EQ(from_rvalue.get_allocator().id, 1U);
}

TEST(Allocation, UnaryOperatorsSelectAllocator) {
    soccc_big_int x{1, soccc_alloc_type{1U}};
    x <<= 4000;

    EXPECT_EQ((+x).get_allocator().id, 2U);
    EXPECT_EQ((-x).get_allocator().id, 2U);
    EXPECT_EQ((~x).get_allocator().id, 2U);
    EXPECT_EQ((x++).get_allocator().id, 2U);
    EXPECT_EQ((x--).get_allocator().id, 2U);
    EXPECT_EQ(x.get_allocator().id, 1U); // the operand is untouched by the selection

    // The `&&` overloads take over the storage, so they keep the operand's own
    // allocator rather than selecting a new one.
    soccc_big_int y{1, soccc_alloc_type{1U}};
    y <<= 4000;
    EXPECT_EQ((-std::move(y)).get_allocator().id, 1U);
}

TEST(Allocation, GcdAndLcmSelectAllocator) {
    soccc_big_int a{12, soccc_alloc_type{1U}};
    soccc_big_int b{18, soccc_alloc_type{1U}};
    a <<= 4000;
    b <<= 4000;

    EXPECT_EQ(gcd(a, b).get_allocator().id, 2U);
    EXPECT_EQ(lcm(a, b).get_allocator().id, 2U);
    EXPECT_EQ(gcd(a, 42).get_allocator().id, 2U);
    EXPECT_EQ(midpoint(a, b).get_allocator().id, 2U);
}

TEST(Allocation, BinaryOperatorsSelectAllocator) {
    // Every binary operator builds a fresh result, so it selects the allocator
    // through the trait rather than taking the operand's (id 1) or leaving a
    // value-initialized one (id 0).
    soccc_big_int a{12, soccc_alloc_type{1U}};
    soccc_big_int b{18, soccc_alloc_type{1U}};
    a <<= 4000;
    b <<= 4000;

    EXPECT_EQ((a + b).get_allocator().id, 2U);
    EXPECT_EQ((a - b).get_allocator().id, 2U);
    EXPECT_EQ((a * b).get_allocator().id, 2U);
    EXPECT_EQ((a / b).get_allocator().id, 2U);
    EXPECT_EQ((a % b).get_allocator().id, 2U);
    EXPECT_EQ((a & b).get_allocator().id, 2U);
    EXPECT_EQ((a | b).get_allocator().id, 2U);
    EXPECT_EQ((a ^ b).get_allocator().id, 2U);
    EXPECT_EQ((a << 100).get_allocator().id, 2U);
    EXPECT_EQ((a >> 100).get_allocator().id, 2U);
    EXPECT_EQ((a >> 100000).get_allocator().id, 2U); // the whole value is discarded
    EXPECT_EQ((-a >> 100000).get_allocator().id, 2U);

    const auto [quo, rem] = div_rem_to_zero(a, b);
    EXPECT_EQ(quo.get_allocator().id, 2U);
    EXPECT_EQ(rem.get_allocator().id, 2U);

    EXPECT_EQ(a.get_allocator().id, 1U); // the operands are untouched
    EXPECT_EQ(b.get_allocator().id, 1U);
}

TEST(Allocation, BinaryOperatorsWithIntegerSelectAllocator) {
    // The same holds when only one side is a basic_big_int, whichever side it is.
    soccc_big_int a{12, soccc_alloc_type{1U}};
    a <<= 4000;

    EXPECT_EQ((a + 7).get_allocator().id, 2U);
    EXPECT_EQ((7 + a).get_allocator().id, 2U);
    EXPECT_EQ((a - 7).get_allocator().id, 2U);
    EXPECT_EQ((7 - a).get_allocator().id, 2U);
    EXPECT_EQ((a * 7).get_allocator().id, 2U);
    EXPECT_EQ((7 * a).get_allocator().id, 2U);
    EXPECT_EQ((a / 7).get_allocator().id, 2U);
    EXPECT_EQ((7 % a).get_allocator().id, 2U);
    EXPECT_EQ((a & 7).get_allocator().id, 2U);
    EXPECT_EQ((7 | a).get_allocator().id, 2U);
    EXPECT_EQ((a ^ 7).get_allocator().id, 2U);
}

TEST(Allocation, BinaryOperatorsOnRvalueKeepAllocatorWhenStorageIsReused) {
    // `+` and `-` fold into the operand's own buffer, so a handed-over operand
    // takes its allocator along -- the same rule the move constructor follows.
    // Multiplication and the bitwise operators always need a fresh buffer, so
    // they select even for an rvalue.
    const auto reused = [] {
        soccc_big_int x{12, soccc_alloc_type{1U}};
        x <<= 4000;
        return x;
    };

    EXPECT_EQ((reused() + 7).get_allocator().id, 1U);
    EXPECT_EQ((reused() - 7).get_allocator().id, 1U);
    EXPECT_EQ((reused() << 100).get_allocator().id, 1U);
    EXPECT_EQ((reused() >> 100).get_allocator().id, 1U);
    EXPECT_EQ((reused() * 7).get_allocator().id, 2U);
    EXPECT_EQ((reused() & 7).get_allocator().id, 2U);
}

// ----- allocator-extended copy and move construction -----
// `pocca_alloc` compares by `id` and is not always equal, so these tests can
// hand the constructors an allocator that does or does not match the source's,
// and arm `fail` on it to show whether the construction allocated at all.

using pocca_alloc_type = pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>;

TEST(Allocation, AllocatorExtendedCopyUsesNamedAllocator) {
    pocca_big_int src{1, pocca_alloc_type{1U}};
    src <<= 4000;
    const pocca_big_int small{-7, pocca_alloc_type{1U}};

    const pocca_big_int dst{src, pocca_alloc_type{2U}};
    const pocca_big_int small_dst{small, pocca_alloc_type{2U}};

    EXPECT_EQ(dst, src);
    EXPECT_EQ(dst.get_allocator().id, 2U);
    EXPECT_NE(dst.representation().data(), src.representation().data());
    EXPECT_EQ(small_dst, small);
    EXPECT_EQ(small_dst.get_allocator().id, 2U);
    EXPECT_EQ(src.get_allocator().id, 1U);
}

TEST(Allocation, AllocatorExtendedCopyDoesNotSelectAllocator) {
    // The named allocator is used as is: `select_on_container_copy_construction`
    // would have given id 2 (or 6 had it been applied to the named one).
    soccc_big_int src{1, soccc_alloc_type{1U}};
    src <<= 4000;

    const soccc_big_int dst{src, soccc_alloc_type{5U}};

    EXPECT_EQ(dst, src);
    EXPECT_EQ(dst.get_allocator().id, 5U);
}

TEST(Allocation, AllocatorExtendedCopyAllocatesThroughNamedAllocator) {
    // Storage for the copy comes from the named allocator, not the source's.
    bool          fail = true;
    pocca_big_int src{1, pocca_alloc_type{1U}};
    src <<= 4000;

    EXPECT_THROW(const pocca_big_int dst(src, pocca_alloc_type{2U, &fail}), std::bad_alloc);
}

TEST(Allocation, AllocatorExtendedCopyOfHeapHeldSmallValueStaysInPlace) {
    // A value that fits in place is copied into place even when the source holds
    // it on the heap, so the armed allocator is never asked for storage.
    bool          fail = true;
    pocca_big_int src{42, pocca_alloc_type{1U}};
    src.reserve_representation(8);

    const pocca_big_int dst{src, pocca_alloc_type{2U, &fail}};

    EXPECT_EQ(dst, 42);
    EXPECT_TRUE(is_inplace(dst));
}

TEST(Allocation, AllocatorExtendedMoveWithEqualAllocatorTakesBuffer) {
    bool          fail = true; // equal allocators hand the buffer over, so nothing is allocated
    pocca_big_int src{1, pocca_alloc_type{1U}};
    src <<= 4000;
    const pocca_big_int expected = src;
    const auto* const   data     = src.representation().data();

    const pocca_big_int dst{std::move(src), pocca_alloc_type{1U, &fail}};

    EXPECT_EQ(dst, expected);
    EXPECT_EQ(dst.representation().data(), data);
    EXPECT_EQ(dst.get_allocator().id, 1U);

    // The moved-from source no longer owns the buffer and stays usable.
    src = 5;
    EXPECT_EQ(src, 5);
}

TEST(Allocation, AllocatorExtendedMoveWithUnequalAllocatorCopies) {
    // Allocator 2 cannot free a buffer from allocator 1, so the value is copied
    // into storage of its own rather than taken over.
    pocca_big_int src{1, pocca_alloc_type{1U}};
    src <<= 4000;
    const pocca_big_int expected = src;
    const auto* const   data     = src.representation().data();

    const pocca_big_int dst{std::move(src), pocca_alloc_type{2U}};

    EXPECT_EQ(dst, expected);
    EXPECT_EQ(dst.get_allocator().id, 2U);
    EXPECT_NE(dst.representation().data(), data);
    EXPECT_EQ(src.get_allocator().id, 1U);
}

TEST(Allocation, AllocatorExtendedMoveWithUnequalAllocatorIsStrongWhenAllocationThrows) {
    bool          fail = true;
    pocca_big_int src{1, pocca_alloc_type{1U}};
    src <<= 4000;
    const pocca_big_int expected = src;

    EXPECT_THROW(const pocca_big_int dst(std::move(src), pocca_alloc_type{2U, &fail}), std::bad_alloc);

    // Strong: the failed construction leaves the source untouched.
    EXPECT_EQ(src, expected);
    EXPECT_EQ(src.get_allocator().id, 1U);
}

TEST(Allocation, AllocatorExtendedMoveOfSmallValueNeverAllocates) {
    // An in-place value, and one that fits in place although it is held on the
    // heap, land in place even when the allocators differ.
    bool          fail = true;
    pocca_big_int inplace{-7, pocca_alloc_type{1U}};
    pocca_big_int reserved{42, pocca_alloc_type{1U}};
    reserved.reserve_representation(8);

    const pocca_big_int from_inplace{std::move(inplace), pocca_alloc_type{2U, &fail}};
    const pocca_big_int from_reserved{std::move(reserved), pocca_alloc_type{2U, &fail}};

    EXPECT_EQ(from_inplace, -7);
    EXPECT_TRUE(is_inplace(from_inplace));
    EXPECT_EQ(from_inplace.get_allocator().id, 2U);
    EXPECT_EQ(from_reserved, 42);
    EXPECT_TRUE(is_inplace(from_reserved));
    EXPECT_EQ(from_reserved.get_allocator().id, 2U);
}

TEST(Allocation, SizeDefault) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    EXPECT_EQ(x.size(), 0);
}

TEST(Allocation, SizeFromValue) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    EXPECT_EQ(x.size(), 6);
}

TEST(Allocation, SizeFromValueNeg) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{-42};
    EXPECT_EQ(x.size(), 6);
}

TEST(Allocation, SizeFromValueBig) {
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;
    BEMAN_BIG_INT_NAMESPACE::big_int x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    EXPECT_EQ(x.size(), 3324);
}

TEST(Allocation, SizeFromValueBigNeg) {
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;
    BEMAN_BIG_INT_NAMESPACE::big_int x{
        -31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    EXPECT_EQ(x.size(), 3324);
}

TEST(Allocation, MaxSize) {
    const BEMAN_BIG_INT_NAMESPACE::big_int x;
    constexpr std::size_t                  digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    EXPECT_EQ(x.max_size(), x.max_representation_size() * digits);
}

TEST(Allocation, CapacityDefault) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x.capacity(), BEMAN_BIG_INT_NAMESPACE::big_int::inplace_bits);
}

TEST(Allocation, ReserveWithinInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(1);
    EXPECT_TRUE(is_inplace(x));
}

TEST(Allocation, ReserveBeyondInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(4);
    EXPECT_GE(x.representation_capacity(), 4U);
}

TEST(Allocation, ReservePreservesValue) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    EXPECT_EQ(x.representation()[0], 42U);
    EXPECT_GE(x.representation_capacity(), 8U);
}

TEST(Allocation, ReserveDoubling) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(3); // max(3, 2*2) = 4
    EXPECT_GE(x.representation_capacity(), 3);
}

TEST(Allocation, ReserveGrowsGeometrically) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(4); // cap = 4
    EXPECT_GE(x.representation_capacity(), 4u);
    x.reserve_representation(5); // cap = max(5, 2*4) = 8
    EXPECT_GE(x.representation_capacity(), 8u);
}

TEST(Allocation, ReserveNoShrink) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(10);
    auto cap = x.representation_capacity();
    x.reserve_representation(2);
    EXPECT_EQ(x.representation_capacity(), cap);
}

TEST(Allocation, ShrinkToFitNoopInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.shrink_to_fit();
    EXPECT_TRUE(is_inplace(x));
}

TEST(Allocation, ShrinkToFitAfterReserve) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(16);
    EXPECT_GE(x.representation_capacity(), 16U);
    x.shrink_to_fit();
    // After shrink, capacity should be reduced
    EXPECT_LT(x.representation_capacity(), 16U);
    // Value should be preserved
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(Allocation, ReserveLargeValue) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(1024);
    EXPECT_GE(x.representation_capacity(), 1024U);
}

TEST(Allocation, RepresentationSizeZero) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    EXPECT_EQ(x.representation_size(), 1U);
    EXPECT_EQ(x.representation_size(), x.representation().size());
}

TEST(Allocation, RepresentationSizeSmall) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    EXPECT_EQ(x.representation_size(), 1U);
    EXPECT_EQ(x.representation_size(), x.representation().size());
}

TEST(Allocation, RepresentationSizeNegativeMatchesMagnitude) {
    BEMAN_BIG_INT_NAMESPACE::big_int pos{42};
    BEMAN_BIG_INT_NAMESPACE::big_int neg{-42};
    EXPECT_EQ(neg.representation_size(), pos.representation_size());
}

TEST(Allocation, RepresentationSizeBig) {
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;
    BEMAN_BIG_INT_NAMESPACE::big_int x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    EXPECT_EQ(x.representation_size(), x.representation().size());
    EXPECT_GT(x.representation_size(), 1U);
}

TEST(Allocation, MaxRepresentationSize) {
    const BEMAN_BIG_INT_NAMESPACE::big_int x;
    constexpr std::size_t                  digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    EXPECT_EQ(x.max_size(), x.max_representation_size() * digits);
    EXPECT_GE(x.max_representation_size(), 1U);
}

TEST(Allocation, RepresentationCapacityInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    constexpr std::size_t            inplace_cap = BEMAN_BIG_INT_NAMESPACE::big_int::inplace_capacity;
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x.representation_capacity(), inplace_cap);
}

TEST(Allocation, ReserveRepresentationBeyondInline) {
    constexpr std::size_t digits =
        static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x.reserve_representation(4);
    EXPECT_FALSE(is_inplace(x));
    EXPECT_GE(x.representation_capacity(), 4U);
    EXPECT_EQ(x.capacity(), x.representation_capacity() * digits);
}

TEST(Allocation, ReserveRepresentationPreservesValue) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    EXPECT_EQ(x.representation()[0], 42U);
    EXPECT_GE(x.representation_capacity(), 8U);
}

// ----- copy/move with heap storage -----

TEST(Allocation, CopyConstructHeapAllocated) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    EXPECT_EQ(x.representation().size(), 1);
    x.reserve_representation(8); // force heap
    // GE instead of EQ because allocate_at_least may be used.
    EXPECT_GE(x.representation_capacity(), 8);
    EXPECT_EQ(x.representation().size(), 1);

    BEMAN_BIG_INT_NAMESPACE::big_int y(x);
    // y should have no heap allocation
    // because the integer value can be represented using a single limb,
    // irrespective of what the capacity of x is.
    EXPECT_TRUE(is_inplace(y));
    EXPECT_EQ(y.representation().size(), 1);
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, MoveConstructHeapAllocated) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    auto                             cap = x.representation_capacity();
    BEMAN_BIG_INT_NAMESPACE::big_int y(std::move(x));
    EXPECT_EQ(y.representation()[0], 42U);
    EXPECT_EQ(y.representation_capacity(), cap);
}

TEST(Allocation, CopyAssignHeapToInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    BEMAN_BIG_INT_NAMESPACE::big_int y;
    y = x;
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, CopyAssignHeapToHeap) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    BEMAN_BIG_INT_NAMESPACE::big_int y{99U};
    y.reserve_representation(4);
    y = x;
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, MoveAssignHeapToInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    BEMAN_BIG_INT_NAMESPACE::big_int y;
    y = std::move(x);
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, MoveAssignHeapToHeap) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    BEMAN_BIG_INT_NAMESPACE::big_int y{99U};
    y.reserve_representation(4);
    y = std::move(x);
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, SelfAssignment) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    auto& ref = x;
    x         = ref;
    EXPECT_EQ(x.representation()[0], 42U);
}

// ----- operator= storage reuse -----
// Limbs in 2^64, whatever the limb width.
constexpr std::size_t two_pow_64_limbs =
    64U / static_cast<std::size_t>(std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits) + 1U;

// The shared `assign_value` helper keeps the destination's allocation if its
// effective capacity already fits the source. These tests verify the fast path
// by checking that the destination's data pointer does not change.

TEST(Allocation, CopyAssignReusesDstStorage) {
    BEMAN_BIG_INT_NAMESPACE::big_int dst{1U};
    dst.reserve_representation(8); // dst now on the heap with capacity >= 8
    const auto* const dst_data = dst.representation().data();
    const auto        dst_cap  = dst.representation_capacity();

    const BEMAN_BIG_INT_NAMESPACE::big_int src =
        BEMAN_BIG_INT_NAMESPACE::big_int{0xFFFFFFFFFFFFFFFFU} + BEMAN_BIG_INT_NAMESPACE::big_int{1};
    ASSERT_EQ(src.representation().size(), two_pow_64_limbs); // heap -- fits in dst's capacity

    dst = src;
    EXPECT_EQ(dst.representation().data(), dst_data); // no reallocation
    EXPECT_EQ(dst.representation_capacity(), dst_cap);
    ASSERT_EQ(dst.representation().size(), two_pow_64_limbs);
    EXPECT_EQ(dst, src);
}

TEST(Allocation, MoveAssignStealsHeapSrcEvenWhenDstLarger) {
    // A heap source is stolen whatever capacity dst already holds: the source buffer is
    // adopted, dst's own buffer is released, and the source is left as inline zero.
    BEMAN_BIG_INT_NAMESPACE::big_int dst{1U};
    dst.reserve_representation(16); // big dst buffer
    const auto dst_cap = dst.representation_capacity();

    BEMAN_BIG_INT_NAMESPACE::big_int src =
        BEMAN_BIG_INT_NAMESPACE::big_int{0xFFFFFFFFFFFFFFFFU} + BEMAN_BIG_INT_NAMESPACE::big_int{1};
    ASSERT_EQ(src.representation().size(), two_pow_64_limbs);
    const auto* const src_data = src.representation().data();
    const auto        src_cap  = src.representation_capacity();
    ASSERT_LT(src_cap, dst_cap); // dst has more capacity than src

    dst = std::move(src);
    EXPECT_EQ(dst.representation().data(), src_data);
    EXPECT_EQ(dst.representation_capacity(), src_cap);
    ASSERT_EQ(dst.representation().size(), two_pow_64_limbs);
    EXPECT_TRUE(is_inplace(src));
    EXPECT_EQ(src, 0U);
}

TEST(Allocation, MoveAssignInlineSrcReusesDstStorage) {
    // An inline source has no buffer to steal, so it is copied into dst's existing storage.
    BEMAN_BIG_INT_NAMESPACE::big_int dst{1U};
    dst.reserve_representation(16);
    const auto* const dst_data = dst.representation().data();
    const auto        dst_cap  = dst.representation_capacity();

    BEMAN_BIG_INT_NAMESPACE::big_int src{5U};
    ASSERT_TRUE(is_inplace(src));

    dst = std::move(src);
    EXPECT_EQ(dst.representation().data(), dst_data);
    EXPECT_EQ(dst.representation_capacity(), dst_cap);
    EXPECT_EQ(dst, 5U);
}

TEST(Allocation, MoveAssignStealsSrcWhenDstTooSmall) {
    // When dst's capacity is insufficient, move-assign must steal src's buffer
    // (noexcept contract -- no allocation allowed).
    BEMAN_BIG_INT_NAMESPACE::big_int dst; // inline, no allocation
    EXPECT_TRUE(is_inplace(dst));

    BEMAN_BIG_INT_NAMESPACE::big_int src =
        BEMAN_BIG_INT_NAMESPACE::big_int{0xFFFFFFFFFFFFFFFFU} + BEMAN_BIG_INT_NAMESPACE::big_int{1};
    ASSERT_FALSE(is_inplace(src));
    const auto* const src_data = src.representation().data();
    const auto        src_cap  = src.representation_capacity();

    dst = std::move(src);
    // dst adopted src's buffer wholesale.
    EXPECT_EQ(dst.representation().data(), src_data);
    EXPECT_EQ(dst.representation_capacity(), src_cap);
    // src released heap ownership (moved-from state; value is unspecified,
    // matching the existing move-assign contract).
    EXPECT_TRUE(is_inplace(src));
}

TEST(Allocation, CopyAssignAllocatesWhenDstTooSmall) {
    // When dst has no (heap) capacity and src is bigger than inline, copy-assign
    // must allocate a fresh buffer.
    BEMAN_BIG_INT_NAMESPACE::big_int       dst; // inline, capacity 0
    const BEMAN_BIG_INT_NAMESPACE::big_int src =
        BEMAN_BIG_INT_NAMESPACE::big_int{0xFFFFFFFFFFFFFFFFU} + BEMAN_BIG_INT_NAMESPACE::big_int{1};
    ASSERT_FALSE(is_inplace(src));

    dst = src;
    ASSERT_EQ(dst.representation().size(), two_pow_64_limbs);
    EXPECT_FALSE(is_inplace(dst));
    EXPECT_NE(dst.representation().data(), src.representation().data());
    EXPECT_EQ(dst, src);
}

TEST(Allocation, AssignPreservesInlineBitCastInvariant) {
    // After assigning a shorter value into a destination that previously held
    // a longer value in inline storage, the unused tail limbs must be zero so
    // that `inplace_to_bit_uint` would still produce the correct bit pattern.
    // We verify indirectly by checking that equality comparisons match a freshly
    // constructed big_int.
    using big_int_256 = BEMAN_BIG_INT_NAMESPACE::basic_big_int<256>;
    big_int_256 dst{0xFFFFFFFFFFFFFFFFU};
    dst = dst + big_int_256{1}; // promote to 2 limbs inline
    dst = big_int_256{7};       // shrink back to 1 limb inline -- tail must be zeroed
    EXPECT_EQ(dst, 7);
    EXPECT_EQ(dst, big_int_256{7});
    EXPECT_EQ(dst.representation().size(), 1U);
}

// ----- shrink_to_fit edge cases -----

TEST(Allocation, ShrinkToFitBackToInline) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(16);
    EXPECT_GE(x.representation_capacity(), 16U);
    x.shrink_to_fit();
    // limb_count is 1, which fits in the in-place buffer, so storage returns to inline
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(Allocation, ShrinkToFitWhenCapacityEqualsCount) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    x.shrink_to_fit(); // goes back to inline
    x.shrink_to_fit(); // should be a no-op now
    EXPECT_EQ(x.representation()[0], 42U);
}

// ----- from_range with heap allocation -----

TEST(Allocation, FromRangeLargeAllocatesThenDestroys) {
#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_CONTAINERS_RANGES
    std::array<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t, 8> limbs{1, 2, 3, 4, 5, 6, 7, 8};
    BEMAN_BIG_INT_NAMESPACE::big_int                              x(std::from_range, limbs);
    EXPECT_EQ(x.representation().size(), 8U);
    EXPECT_EQ(x.representation()[0], 1U);
    EXPECT_EQ(x.representation()[7], 8U);
#endif
}

// ----- unary ops with heap storage -----

TEST(Allocation, NegateHeapAllocated) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    auto y = -x;
    EXPECT_EQ(y.representation()[0], 42U);
}

// ----- multiple grow/shrink cycles -----

TEST(Allocation, GrowShrinkGrowCycle) {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    EXPECT_GE(x.representation_capacity(), 8U);
    x.shrink_to_fit();
    x.reserve_representation(16);
    EXPECT_GE(x.representation_capacity(), 16U);
    x.shrink_to_fit();
    EXPECT_EQ(x.representation()[0], 42U);
}

// ----- compile-time copy/move with heap -----

consteval bool test_copy_heap() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    BEMAN_BIG_INT_NAMESPACE::big_int y(x);
    return y.representation()[0] == 42U;
}
static_assert(test_copy_heap());

consteval bool test_move_heap() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    BEMAN_BIG_INT_NAMESPACE::big_int y(std::move(x));
    return y.representation()[0] == 42U;
}
static_assert(test_move_heap());

consteval bool test_shrink_to_fit_back_to_inline() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(16);
    x.shrink_to_fit();
    return is_inplace(x) && x.representation()[0] == 42U;
}
static_assert(test_shrink_to_fit_back_to_inline());

consteval bool test_grow_shrink_grow() {
    BEMAN_BIG_INT_NAMESPACE::big_int x{42U};
    x.reserve_representation(8);
    x.shrink_to_fit();
    x.reserve_representation(16);
    x.shrink_to_fit();
    return x.representation()[0] == 42U;
}
static_assert(test_grow_shrink_grow());

// ----- Allocator ownership across assignment -----

// Records which allocator id handed out each block, and counts blocks released through an allocator other than
// the one that allocated them.
struct ownership_tracker {
    std::map<const void*, std::size_t> owner;
    std::size_t                        mismatched_frees = 0;
    std::size_t                        live             = 0;
};

ownership_tracker& tracker() {
    static ownership_tracker t;
    return t;
}

template <class T>
struct tracking_pocca_alloc {
    using value_type                             = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    std::size_t id = 0;

    tracking_pocca_alloc() = default;
    explicit tracking_pocca_alloc(std::size_t allocator_id) noexcept : id{allocator_id} {}
    template <class U>
    tracking_pocca_alloc(const tracking_pocca_alloc<U>& other) noexcept : id{other.id} {}

    [[nodiscard]] T* allocate(std::size_t n) {
        T* const p         = std::allocator<T>{}.allocate(n);
        tracker().owner[p] = id;
        ++tracker().live;
        return p;
    }
    void deallocate(T* p, std::size_t n) noexcept {
        if (tracker().owner[p] != id) {
            ++tracker().mismatched_frees;
        }
        tracker().owner.erase(p);
        --tracker().live;
        std::allocator<T>{}.deallocate(p, n);
    }

    template <class U>
    bool operator==(const tracking_pocca_alloc<U>& other) const noexcept {
        return id == other.id;
    }
};

using tracking_big_int =
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<64,
                                           BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t,
                                           tracking_pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>>;

namespace {
using tracking_id = tracking_pocca_alloc<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>;

tracking_big_int make_heap_value(const tracking_id alloc, const std::size_t bits) {
    tracking_big_int x{1, alloc};
    x <<= bits;
    return x;
}
} // namespace

TEST(Allocation, PropagatingCopyAssignFreesLargerDstWithItsOwnAllocator) {
    tracker() = {};
    {
        tracking_big_int dst = make_heap_value(tracking_id{1U}, 4000);
        dst.reserve_representation(200); // larger than the source, so the old fast path kept it
        const tracking_big_int src = make_heap_value(tracking_id{2U}, 100);
        ASSERT_FALSE(is_inplace(src));

        dst = src;

        EXPECT_EQ(dst, src);
        EXPECT_EQ(dst.get_allocator().id, 2U);
    }
    EXPECT_EQ(tracker().mismatched_frees, 0U);
    EXPECT_EQ(tracker().live, 0U);
}

TEST(Allocation, PropagatingCopyAssignOfInlineSrcFreesDstWithItsOwnAllocator) {
    tracker() = {};
    {
        tracking_big_int       dst = make_heap_value(tracking_id{1U}, 4000);
        const tracking_big_int src{7, tracking_id{2U}};

        dst = src;

        EXPECT_EQ(dst, 7);
        EXPECT_EQ(dst.get_allocator().id, 2U);
    }
    EXPECT_EQ(tracker().mismatched_frees, 0U);
    EXPECT_EQ(tracker().live, 0U);
}

TEST(Allocation, PropagatingMoveAssignFreesDstWithItsOwnAllocator) {
    tracker() = {};
    {
        tracking_big_int  dst      = make_heap_value(tracking_id{1U}, 4000);
        tracking_big_int  heap_src = make_heap_value(tracking_id{2U}, 100);
        const auto* const src_data = heap_src.representation().data();

        dst = std::move(heap_src);
        EXPECT_EQ(dst.representation().data(), src_data);
        EXPECT_EQ(dst.get_allocator().id, 2U);

        tracking_big_int inline_src{9, tracking_id{3U}};
        dst = std::move(inline_src);
        EXPECT_EQ(dst, 9);
        EXPECT_EQ(dst.get_allocator().id, 3U);
    }
    EXPECT_EQ(tracker().mismatched_frees, 0U);
    EXPECT_EQ(tracker().live, 0U);
}

TEST(Allocation, PmrMoveAssignStealsFromTheSameResource) {
    std::pmr::monotonic_buffer_resource   resource;
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int dst{1, &resource};
    dst.reserve_representation(16);
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int src{1, &resource};
    src <<= 200;
    const auto* const src_data = src.representation().data();
    const auto        expected = src;

    dst = std::move(src);

    EXPECT_EQ(dst.representation().data(), src_data);
    EXPECT_EQ(dst, expected);
    EXPECT_EQ(dst.get_allocator().resource(), &resource);
    EXPECT_EQ(src, 0U);
}

TEST(Allocation, PmrMoveAssignCopiesFromAnotherResourceAndKeepsItsOwn) {
    std::pmr::monotonic_buffer_resource   dst_resource;
    std::pmr::monotonic_buffer_resource   src_resource;
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int dst{1, &dst_resource};
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int src{1, &src_resource};
    src <<= 200;
    const auto* const src_data = src.representation().data();
    const auto        expected = src;

    dst = std::move(src);

    EXPECT_NE(dst.representation().data(), src_data);
    EXPECT_EQ(dst, expected);
    EXPECT_EQ(dst.get_allocator().resource(), &dst_resource);
    EXPECT_EQ(src.get_allocator().resource(), &src_resource);
    EXPECT_EQ(src, expected); // not stolen, so the source keeps its value
}

TEST(Allocation, PmrMoveAssignFromAnotherResourceReusesLargerDstStorage) {
    std::pmr::monotonic_buffer_resource   dst_resource;
    std::pmr::monotonic_buffer_resource   src_resource;
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int dst{1, &dst_resource};
    dst.reserve_representation(16);
    const auto* const                     dst_data = dst.representation().data();
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int src{1, &src_resource};
    src <<= 200;
    const auto expected = src;

    dst = std::move(src);

    EXPECT_EQ(dst.representation().data(), dst_data);
    EXPECT_EQ(dst, expected);
    EXPECT_EQ(dst.get_allocator().resource(), &dst_resource);
}
