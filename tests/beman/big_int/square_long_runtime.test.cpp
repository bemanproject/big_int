// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Direct tests of beman_big_int_square_long_runtime (the assembly kernel
// (x86_64/AArch64), or its portable fallback elsewhere) against multiply_long(a, a).

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/square_long_runtime.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

using limb = ::beman::big_int::uint_multiprecision_t;

constexpr limb        limb_max    = std::numeric_limits<limb>::max();
constexpr limb        poison      = limb_max / 0xFF * 0xA5; // 0xA5 in every byte
constexpr std::size_t guard_limbs = 4;
constexpr std::size_t max_limbs   = 160;

// Squares `a` into a poisoned buffer framed by guard limbs, checking the
// product against multiply_long and that nothing outside [0, 2n) is written.
void expect_square_matches(const std::vector<limb>& a, const char* pattern) {
    const std::size_t n = a.size();

    std::vector<limb> expected(2 * n);
    ::beman::big_int::detail::multiply_long(expected, a, a);

    std::vector<limb> buf(2 * n + 2 * guard_limbs, poison);
    ::beman_big_int_square_long_runtime(buf.data() + guard_limbs, a.data(), n);

    for (std::size_t k = 0; k < guard_limbs; ++k) {
        ASSERT_EQ(buf[k], poison) << pattern << " n=" << n << " leading guard " << k;
        ASSERT_EQ(buf[guard_limbs + 2 * n + k], poison) << pattern << " n=" << n << " trailing guard " << k;
    }
    for (std::size_t k = 0; k < 2 * n; ++k) {
        ASSERT_EQ(buf[guard_limbs + k], expected[k]) << pattern << " n=" << n << " limb " << k;
    }
}

TEST(SquareLongRuntime, EmptyOperandWritesNothing) {
    std::vector<limb> buf(2 * guard_limbs, poison);
    const limb        a = 1;
    ::beman_big_int_square_long_runtime(buf.data() + guard_limbs, &a, 0);
    for (const limb x : buf) {
        EXPECT_EQ(x, poison);
    }
}

// All-ones limbs drive every carry to its maximum: the triangle rows carry out
// every limb and the diagonal pass sees the largest `extra`.
TEST(SquareLongRuntime, AllOnes) {
    for (std::size_t n = 1; n <= max_limbs; ++n) {
        expect_square_matches(std::vector<limb>(n, limb_max), "all-ones");
    }
}

TEST(SquareLongRuntime, Random) {
    std::mt19937_64                     rng{0x5157A12EULL};
    std::uniform_int_distribution<limb> dist;
    for (unsigned trial = 0; trial < 4; ++trial) {
        for (std::size_t n = 1; n <= max_limbs; ++n) {
            std::vector<limb> a(n);
            for (limb& x : a) {
                x = dist(rng);
            }
            expect_square_matches(a, "random");
        }
    }
}

// Sparse and structured operands: zero limbs inside the triangle, lone high
// bits that make the doubling shift carry between limb pairs, and zero
// multipliers that leave whole rows as pure carry propagation.
TEST(SquareLongRuntime, Structured) {
    constexpr limb top_bit = limb{1} << (std::numeric_limits<limb>::digits - 1);

    for (std::size_t n = 1; n <= max_limbs; ++n) {
        std::vector<limb> a(n, top_bit);
        expect_square_matches(a, "top-bit");

        std::vector<limb> alternating(n);
        for (std::size_t i = 0; i < n; ++i) {
            alternating[i] = (i % 2 == 0) ? limb_max : limb{0};
        }
        expect_square_matches(alternating, "alternating");

        std::vector<limb> ends(n, limb{0});
        ends.front() = limb_max;
        ends.back()  = limb_max;
        expect_square_matches(ends, "ends");

        std::vector<limb> one_less(n, limb_max);
        one_less.front() = limb_max - 1;
        expect_square_matches(one_less, "all-ones-minus-one");
    }
}

} // namespace
