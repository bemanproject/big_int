// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Exercises to_chars/from_chars (include/beman/big_int/charconv.hpp) and
// to_string/to_wstring (include/beman/big_int/string.hpp) through
// `import beman.big_int;`. `to_chars`/`from_chars` are ordinary namespace-scope
// function templates in beman::big_int that are NOT themselves marked
// BEMAN_BIG_INT_EXPORT (only to_string/to_wstring are); they are called here
// unqualified so that they are found via argument-dependent lookup on their
// basic_big_int argument, which the module attaches to the global module and
// makes reachable to this translation unit.

#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <system_error>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;
namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using namespace BEMAN_BIG_INT_NAMESPACE::literals;

// A handful of representative values, exercised across every base under test.
constexpr long long some_positive = 123456789;
constexpr long long some_negative = -123456789;

} // namespace

// [to_chars] ==================================================================

TEST(CharconvString, ToCharsBase2) {
    const big_int        x = 255;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 2);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "11111111");
}

TEST(CharconvString, ToCharsBase8) {
    const big_int        x = 255;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 8);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "377");
}

TEST(CharconvString, ToCharsBase10) {
    const big_int        x = some_positive;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 10);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "123456789");
}

TEST(CharconvString, ToCharsBase16) {
    const big_int        x = 255;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 16);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "ff");
}

TEST(CharconvString, ToCharsBase36) {
    const big_int        x = 35;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 36);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "z");
}

TEST(CharconvString, ToCharsZero) {
    const big_int       x = 0;
    std::array<char, 8> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 10);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "0");
}

TEST(CharconvString, ToCharsNegative) {
    const big_int        x = some_negative;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 10);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "-123456789");
}

TEST(CharconvString, ToCharsNegativeHex) {
    const big_int        x = -255;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 16);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "-ff");
}

// big_int.hpp forward-declares `to_chars`/`from_chars` with a default `base = 10`
// argument (the redeclaration in charconv.hpp omits it, as default arguments are
// only spelled out once); this exercises that default.
TEST(CharconvString, ToCharsDefaultBase) {
    const big_int        x = some_positive;
    std::array<char, 64> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x);
    ASSERT_EQ(ec, std::errc{});
    const std::string_view result(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
    EXPECT_EQ(result, "123456789");
}

// An empty buffer: `to_chars` cannot write even one character, so it reports
// `value_too_large` and leaves `ptr` at `end` (charconv.hpp, `begin == end`).
TEST(CharconvString, ToCharsEmptyBufferIsValueTooLarge) {
    const big_int       x = 42;
    std::array<char, 0> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data(), x, 10);
    EXPECT_EQ(ec, std::errc::value_too_large);
    EXPECT_EQ(ptr, buf.data());
}

// A buffer one byte too short for a two-digit value.
TEST(CharconvString, ToCharsShortBufferIsValueTooLarge) {
    const big_int       x = 99;
    std::array<char, 1> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 10);
    EXPECT_EQ(ec, std::errc::value_too_large);
    EXPECT_EQ(ptr, buf.data() + buf.size());
}

// A buffer too short by exactly one byte for a negative value: the minus sign
// alone fits, but no digit does.
TEST(CharconvString, ToCharsShortBufferNegativeIsValueTooLarge) {
    const big_int       x = -5;
    std::array<char, 1> buf{};
    const auto [ptr, ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 10);
    EXPECT_EQ(ec, std::errc::value_too_large);
    EXPECT_EQ(ptr, buf.data() + buf.size());
}

// [from_chars] ================================================================

TEST(CharconvString, FromCharsRoundTripBase2) {
    const std::string text = "11111111";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 2);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{255});
}

TEST(CharconvString, FromCharsRoundTripBase8) {
    const std::string text = "377";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 8);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{255});
}

TEST(CharconvString, FromCharsRoundTripBase10) {
    const std::string text = "123456789";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 10);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{some_positive});
}

TEST(CharconvString, FromCharsRoundTripBase16) {
    const std::string text = "ff";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 16);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{255});
}

TEST(CharconvString, FromCharsRoundTripBase36) {
    const std::string text = "z";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 36);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{35});
}

TEST(CharconvString, FromCharsNegative) {
    const std::string text = "-123456789";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 10);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{some_negative});
}

TEST(CharconvString, FromCharsDefaultBase) {
    const std::string text = "123456789";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(out, big_int{some_positive});
}

// An out-of-range base is rejected outright: charconv.hpp checks
// `base < 2 || base > 36` before looking at the input, and returns `ptr == end`.
TEST(CharconvString, FromCharsInvalidBase) {
    const std::string text = "123";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 37);
    EXPECT_EQ(ec, std::errc::invalid_argument);
    EXPECT_EQ(ptr, text.data() + text.size());
}

// An empty range is likewise `invalid_argument`, with `ptr` reported as `end`.
TEST(CharconvString, FromCharsEmptyRange) {
    const std::string text;
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data(), out, 10);
    EXPECT_EQ(ec, std::errc::invalid_argument);
    EXPECT_EQ(ptr, text.data());
}

// A lone minus sign with no digits behind it is also `invalid_argument`.
TEST(CharconvString, FromCharsLoneMinusSign) {
    const std::string text = "-";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 10);
    EXPECT_EQ(ec, std::errc::invalid_argument);
    EXPECT_EQ(ptr, text.data() + text.size());
}

// Trailing characters that are not valid digits in the given base stop the
// parse there; `ptr` marks the first character not consumed, and `ec` is
// still success (a partial match is not an error for `from_chars`).
TEST(CharconvString, FromCharsPartialParseBase10) {
    const std::string text = "123abc";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 10);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + 3);
    EXPECT_EQ(out, big_int{123});
}

// The same idea in base 16, where 'g' is the first character outside the
// hexadecimal alphabet.
TEST(CharconvString, FromCharsPartialParseBase16) {
    const std::string text = "abcdefgh";
    big_int           out;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), out, 16);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + 6);
    EXPECT_EQ(out, big_int{0xabcdefLL});
}

// [constexpr] ==================================================================

// Confirms that both `to_chars` and `from_chars` can run during constant
// evaluation, exercising the same code paths used at runtime above.
namespace {

consteval bool check_charconv_constexpr() {
    const big_int x = 12345;

    std::array<char, 32> buf{};
    const auto [to_ptr, to_ec] = to_chars(buf.data(), buf.data() + buf.size(), x, 16);
    if (to_ec != std::errc{}) {
        return false;
    }
    const auto written = static_cast<std::size_t>(to_ptr - buf.data());
    if (written != 4 || buf[0] != '3' || buf[1] != '0' || buf[2] != '3' || buf[3] != '9') {
        return false;
    }

    big_int roundtrip;
    const auto [from_ptr, from_ec] = from_chars(buf.data(), buf.data() + written, roundtrip, 16);
    if (from_ec != std::errc{} || from_ptr != buf.data() + written) {
        return false;
    }
    return roundtrip == x;
}

static_assert(check_charconv_constexpr());

} // namespace

// [to_string / to_wstring] =====================================================

TEST(CharconvString, ToStringDefaultBase) {
    const big_int x = some_positive;
    EXPECT_EQ(to_string(x), "123456789");
}

TEST(CharconvString, ToStringExplicitBase) {
    const big_int x = 255;
    EXPECT_EQ(to_string(x, 16), "ff");
}

TEST(CharconvString, ToStringNegative) {
    const big_int x = some_negative;
    EXPECT_EQ(to_string(x), "-123456789");
}

TEST(CharconvString, ToWstringDefaultBase) {
    const big_int x = some_positive;
    EXPECT_EQ(to_wstring(x), L"123456789");
}

TEST(CharconvString, ToWstringExplicitBase) {
    const big_int x = 255;
    EXPECT_EQ(to_wstring(x, 16), L"ff");
}

TEST(CharconvString, ToWstringNegative) {
    const big_int x = some_negative;
    EXPECT_EQ(to_wstring(x), L"-123456789");
}

// [wide round trip] ============================================================

// A value wide enough (4096 bits) that base-10 conversion in both directions
// leaves the inline paths and engages the sub-quadratic FastIntegerOutput /
// FastIntegerInput kernels (see detail/base_conversion.hpp), all under the
// module import rather than a header include.
TEST(CharconvString, WideValueRoundTripsThroughToStringAndFromChars) {
    const big_int mersenne = (big_int{1} << 4096) - 1_n;

    const std::string text = to_string(mersenne);
    EXPECT_EQ(text.size(), 1234u); // 2^4096 - 1 has exactly 1234 decimal digits.

    big_int roundtrip;
    const auto [ptr, ec] = from_chars(text.data(), text.data() + text.size(), roundtrip, 10);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(ptr, text.data() + text.size());
    EXPECT_EQ(roundtrip, mersenne);
}
