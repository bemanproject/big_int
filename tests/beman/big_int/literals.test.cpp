// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <type_traits>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

// Clang rejects a literal suffix which does not begin with an underscore at the point of
// use, so `n` and `N` can only be spelled as literal suffixes on the other compilers:
// https://github.com/llvm/llvm-project/issues/76394
#if !defined(BEMAN_BIG_INT_CLANG)
    #define BEMAN_BIG_INT_TEST_BARE_SUFFIX
#endif

// This file names the reserved suffixes throughout, both as literal suffixes and by
// naming the operator templates directly.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wreserved-user-defined-literal")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wliteral-suffix")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4455)

namespace {

using namespace BEMAN_BIG_INT_NAMESPACE::literals;

using BEMAN_BIG_INT_NAMESPACE::big_int;

// [big.int.literal]
// `operator""N`, `operator""_n` and `operator""_N` delegate to `operator""n`, so every
// spelling of a literal must produce the same type, the same value, and the same
// exception specification.

static_assert(std::is_same_v<decltype(0_n), big_int>);
static_assert(std::is_same_v<decltype(0_N), big_int>);

// Every base and prefix spelling, through the capitalized suffix.
static_assert(0_N == 0_n);
static_assert(1_N != 0_N);
static_assert(255_N == 255_n);
static_assert(0xff_N == 255_N);
static_assert(0XFF_N == 255_N);
static_assert(0xFf_N == 255_N);
static_assert(0b1111'1111_N == 255_N);
static_assert(0B11111111_N == 255_N);
static_assert(0377_N == 255_N);

// Digit separators are removed before parsing, whichever suffix is used.
static_assert(1000_N == 1'0'00_N);
static_assert(1'000'000_N == 1000000_n);

// A literal is never negative; unary minus applies to the resulting `big_int`.
static_assert(-42_N == -42_n);
static_assert(-1'000'000_N < 0_N);

// Values too large for the in-place storage take the pre-computed limb path.
static_assert(1'000'000'000'000'000'000'000'000'000_N == 0x33b'2e3c'9fd0'803c'e800'0000_N);
static_assert(340'282'366'920'938'463'463'374'607'431'768'211'457_N == (1_N << 128) + 1_N);

// A literal which fits in the in-place storage constructs without allocating and is
// therefore `noexcept`; one which does not fit allocates and is potentially-throwing.
// The boundary is `inplace_bits`, independent of the limb width.
static_assert(big_int::inplace_bits == 64);
static_assert(noexcept(18446744073709551615_N));
static_assert(noexcept(0xFFFF'FFFF'FFFF'FFFF_N));
static_assert(!noexcept(18446744073709551616_N));
static_assert(!noexcept(0x1'0000'0000'0000'0000_N));
static_assert(noexcept(255_n) == noexcept(255_N));
static_assert(noexcept(1'000'000'000'000'000'000'000'000'000_n) == noexcept(1'000'000'000'000'000'000'000'000'000_N));

// The bare suffixes cannot be spelled as literals everywhere, but naming the operator
// templates directly exercises `operator""n` and `operator""N` on every compiler.
// clang-format off
static_assert(std::is_same_v<decltype(operator""N<'0'>()), big_int>);
static_assert(operator""n<'2', '5', '5'>() == 255_n);
static_assert(operator""N<'2', '5', '5'>() == 255_n);
static_assert(operator""_n<'2', '5', '5'>() == 255_n);
static_assert(operator""_N<'2', '5', '5'>() == 255_n);
static_assert(operator""N<'0', 'x', 'f', 'f'>() == 255_n);
static_assert(operator""N<'1', '\'', '0', '0', '0'>() == 1000_n);
static_assert(noexcept(operator""N<'2', '5', '5'>()));
static_assert(!noexcept(operator""N<'1', '8', '4', '4', '6', '7', '4', '4', '0', '7',
                                    '3', '7', '0', '9', '5', '5', '1', '6', '1', '6'>()));
// clang-format on

#ifdef BEMAN_BIG_INT_TEST_BARE_SUFFIX
static_assert(std::is_same_v<decltype(0n), big_int>);
static_assert(std::is_same_v<decltype(0N), big_int>);

static_assert(255n == 255_n);
static_assert(255N == 255_n);
static_assert(0xffN == 255n);
static_assert(0B1111'1111N == 255N);
static_assert(0377n == 255N);
static_assert(-42N == -42_n);
static_assert(1'000'000'000'000'000'000'000'000'000N == 0x33b'2e3c'9fd0'803c'e800'0000n);
static_assert(noexcept(255N));
static_assert(!noexcept(18446744073709551616N));
#endif // BEMAN_BIG_INT_TEST_BARE_SUFFIX

// The value a suffix produces at run time, where allocation is permitted, must match the
// value it produces during constant evaluation.
TEST(Literals, CapitalizedSuffixMatchesLowercase) {
    EXPECT_EQ(0_N, 0_n);
    EXPECT_EQ(255_N, 255_n);
    EXPECT_EQ(0xff_N, 255_n);
    EXPECT_EQ(-1'000'000_N, -1000000_n);
    EXPECT_EQ(340'282'366'920'938'463'463'374'607'431'768'211'457_N,
              340'282'366'920'938'463'463'374'607'431'768'211'457_n);
    EXPECT_EQ(to_string(340'282'366'920'938'463'463'374'607'431'768'211'457_N),
              "340282366920938463463374607431768211457");
}

// The allocating path builds the value from a pre-computed limb array, which must observe
// the same storage and normalization invariants as any other `big_int`.
TEST(Literals, AllocatingLiteralInvariants) {
    const auto small = 255_N;
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_inplace(small));
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_normalized(small));

    // 2^256, both in decimal and in hexadecimal.
    const auto large = 115792089237316195423570985008687907853269984665640564039457584007913129639936_N;
    EXPECT_FALSE(BEMAN_BIG_INT_NAMESPACE::is_inplace(large));
    EXPECT_TRUE(BEMAN_BIG_INT_NAMESPACE::is_normalized(large));
    EXPECT_EQ(large, 1_N << 256);
    EXPECT_EQ(large, 0x1'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000_N);
}

#ifdef BEMAN_BIG_INT_TEST_BARE_SUFFIX
TEST(Literals, BareSuffix) {
    EXPECT_EQ(255n, 255_n);
    EXPECT_EQ(255N, 255_n);
    EXPECT_EQ(-0xffN, -255_n);
    EXPECT_EQ(340'282'366'920'938'463'463'374'607'431'768'211'457N, (1N << 128) + 1n);
    EXPECT_EQ(to_string(340'282'366'920'938'463'463'374'607'431'768'211'457N),
              "340282366920938463463374607431768211457");
}
#endif // BEMAN_BIG_INT_TEST_BARE_SUFFIX

} // namespace

BEMAN_BIG_INT_DIAGNOSTIC_POP()
