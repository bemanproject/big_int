// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Consumer-side tests of the arithmetic operator surface reached through
// `import beman.big_int;`. Two jobs: ordinary correctness for the operators, and
// -- the important one -- reaching the multiplication/division kernels that are
// compiled into libbeman.big_int from src/*.cpp, which is what proves the module
// target actually links the static library rather than resolving everything from
// inline templates.
//
// The kernel-reach sizes below were picked by reading detail/mul_impl.hpp and
// detail/div_impl.hpp (64-bit limbs) and src/mul_dispatch.cpp's tier ladder for
// one build; the cutoffs are per-configuration tuning constants, so other
// configurations may route a given size to a neighbouring tier. That is fine
// here: every tier lives in the static library, and the point is that the
// module target links it.
//   `x * x` (same object on both sides of operator*, including `x *= x`) is
//   detected by a pointer/size compare in multiply_runtime and dispatched to
//   square_runtime, which uses a DIFFERENT ladder than `x * y` for distinct `x`
//   and `y` (the square_* cutoffs in detail/mul_impl.hpp).
//   Division's tier gate (divide_dispatch in detail/div_impl.hpp) routes to
//   Burnikel-Ziegler once the divisor is at least burnikel_ziegler_cutoff limbs
//   and the dividend is at least burnikel_ziegler_offset limbs wider, provided
//   it stays below the Barrett gates (barrett_march_cutoff and up).

#include <cstddef>
#include <limits>
#include <utility>

#include <gtest/gtest.h>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;
namespace {

using beman::big_int::big_int;
using beman::big_int::div_rem_to_zero;
using beman::big_int::div_result;
using beman::big_int::uint_multiprecision_t;

// ----- small-value ladder: binary operators against pinned values -----

TEST(Arithmetic, BinaryOperators) {
    const big_int a{47};
    const big_int b{9};

    EXPECT_EQ(a + b, 56);
    EXPECT_EQ(a - b, 38);
    EXPECT_EQ(a * b, 423);
    EXPECT_EQ(a / b, 5);
    EXPECT_EQ(a % b, 2);
}

TEST(Arithmetic, CompoundAssignment) {
    big_int x{100};
    x += big_int{23};
    EXPECT_EQ(x, 123);
    x -= big_int{23};
    EXPECT_EQ(x, 100);
    x *= big_int{4};
    EXPECT_EQ(x, 400);
    x /= big_int{8};
    EXPECT_EQ(x, 50);
    x %= big_int{7};
    EXPECT_EQ(x, 1);

    // Same set with a builtin right-hand side.
    big_int y{100};
    y += 23;
    EXPECT_EQ(y, 123);
    y -= 23;
    EXPECT_EQ(y, 100);
    y *= 4;
    EXPECT_EQ(y, 400);
    y /= 8;
    EXPECT_EQ(y, 50);
    y %= 7;
    EXPECT_EQ(y, 1);
}

TEST(Arithmetic, Bitwise) {
    const big_int a{0b1100};
    const big_int b{0b1010};

    EXPECT_EQ(a & b, 0b1000);
    EXPECT_EQ(a | b, 0b1110);
    EXPECT_EQ(a ^ b, 0b0110);
    // Bitwise complement emulates two's complement: ~x == -x - 1.
    EXPECT_EQ(~big_int{0}, -1);
    EXPECT_EQ(~big_int{5}, -6);
    EXPECT_EQ(a << 2, 0b110000);
    EXPECT_EQ(a >> 2, 0b0011);
}

TEST(Arithmetic, CompoundBitwise) {
    big_int x{0b1100};
    x &= big_int{0b1010};
    EXPECT_EQ(x, 0b1000);
    x |= big_int{0b0111};
    EXPECT_EQ(x, 0b1111);
    x ^= big_int{0b1010};
    EXPECT_EQ(x, 0b0101);
    x <<= 3;
    EXPECT_EQ(x, 0b0101000);
    x >>= 1;
    EXPECT_EQ(x, 0b0010100);
}

TEST(Arithmetic, IncrementDecrementBothFixities) {
    big_int x{41};

    EXPECT_EQ(++x, 42); // prefix increment
    EXPECT_EQ(x++, 42); // postfix increment
    EXPECT_EQ(x, 43);
    EXPECT_EQ(--x, 42); // prefix decrement
    EXPECT_EQ(x--, 42); // postfix decrement
    EXPECT_EQ(x, 41);
}

TEST(Arithmetic, UnaryPlusMinus) {
    const big_int a{42};
    EXPECT_EQ(+a, 42);
    EXPECT_EQ(-a, -42);
    EXPECT_EQ(-(-a), 42);
    EXPECT_EQ(+big_int{-7}, -7);
}

TEST(Arithmetic, AllSixComparisons) {
    const big_int a{5};
    const big_int b{9};

    EXPECT_TRUE(a == a);
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(b >= a);
    EXPECT_FALSE(a == b);
    EXPECT_FALSE(b < a);
}

TEST(Arithmetic, MixedOperandBuiltinEitherSide) {
    const big_int a{10};

    EXPECT_EQ(a + 1, 11);
    EXPECT_EQ(1 + a, 11);
    EXPECT_EQ(a - 3, 7);
    EXPECT_EQ(20 - a, 10);
    EXPECT_EQ(a * 2, 20);
    EXPECT_EQ(2 * a, 20);
    EXPECT_EQ(a / 3, 3);
    EXPECT_EQ(100 / a, 10);
    EXPECT_EQ(a % 3, 1);
    EXPECT_EQ(7 % a, 7);
    EXPECT_TRUE(a == 10);
    EXPECT_TRUE(10 == a);
    EXPECT_TRUE(a < 100);
    EXPECT_TRUE(100 > a);
    EXPECT_TRUE(a >= 10);
    EXPECT_TRUE(10 <= a);
}

// ----- div_rem_to_zero agreeing with / and %, across the sign quadrants -----

TEST(Arithmetic, DivRemToZeroAgreesAcrossSignQuadrants) {
    struct case_t {
        int x;
        int y;
        int quotient;
        int remainder;
    };
    // Rounding toward zero: the quotient's sign is the product of the operand
    // signs, and the remainder's sign always follows the dividend.
    const case_t cases[] = {
        {7, 2, 3, 1},
        {-7, 2, -3, -1},
        {7, -2, -3, 1},
        {-7, -2, 3, -1},
    };
    for (const case_t& c : cases) {
        const big_int x{c.x};
        const big_int y{c.y};

        EXPECT_EQ(x / y, c.quotient);
        EXPECT_EQ(x % y, c.remainder);

        const div_result<big_int> dr = div_rem_to_zero(x, y);
        EXPECT_EQ(dr.quotient, c.quotient);
        EXPECT_EQ(dr.remainder, c.remainder);
        EXPECT_EQ(dr.quotient, x / y);
        EXPECT_EQ(dr.remainder, x % y);
    }
}

// ----- kernel reach: multiplication tiers -----
//
// `x * x` (same big_int on both sides) is detected as squaring and uses the
// square_* cutoffs, not the general multiply ones -- see the file header.

TEST(Arithmetic, KaratsubaTierSquare) {
    constexpr std::size_t k       = 9600; // 150 limbs: a Karatsuba-tier square on most configurations.
    const big_int         m       = (big_int{1} << k) - 1;
    const big_int         squared = m * m;
    EXPECT_EQ(squared, (big_int{1} << (2 * k)) - (big_int{1} << (k + 1)) + 1);
}

TEST(Arithmetic, ToomCook3TierSquare) {
    constexpr std::size_t k       = 51200; // 800 limbs: a Toom-3-tier square on most configurations.
    const big_int         m       = (big_int{1} << k) - 1;
    const big_int         squared = m * m;
    EXPECT_EQ(squared, (big_int{1} << (2 * k)) - (big_int{1} << (k + 1)) + 1);
}

TEST(Arithmetic, ToomCook4TierSquare) {
    constexpr std::size_t k       = 140800; // 2200 limbs: a Toom-4-tier square on most configurations.
    const big_int         m       = (big_int{1} << k) - 1;
    const big_int         squared = m * m;
    EXPECT_EQ(squared, (big_int{1} << (2 * k)) - (big_int{1} << (k + 1)) + 1);
}

// FFT tier: two DISTINCT operands (no squaring aliasing), large enough to reach the FFT
// on the AArch64 and portable configurations, checked by round-tripping through division
// rather than a closed-form literal.
TEST(Arithmetic, FftTierMultiply) {
    const big_int x       = (big_int{1} << 320000) - 1; // 5000 limbs.
    const big_int y       = (big_int{1} << 332800) - 3; // 5200 limbs.
    const big_int product = x * y;
    EXPECT_EQ(product / y, x);
    EXPECT_EQ(product % y, 0);
    EXPECT_EQ(product / x, y);
    EXPECT_EQ(product % x, 0);
}

// Burnikel-Ziegler division tier: divisor size (100 limbs) clears
// burnikel_ziegler_cutoff on every configuration and the dividend is more than
// burnikel_ziegler_offset limbs wider, while staying below the Barrett march
// line (dividend under 16x the divisor) and every other Barrett gate.
TEST(Arithmetic, BurnikelZieglerTierDivide) {
    const big_int divisor  = (big_int{1} << 6400) - 1;  // 100 limbs.
    const big_int dividend = (big_int{1} << 9600) - 12; // 150 limbs.

    const big_int q = dividend / divisor;
    const big_int r = dividend % divisor;
    EXPECT_EQ(q * divisor + r, dividend);
    EXPECT_GE(r, 0);
    EXPECT_LT(r, divisor);

    const div_result<big_int> dr = div_rem_to_zero(dividend, divisor);
    EXPECT_EQ(dr.quotient, q);
    EXPECT_EQ(dr.remainder, r);
}

// ----- aliasing at kernel sizes -----

TEST(Arithmetic, AliasingMultiplyAssign) {
    constexpr std::size_t k = 25600; // 400 limbs: a Karatsuba- or Toom-3-tier square, depending on the configuration.
    big_int               x = (big_int{1} << k) - 1;
    x *= x;
    EXPECT_EQ(x, (big_int{1} << (2 * k)) - (big_int{1} << (k + 1)) + 1);
}

TEST(Arithmetic, AliasingAddAssign) {
    constexpr std::size_t k = 25600;
    big_int               x = (big_int{1} << k) - 1;
    x += x;
    EXPECT_EQ(x, (big_int{1} << (k + 1)) - 2);
}

TEST(Arithmetic, AliasingDivideAssign) {
    constexpr std::size_t k = 25600;
    big_int               x = (big_int{1} << k) - 1;
    x /= x;
    EXPECT_EQ(x, 1);
}

// ----- capacity behaviour reachable from the public API only -----
//
// testing.hpp is off-limits from a module-consumer test, so `is_inplace` is
// reimplemented locally from representation_capacity() and inplace_capacity,
// both public members.

template <class T>
[[nodiscard]] bool is_inplace(const T& x) noexcept {
    return x.representation_capacity() == T::inplace_capacity;
}

TEST(Arithmetic, CapacityDefaultIsInplace) {
    const big_int x{42};
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x.capacity(), big_int::inplace_bits);
}

TEST(Arithmetic, ReserveGrowsPastInplace) {
    big_int x{1};
    x.reserve(big_int::inplace_bits + 64); // one limb beyond in-place capacity.
    EXPECT_FALSE(is_inplace(x));
    EXPECT_GT(x.representation_capacity(), big_int::inplace_capacity);
}

TEST(Arithmetic, CapacityTracksRepresentationCapacity) {
    constexpr std::size_t digits = static_cast<std::size_t>(std::numeric_limits<uint_multiprecision_t>::digits);
    big_int               x{1};
    x.reserve_representation(big_int::inplace_capacity + 8);
    EXPECT_EQ(x.capacity(), x.representation_capacity() * digits);
}

TEST(Arithmetic, ShrinkToFitReturnsToInplace) {
    big_int x{7};
    x.reserve_representation(big_int::inplace_capacity + 8);
    ASSERT_FALSE(is_inplace(x));
    x.shrink_to_fit();
    EXPECT_TRUE(is_inplace(x));
    EXPECT_EQ(x, 7);
}

} // namespace
