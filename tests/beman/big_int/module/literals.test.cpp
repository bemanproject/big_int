// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Consumer-side tests of the `n`/`N`/`_n`/`_N` literal suffixes through
// `import beman.big_int;`. Modeled on tests/beman/big_int/literals.test.cpp, with
// everything that needs a library macro (BEMAN_BIG_INT_*) or a header
// (testing.hpp, beman/big_int.hpp) stripped: macros do not cross a module
// boundary, and including a public header under BEMAN_BIG_INT_BUILD_MODULE is
// ill-formed. BEMAN_BIG_INT_NAMESPACE is the exception: CMake passes it on the
// command line.
//
// Diagnostic suppression below uses raw compiler pragmas rather than the
// library's BEMAN_BIG_INT_DIAGNOSTIC_* macros, for the same reason.

#include <type_traits>

#include <gtest/gtest.h>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;
namespace {

using namespace BEMAN_BIG_INT_NAMESPACE::literals;

using BEMAN_BIG_INT_NAMESPACE::big_int;

// [big.int.literal]
// `operator""N`, `operator""_n` and `operator""_N` delegate to `operator""n`, so
// every spelling of a literal must produce the same type, the same value, and
// the same exception specification.

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

// Values too large for the in-place storage take the pre-computed limb path,
// and must still agree with the non-allocating path bit for bit.
static_assert(1'000'000'000'000'000'000'000'000'000_N == 0x33b'2e3c'9fd0'803c'e800'0000_N);
static_assert(340'282'366'920'938'463'463'374'607'431'768'211'457_N == (1_N << 128) + 1_N);

// A wide literal, spelled out to a known decimal value.
static_assert((1_n << 128) + 1_n == 340282366920938463463374607431768211457_n);

// A literal which fits in the in-place storage constructs without allocating and
// is therefore `noexcept`; one which does not fit allocates and is
// potentially-throwing. The boundary is `inplace_bits`, independent of the limb
// width. Verified against the library rather than assumed: `big_int` is
// `basic_big_int<64, ...>` (min_inplace_bits = 64), and on this build the limb is
// 64 bits wide and a pointer is one limb, so inplace_capacity == 1 limb == 64 bits.
static_assert(big_int::inplace_bits == 64);
static_assert(noexcept(18446744073709551615_N));     // 2^64 - 1: fits one limb.
static_assert(noexcept(0xFFFF'FFFF'FFFF'FFFF_N));    // Same value, hex.
static_assert(!noexcept(18446744073709551616_N));    // 2^64: needs a second limb.
static_assert(!noexcept(0x1'0000'0000'0000'0000_N)); // Same value, hex.
static_assert(noexcept(255_n) == noexcept(255_N));
static_assert(noexcept(1'000'000'000'000'000'000'000'000'000_n) == noexcept(1'000'000'000'000'000'000'000'000'000_N));

// Naming the operator templates directly exercises `operator""n` and
// `operator""N` on every compiler, independent of whether the bare-suffix
// literal syntax is accepted. Empirically, Clang's -Wreserved-user-defined-literal
// fires even on this template-id spelling (not just the literal syntax), so it
// needs the same suppression the library applies at the point of definition.
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wreserved-user-defined-literal"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wliteral-suffix"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4455)
#endif

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

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif

// The bare `n`/`N` suffixes (without a leading underscore) are a
// reserved-identifier extension. Clang rejects them at the point of use (a hard
// error, not merely a warning) regardless of -Wpedantic:
// https://github.com/llvm/llvm-project/issues/76394
// Empirically, under -Wpedantic -Werror this compiles clean on every other
// spelling above without any diagnostic suppression, so only this block needs
// guarding.
#ifndef __clang__
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
#endif // __clang__

// The value a suffix produces at run time, where allocation is permitted, must
// match the value it produces during constant evaluation.
TEST(Literals, CapitalizedSuffixMatchesLowercase) {
    EXPECT_EQ(0_N, 0_n);
    EXPECT_EQ(255_N, 255_n);
    EXPECT_EQ(0xff_N, 255_n);
    EXPECT_EQ(-1'000'000_N, -1000000_n);
    EXPECT_EQ(340'282'366'920'938'463'463'374'607'431'768'211'457_N,
              340'282'366'920'938'463'463'374'607'431'768'211'457_n);
}

#ifndef __clang__
TEST(Literals, BareSuffix) {
    EXPECT_EQ(255n, 255_n);
    EXPECT_EQ(255N, 255_n);
    EXPECT_EQ(-0xffN, -255_n);
    EXPECT_EQ(340'282'366'920'938'463'463'374'607'431'768'211'457N, (1N << 128) + 1n);
}
#endif // __clang__

} // namespace
