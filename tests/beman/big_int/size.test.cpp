// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <utility>

#include <beman/big_int.hpp>

#include <gtest/gtest.h>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::is_inplace;
using BEMAN_BIG_INT_NAMESPACE::is_normalized;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

using pmr_big_int = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;

// `big_int` keeps a single inline limb, so any value >= 2^64 lives on the heap.
constexpr big_int two_pow(unsigned e) {
    big_int x{1};
    x <<= e;
    return x;
}

// ----- compile-time tests -----

// size() reads the top limb of the magnitude, so it needs a limb to read. Every
// zero, however it was reached, must still hold one trimmed limb: reporting 0
// here is what keeps the observer from indexing off the front of the buffer.
consteval bool zero_reports_no_significant_bits() {
    const big_int fresh;
    const big_int from_literal{0};
    big_int       from_subtraction = two_pow(200);
    from_subtraction -= two_pow(200);
    big_int from_shift = two_pow(200);
    from_shift >>= 200 + 1;
    big_int from_multiply = two_pow(200);
    from_multiply *= big_int{0};
    big_int from_modulus = two_pow(200);
    from_modulus %= two_pow(200);

    return fresh.size() == 0 && fresh.representation_size() == 1 && from_literal.size() == 0 &&
           from_subtraction.size() == 0 && from_subtraction.representation_size() == 1 && from_shift.size() == 0 &&
           from_multiply.size() == 0 && from_modulus.size() == 0 && is_normalized(from_subtraction) &&
           is_normalized(from_shift) && is_normalized(from_multiply) && is_normalized(from_modulus);
}
static_assert(zero_reports_no_significant_bits());

// The sign is not part of the width: a value and its negation span the same bits.
consteval bool negative_matches_its_magnitude() {
    const big_int small{-42};
    const big_int large = -two_pow(200);
    const big_int least{-1};

    return small.size() == 6 && large.size() == 201 && least.size() == 1 && small.size() == (-small).size() &&
           large.size() == (-large).size() && least.size() == (-least).size();
}
static_assert(negative_matches_its_magnitude());

static_assert(noexcept(std::declval<const big_int&>().size()));

// ----- runtime tests -----

// The same zeros at run time, where the debug assertions in size() are live.
TEST(Size, ZeroReportsNoSignificantBits) {
    const big_int fresh;
    EXPECT_EQ(fresh.size(), 0U);
    EXPECT_EQ(fresh.representation_size(), 1U);

    std::allocator<uint_multiprecision_t> alloc;
    const big_int                         with_allocator{0, alloc};
    EXPECT_EQ(with_allocator.size(), 0U);

    const big_int copied{fresh};
    EXPECT_EQ(copied.size(), 0U);

    big_int       moved_from;
    const big_int moved_to{std::move(moved_from)};
    EXPECT_EQ(moved_to.size(), 0U);
}

// Zero reached from a heap-allocated value: the magnitude has to be trimmed back
// to one limb, or size() would read a stale non-zero top limb.
TEST(Size, ZeroFromHeapAllocatedOperands) {
    big_int subtracted = two_pow(200);
    subtracted -= two_pow(200);
    EXPECT_EQ(subtracted.size(), 0U);
    EXPECT_EQ(subtracted.representation_size(), 1U);
    EXPECT_TRUE(is_normalized(subtracted));

    big_int shifted = two_pow(200);
    shifted >>= 201;
    EXPECT_EQ(shifted.size(), 0U);
    EXPECT_TRUE(is_normalized(shifted));

    big_int divided = two_pow(200);
    divided /= two_pow(201);
    EXPECT_EQ(divided.size(), 0U);
    EXPECT_TRUE(is_normalized(divided));

    big_int negative_to_zero = -two_pow(200);
    negative_to_zero += two_pow(200);
    EXPECT_EQ(negative_to_zero.size(), 0U);
    EXPECT_TRUE(is_normalized(negative_to_zero));
}

TEST(Size, NegativeMatchesItsMagnitude) {
    for (const unsigned bits : {0U, 1U, 63U, 64U, 65U, 200U}) {
        const big_int magnitude = two_pow(bits);
        const big_int negated   = -magnitude;
        EXPECT_EQ(negated.size(), bits + 1) << "bits " << bits;
        EXPECT_EQ(negated.size(), magnitude.size()) << "bits " << bits;
    }

    EXPECT_EQ(big_int{-1}.size(), 1U);
    EXPECT_EQ(big_int{-42}.size(), 6U);
}

// A counting resource: size() is a `noexcept` observer, so it must not reach the
// allocator at all. It once copied and negated a negative value to measure the
// magnitude, which allocated -- and could therefore terminate on a heap value.
class counting_resource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t alloc_count() const noexcept { return m_allocs; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        ++m_allocs;
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::size_t m_allocs{0};
};

TEST(Size, DoesNotAllocate) {
    counting_resource cr;
    pmr_big_int       x{-1, &cr};
    pmr_big_int       y{-1, &cr};
    x <<= 200; // heap, served by cr
    y <<= 200;
    ASSERT_FALSE(is_inplace(x));

    const std::size_t allocs = cr.alloc_count();
    ASSERT_GE(allocs, 1U);

    EXPECT_EQ(x.size(), 201U);
    EXPECT_EQ(cr.alloc_count(), allocs);

    x -= y; // -2^200 - -2^200 == 0
    const std::size_t allocs_after_zero = cr.alloc_count();
    EXPECT_EQ(x.size(), 0U);
    EXPECT_EQ(cr.alloc_count(), allocs_after_zero);
}

} // namespace
