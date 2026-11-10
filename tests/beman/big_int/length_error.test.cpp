// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// The representation limit. Every growth request funnels through one check that
// rejects a limb count exceeding `max_representation_size()` -- equivalently, a
// bit count exceeding `max_size()` -- by throwing `std::length_error` before the
// allocator is consulted. During constant evaluation the same condition is a
// compile-time error instead, since a thrown exception is not a constant
// expression, so these tests are all runtime tests.

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using uint_t = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

constexpr std::size_t max_limbs = big_int{}.max_representation_size();
constexpr std::size_t max_bits  = big_int{}.max_size();

// An allocator that never hands out storage. A request that reaches it fails
// with `bad_alloc`, so a `length_error` proves the limit was checked first and a
// `bad_alloc` proves the request was considered representable and passed on.
// Stateless, so it is always-equal and adds no padding to the owning big_int.
template <class T>
struct never_alloc {
    using value_type = T;

    [[nodiscard]] T* allocate(std::size_t) { throw std::bad_alloc{}; }
    void             deallocate(T*, std::size_t) noexcept {}

    template <class U>
    bool operator==(const never_alloc<U>&) const noexcept {
        return true;
    }
};

using starved_big_int = BEMAN_BIG_INT_NAMESPACE::basic_big_int<64, uint_t, never_alloc<uint_t>>;

// Left-shifting one by `s` through `<<=` and the lvalue and rvalue operators throws and leaves the
// operand unchanged. A heap operand also keeps its storage.
template <class S>
void expect_left_shift_of_one_throws(const S s) {
    big_int x{1U};
    big_int result;

    EXPECT_THROW(x <<= s, std::length_error);
    EXPECT_EQ(x, 1U);
    EXPECT_THROW(result = x << s, std::length_error);
    EXPECT_EQ(x, 1U);
    EXPECT_EQ(result, 0U);
    EXPECT_THROW(result = big_int{1U} << s, std::length_error);
    EXPECT_EQ(result, 0U);

    big_int       heap   = -(big_int{1U} << 1000);
    const big_int before = heap;
    const auto    cap    = heap.representation_capacity();
    EXPECT_THROW(heap <<= s, std::length_error);
    EXPECT_EQ(heap, before);
    EXPECT_EQ(heap.representation_capacity(), cap);
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_normalized(heap));
    EXPECT_THROW(result = heap << s, std::length_error);
    EXPECT_EQ(heap, before);
    EXPECT_THROW(result = big_int{heap} << s, std::length_error);
    EXPECT_EQ(result, 0U);
}

} // namespace

TEST(LengthError, ReserveRepresentationBeyondMaxThrows) {
    big_int x{42U};
    EXPECT_THROW(x.reserve_representation(max_limbs + 1), std::length_error);
    EXPECT_THROW(x.reserve_representation(std::numeric_limits<std::size_t>::max()), std::length_error);
}

TEST(LengthError, ReserveBeyondMaxSizeThrows) {
    big_int x{42U};
    EXPECT_THROW(x.reserve(max_bits + 1), std::length_error);
    EXPECT_THROW(x.reserve(std::numeric_limits<std::size_t>::max()), std::length_error);
}

TEST(LengthError, ReserveIsStrongWhenLengthIsRejected) {
    big_int x{42U};
    x.reserve_representation(32);
    const auto cap = x.representation_capacity();
    ASSERT_GE(cap, 32U);

    EXPECT_THROW(x.reserve_representation(max_limbs + 1), std::length_error);

    // The value, its capacity, and the class invariants all survive the failure.
    EXPECT_EQ(x, 42U);
    EXPECT_EQ(x.representation_capacity(), cap);
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_normalized(x));
    x *= 2U;
    EXPECT_EQ(x, 84U);
}

TEST(LengthError, InPlaceValueIsUntouchedWhenLengthIsRejected) {
    big_int x{42U};
    EXPECT_THROW(x.reserve_representation(max_limbs + 1), std::length_error);
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_inplace(x));
    EXPECT_EQ(x, 42U);
}

// The limit is exact: `max_representation_size()` limbs (`max_size()` bits) is a
// representable request that reaches the allocator, one more is not.
TEST(LengthError, MaxRepresentationSizeIsNotRejectedAsTooLong) {
    starved_big_int x;
    EXPECT_THROW(x.reserve_representation(max_limbs), std::bad_alloc);
    EXPECT_THROW(x.reserve_representation(max_limbs + 1), std::length_error);
}

TEST(LengthError, MaxSizeIsNotRejectedAsTooLong) {
    starved_big_int x;
    EXPECT_THROW(x.reserve(max_bits), std::bad_alloc);
    EXPECT_THROW(x.reserve(max_bits + 1), std::length_error);
}

TEST(LengthError, ThrownExceptionIsALogicError) {
    big_int x;
    EXPECT_THROW(x.reserve_representation(max_limbs + 1), std::logic_error);
}

TEST(LengthError, ShiftLeftBeyondMaxThrows) {
    // Shifting a one-limb value by the whole bit budget needs one limb more
    // than the representation can hold.
    big_int x{1U};
    big_int result;

    // The compound operator shifts `x` itself.
    EXPECT_THROW(x <<= max_bits, std::length_error);
    EXPECT_EQ(x, 1U);

    // The lvalue operator allocates the result up front rather than growing it.
    EXPECT_THROW(result = x << max_bits, std::length_error);
    EXPECT_EQ(x, 1U);
    EXPECT_EQ(result, 0U);

    // The rvalue operator shifts the temporary in place.
    EXPECT_THROW(result = big_int{1U} << max_bits, std::length_error);
    EXPECT_EQ(result, 0U);
}

TEST(LengthError, ShiftLeftWithinMaxIsNotRejected) {
    // One limb less than the budget: representable, so it is the allocator
    // that fails rather than the length check.
    starved_big_int       x{1U};
    constexpr std::size_t digits = static_cast<std::size_t>(std::numeric_limits<uint_t>::digits);
    EXPECT_THROW(x <<= max_bits - digits, std::bad_alloc);
}

// A count past `max_size()` is still a valid shift count, so it throws instead of
// failing an assertion, and a count wider than `size_t` saturates instead of
// wrapping to a small shift.
TEST(LengthError, ShiftLeftByCountPastMaxSizeThrows) {
    expect_left_shift_of_one_throws(max_bits + 1);
    expect_left_shift_of_one_throws(std::numeric_limits<std::size_t>::max());
    expect_left_shift_of_one_throws(1ULL << 40);
    expect_left_shift_of_one_throws(std::numeric_limits<unsigned long long>::max());
    expect_left_shift_of_one_throws(std::numeric_limits<long long>::max());
#ifdef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
    using u128 = BEMAN_BIG_INT_NAMESPACE::detail::uint128_t;
    using i128 = BEMAN_BIG_INT_NAMESPACE::detail::int128_t;
    // Truncated to 64 bits, these would be shifts by 1 and by 0.
    expect_left_shift_of_one_throws((u128{1} << 64) | 1U);
    expect_left_shift_of_one_throws(u128{1} << 64);
    expect_left_shift_of_one_throws((i128{1} << 64) | 1);
#endif
}

TEST(LengthError, PmrReserveBeyondMaxThrows) {
    BEMAN_BIG_INT_NAMESPACE::pmr::big_int x{42U};
    EXPECT_THROW(x.reserve_representation(max_limbs + 1), std::length_error);
    EXPECT_EQ(x, 42U);
}
