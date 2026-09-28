// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Direct tests of beman_big_int_multiply_long_runtime (the assembly kernel,
// or its portable fallback elsewhere) against multiply_long.

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/multiply_long_runtime.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <random>
#include <vector>

namespace {

using limb = ::beman::big_int::uint_multiprecision_t;

constexpr limb        limb_max    = std::numeric_limits<limb>::max();
constexpr limb        poison      = limb_max / 0xFF * 0xA5; // 0xA5 in every byte
constexpr std::size_t guard_limbs = 4;
constexpr std::size_t max_len     = 40;

// Multiplies a * b into a poisoned buffer framed by guard limbs, checking the
// product against multiply_long and that nothing outside [0, len_a + len_b) is written.
void expect_multiply_matches(const std::vector<limb>& a, const std::vector<limb>& b, const char* pattern) {
    const std::size_t len_a = a.size();
    const std::size_t len_b = b.size();

    std::vector<limb> expected(len_a + len_b);
    ::beman::big_int::detail::multiply_long(expected, a, b);

    std::vector<limb> buf(len_a + len_b + 2 * guard_limbs, poison);
    ::beman_big_int_multiply_long_runtime(buf.data() + guard_limbs, a.data(), len_a, b.data(), len_b);

    for (std::size_t k = 0; k < guard_limbs; ++k) {
        ASSERT_EQ(buf[k], poison) << pattern << " len_a=" << len_a << " len_b=" << len_b << " leading guard " << k;
        ASSERT_EQ(buf[guard_limbs + len_a + len_b + k], poison)
            << pattern << " len_a=" << len_a << " len_b=" << len_b << " trailing guard " << k;
    }
    for (std::size_t k = 0; k < len_a + len_b; ++k) {
        ASSERT_EQ(buf[guard_limbs + k], expected[k])
            << pattern << " len_a=" << len_a << " len_b=" << len_b << " limb " << k;
    }
}

// Either operand empty (0xN and Nx0) must write nothing at all: no product
// limbs and no carry-out limb, matching the portable fallback's early return.
TEST(MultiplyLongRuntime, EmptyOperandWritesNothing) {
    const limb a[2] = {1, 2};
    const limb b[3] = {3, 4, 5};

    {
        std::vector<limb> buf(3 + 2 * guard_limbs, poison);
        ::beman_big_int_multiply_long_runtime(buf.data() + guard_limbs, a, 0, b, 3);
        for (const limb x : buf) {
            EXPECT_EQ(x, poison);
        }
    }
    {
        std::vector<limb> buf(2 + 2 * guard_limbs, poison);
        ::beman_big_int_multiply_long_runtime(buf.data() + guard_limbs, a, 2, b, 0);
        for (const limb x : buf) {
            EXPECT_EQ(x, poison);
        }
    }
}

// Every (len_a, len_b) pair in [1, 24] x [1, 24]: both orders (len_a < len_b
// and len_a > len_b) exercise the kernel's internal operand swap, and every
// length covers every len & 3 remainder for the 4x unrolled row body.
TEST(MultiplyLongRuntime, AllOnes) {
    for (std::size_t len_a = 1; len_a <= max_len; ++len_a) {
        for (std::size_t len_b = 1; len_b <= max_len; ++len_b) {
            expect_multiply_matches(
                std::vector<limb>(len_a, limb_max), std::vector<limb>(len_b, limb_max), "all-ones");
        }
    }
}

TEST(MultiplyLongRuntime, Random) {
    std::mt19937_64                     rng{0x8B1D2A57ULL};
    std::uniform_int_distribution<limb> dist;
    for (std::size_t len_a = 1; len_a <= max_len; ++len_a) {
        for (std::size_t len_b = 1; len_b <= max_len; ++len_b) {
            std::vector<limb> a(len_a);
            std::vector<limb> b(len_b);
            for (limb& x : a) {
                x = dist(rng);
            }
            for (limb& x : b) {
                x = dist(rng);
            }
            expect_multiply_matches(a, b, "random");
        }
    }
}

// Sparse and structured operands: long runs of zero limbs on either side, so
// whole rows or columns of the schoolbook grid are pure carry propagation
// with no product term.
TEST(MultiplyLongRuntime, Sparse) {
    constexpr limb top_bit = limb{1} << (std::numeric_limits<limb>::digits - 1);

    for (std::size_t len_a = 1; len_a <= max_len; ++len_a) {
        for (std::size_t len_b = 1; len_b <= max_len; ++len_b) {
            std::vector<limb> ends_a(len_a, limb{0});
            ends_a.front() = limb_max;
            ends_a.back()  = limb_max;

            std::vector<limb> alternating_b(len_b);
            for (std::size_t i = 0; i < len_b; ++i) {
                alternating_b[i] = (i % 2 == 0) ? top_bit : limb{0};
            }
            expect_multiply_matches(ends_a, alternating_b, "sparse-ends-vs-alternating");

            std::vector<limb> every_third_a(len_a, limb{0});
            for (std::size_t i = 0; i < len_a; i += 3) {
                every_third_a[i] = limb_max;
            }

            std::vector<limb> tail_b(len_b, limb{0});
            tail_b.back() = limb_max;
            expect_multiply_matches(every_third_a, tail_b, "sparse-every-third-vs-tail");
        }
    }
}

// Shapes past the 4x unrolled small grid, including one much longer than the
// other in both directions.
TEST(MultiplyLongRuntime, LargeShapes) {
    std::mt19937_64                     rng{0x3C9E7F41ULL};
    std::uniform_int_distribution<limb> dist;

    auto random_vec = [&](std::size_t n) {
        std::vector<limb> v(n);
        for (limb& x : v) {
            x = dist(rng);
        }
        return v;
    };

    expect_multiply_matches(random_vec(97), random_vec(160), "large");
    expect_multiply_matches(random_vec(160), random_vec(97), "large");
    expect_multiply_matches(random_vec(200), random_vec(200), "large");
}

// Extreme aspect ratios: a single row multiplying (or multiplied by) a very
// long operand, and a few-limbs-by-many-limbs shape that drives the
// row-pair path's steady loop for a long time on one side while the other
// side is too short to ever pair.
TEST(MultiplyLongRuntime, UnbalancedShapes) {
    std::mt19937_64                     rng{0x1B4E5A7DULL};
    std::uniform_int_distribution<limb> dist;

    auto random_vec = [&](std::size_t n) {
        std::vector<limb> v(n);
        for (limb& x : v) {
            x = dist(rng);
        }
        return v;
    };

    expect_multiply_matches(random_vec(1), random_vec(700), "unbalanced");
    expect_multiply_matches(random_vec(700), random_vec(1), "unbalanced");
    expect_multiply_matches(random_vec(2), random_vec(513), "unbalanced");
    expect_multiply_matches(random_vec(3), random_vec(500), "unbalanced");
    expect_multiply_matches(random_vec(47), random_vec(1000), "unbalanced");
    expect_multiply_matches(random_vec(1000), random_vec(47), "unbalanced");
}

} // namespace
