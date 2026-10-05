// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/format.hpp>

#include <gtest/gtest.h>

#include <version>

#if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L

    #include <boost/multiprecision/cpp_int.hpp>

    #include <climits>
    #include <cstddef>
    #include <format>
    #include <locale>
    #include <string>
    #include <string_view>
    #include <vector>

namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::from_chars;
using BEMAN_BIG_INT_NAMESPACE::to_string;
using namespace BEMAN_BIG_INT_NAMESPACE::big_int_literals;

// std::vformat is [[nodiscard]]; this non-nodiscard wrapper lets EXPECT_THROW / EXPECT_NO_THROW
// invoke it without tripping -Werror=unused-result.
std::string vformat_call(const std::string_view spec, std::format_args args) { return std::vformat(spec, args); }

// A locale whose numpunct groups digits in threes with a comma, for the 'L' option.
struct comma_grouping : std::numpunct<char> {
    char        do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};

// Differential oracle: big_int must format exactly like the builtin reference for the same
// spec, and must throw std::format_error exactly when the builtin does. Returns a gtest
// AssertionResult so failures report the spec, value, and both outputs.
template <class T>
[[nodiscard]] ::testing::AssertionResult same_fmt(const std::locale* loc, const std::string_view spec, T v) {
    big_int     b(v);
    std::string got;
    std::string want;
    bool        got_threw  = false;
    bool        want_threw = false;
    try {
        want = loc ? std::vformat(*loc, spec, std::make_format_args(v)) : std::vformat(spec, std::make_format_args(v));
    } catch (const std::format_error&) {
        want_threw = true;
    }
    try {
        got = loc ? std::vformat(*loc, spec, std::make_format_args(b)) : std::vformat(spec, std::make_format_args(b));
    } catch (const std::format_error&) {
        got_threw = true;
    }
    if (got_threw != want_threw) {
        return ::testing::AssertionFailure() << "throw mismatch for spec=" << spec << " value=" << v
                                             << " big_int_threw=" << got_threw << " builtin_threw=" << want_threw;
    }
    if (!got_threw && got != want) {
        return ::testing::AssertionFailure()
               << "spec=" << spec << " value=" << v << " big_int=[" << got << "] builtin=[" << want << "]";
    }
    return ::testing::AssertionSuccess();
}

template <class T>
[[nodiscard]] ::testing::AssertionResult same_fmt(const std::string_view spec, T v) {
    return same_fmt(nullptr, spec, v);
}

// Inserts a comma every three digits from the right (mirrors comma_grouping for the magnitude).
[[nodiscard]] std::string add_commas(std::string_view digits) {
    std::string out;
    std::size_t count = 0;
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (count != 0 && count % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[i]);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// Concatenates `n` copies of `s`, for building an expected run of fill characters.
template <class S>
[[nodiscard]] S repeat(const S& s, const std::size_t n) {
    S out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

[[nodiscard]] std::string to_upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return s;
}

// ------------------------------------------------------------------------------------------
// Well-specified regression anchors (safe to assert exact output on every conforming library).
// ------------------------------------------------------------------------------------------
TEST(Format, SemanticAnchors) {
    EXPECT_EQ(std::format("{}", big_int{0}), "0");
    EXPECT_EQ(std::format("{}", big_int{-7}), "-7");
    EXPECT_EQ(std::format("{:5}", big_int{42}), "   42"); // numbers default to right alignment
    EXPECT_EQ(std::format("{:<5}", big_int{42}), "42   ");
    EXPECT_EQ(std::format("{:^7}", big_int{42}), "  42   ");
    EXPECT_EQ(std::format("{:*>6}", big_int{42}), "****42");

    // Sign options.
    EXPECT_EQ(std::format("{:+}", big_int{42}), "+42");
    EXPECT_EQ(std::format("{:+}", big_int{0}), "+0");
    EXPECT_EQ(std::format("{: }", big_int{42}), " 42");
    EXPECT_EQ(std::format("{: }", big_int{-42}), "-42");

    // Sign-aware zero padding: zeros go after the sign and after the base prefix.
    EXPECT_EQ(std::format("{:06d}", big_int{42}), "000042");
    EXPECT_EQ(std::format("{:+06d}", big_int{42}), "+00042");
    EXPECT_EQ(std::format("{:06d}", big_int{-42}), "-00042");
    EXPECT_EQ(std::format("{:08x}", big_int{255}), "000000ff");
    EXPECT_EQ(std::format("{:#06x}", big_int{255}), "0x00ff");
    EXPECT_EQ(std::format("{:#06X}", big_int{255}), "0X00FF");
    EXPECT_EQ(std::format("{:<06d}", big_int{42}), "42    "); // explicit align disables zero-pad

    // Alternate form across bases, including the octal-zero edge case.
    EXPECT_EQ(std::format("{:#b}", big_int{5}), "0b101");
    EXPECT_EQ(std::format("{:#B}", big_int{5}), "0B101");
    EXPECT_EQ(std::format("{:#o}", big_int{64}), "0100");
    EXPECT_EQ(std::format("{:#x}", big_int{255}), "0xff");
    EXPECT_EQ(std::format("{:#o}", big_int{0}), "0");   // no extra prefix for zero
    EXPECT_EQ(std::format("{:#x}", big_int{0}), "0x0"); // prefix unconditional for x/b
    EXPECT_EQ(std::format("{:#b}", big_int{0}), "0b0");
    EXPECT_EQ(std::format("{:#d}", big_int{42}), "42"); // '#' is a no-op for decimal
}

// ------------------------------------------------------------------------------------------
// Differential oracle: cartesian product of every spec field for values that fit a builtin.
// L and c are excluded here (covered separately) since their corner cases are library/locale
// specific; everything in this product is fully specified and identical across libraries.
// ------------------------------------------------------------------------------------------
TEST(Format, DifferentialOracleSigned) {
    const std::vector<std::string> fillalign = {"", "<", ">", "^", "*<", "*>", "*^"};
    const std::vector<std::string> signs     = {"", "+", "-", " "};
    const std::vector<std::string> alts      = {"", "#"};
    const std::vector<std::string> zeros     = {"", "0"};
    const std::vector<std::string> widths    = {"", "1", "6", "20"};
    const std::vector<std::string> types     = {"", "d", "b", "B", "o", "x", "X"};

    const long long values[] = {
        0, 1, -1, 8, 9, 42, -42, 255, 256, -256, 1000000007, -98765432, LLONG_MAX, LLONG_MIN, LLONG_MAX - 1};

    for (const auto& fa : fillalign) {
        for (const auto& s : signs) {
            for (const auto& h : alts) {
                for (const auto& z : zeros) {
                    for (const auto& w : widths) {
                        for (const auto& t : types) {
                            const std::string spec = "{:" + fa + s + h + z + w + t + "}";
                            for (const long long v : values) {
                                ASSERT_TRUE(same_fmt(spec, v));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(Format, DifferentialOracleUnsigned) {
    const std::vector<std::string> fillalign = {"", "<", ">", "^", "*^"};
    const std::vector<std::string> signs     = {"", "+", " "};
    const std::vector<std::string> alts      = {"", "#"};
    const std::vector<std::string> zeros     = {"", "0"};
    const std::vector<std::string> widths    = {"", "1", "6", "20"};
    const std::vector<std::string> types     = {"", "d", "b", "B", "o", "x", "X"};

    const unsigned long long values[] = {0u, 1u, 8u, 255u, 256u, 1000000007u, ULLONG_MAX, ULLONG_MAX - 1u};

    for (const auto& fa : fillalign) {
        for (const auto& s : signs) {
            for (const auto& h : alts) {
                for (const auto& z : zeros) {
                    for (const auto& w : widths) {
                        for (const auto& t : types) {
                            const std::string spec = "{:" + fa + s + h + z + w + t + "}";
                            for (const unsigned long long v : values) {
                                ASSERT_TRUE(same_fmt(spec, v));
                            }
                        }
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------
// Dynamic width via nested replacement fields.
// ------------------------------------------------------------------------------------------
TEST(Format, DynamicWidth) {
    EXPECT_EQ(std::format("{:{}}", big_int{42}, 6), "    42");
    EXPECT_EQ(std::format("{:0{}x}", big_int{255}, 6), "0000ff");
    EXPECT_EQ(std::format("{:{}d}", big_int{-5}, 5), "   -5");
    EXPECT_EQ(std::format("{0:{1}}", big_int{7}, 4), "   7");
    EXPECT_EQ(std::format("{:<{}}", big_int{42}, 6), "42    ");

    // Negative width argument is ill-formed at runtime.
    {
        big_int   b   = big_int(7);
        const int neg = -3;
        EXPECT_THROW(vformat_call("{:{}}", std::make_format_args(b, neg)), std::format_error);
    }
    // Non-integer width argument is ill-formed.
    {
        big_int           b = big_int(7);
        const std::string s = "x";
        EXPECT_THROW(vformat_call("{:{}}", std::make_format_args(b, s)), std::format_error);
    }
}

// ------------------------------------------------------------------------------------------
// Ill-formed specs throw std::format_error, matching the builtin formatter's behavior.
// (std::format with a string literal is consteval-checked and would not compile, so we use
// std::vformat to exercise the runtime path.)
// ------------------------------------------------------------------------------------------
TEST(Format, IllFormedSpecs) {
    const char* bad[] = {
        "{:.3d}",
        "{:.3}",
        "{:.0d}",
        "{:.{}d}",
        "{:q}",
        "{:s}",
        "{:p}",
        "{:n}",
        "{:a}",
        "{:e}",
        "{:f}",
        "{:g}",
        "{:Z}",
        "{:11111111111111111111111d}",
    };
    for (const char* spec : bad) {
        EXPECT_TRUE(same_fmt(spec, 42LL)) << spec;
        EXPECT_TRUE(same_fmt(spec, 12345678901234567890ull)) << spec;
    }
    // '#' on decimal is NOT an error.
    big_int b{42};
    EXPECT_NO_THROW(vformat_call("{:#d}", std::make_format_args(b)));
}

// ------------------------------------------------------------------------------------------
// The fill is a character, not a code unit, so a UTF-8 format string may spell it in up to
// four bytes. Asserted directly rather than differentially: what a standard library makes of a
// fill wider than one code unit has varied across releases, so a differential check would test
// the platform library rather than us.
// ------------------------------------------------------------------------------------------
TEST(Format, MultiCodeUnitFill) {
    const std::string acute = "\xC3\xA9";         // U+00E9, two code units
    const std::string block = "\xE2\x96\x88";     // U+2588, three code units
    const std::string emoji = "\xF0\x9F\x98\x80"; // U+1F600, four code units

    big_int b{42};
    EXPECT_EQ(vformat_call("{:" + block + ">8}", std::make_format_args(b)), repeat(block, 6) + "42");
    EXPECT_EQ(vformat_call("{:" + block + "<8}", std::make_format_args(b)), "42" + repeat(block, 6));
    EXPECT_EQ(vformat_call("{:" + block + "^9}", std::make_format_args(b)),
              repeat(block, 3) + "42" + repeat(block, 4));
    EXPECT_EQ(vformat_call("{:" + acute + ">6}", std::make_format_args(b)), repeat(acute, 4) + "42");
    EXPECT_EQ(vformat_call("{:" + emoji + ">5}", std::make_format_args(b)), repeat(emoji, 3) + "42");

    // The sign and the base prefix sit between the fill and the digits, as with an ASCII fill.
    big_int neg{-42};
    EXPECT_EQ(vformat_call("{:" + block + ">6x}", std::make_format_args(neg)), repeat(block, 3) + "-2a");
    EXPECT_EQ(vformat_call("{:" + block + ">#8x}", std::make_format_args(b)), repeat(block, 4) + "0x2a");
}

TEST(Format, MultiCodeUnitFillOtherFields) {
    const std::string block = "\xE2\x96\x88";

    // A dynamic width, the 'c' type, and the localized path all pad with the same character.
    big_int   b{42};
    const int w = 6;
    EXPECT_EQ(vformat_call("{:" + block + ">{}}", std::make_format_args(b, w)), repeat(block, 4) + "42");

    big_int c{65};
    EXPECT_EQ(vformat_call("{:" + block + ">4c}", std::make_format_args(c)), repeat(block, 3) + "A");
    EXPECT_EQ(vformat_call("{:" + block + "^5c}", std::make_format_args(c)),
              repeat(block, 2) + "A" + repeat(block, 2));

    // Zero padding is not the fill: it stays a run of '0' characters.
    EXPECT_EQ(std::format("{:08d}", big_int{42}), "00000042");
    EXPECT_EQ(std::format("{:#08x}", big_int{255}), "0x0000ff");
}

TEST(Format, MalformedFillCharacter) {
    // Byte sequences that do not spell a character are rejected rather than taken a unit at a
    // time. Implementations disagree on a format string that is not valid UTF-8 (libc++ throws,
    // libstdc++ formats with replacement characters), so this is asserted directly.
    const char* bad[] = {
        "\x80",             // a lone continuation unit
        "\xE2\x96",         // a truncated three-unit sequence
        "\xC0\xAF",         // an overlong encoding of '/'
        "\xED\xA0\x80",     // a surrogate
        "\xF5\x80\x80\x80", // above U+10FFFF
    };
    big_int b{42};
    for (const char* fill : bad) {
        const std::string spec = std::string("{:") + fill + ">6}";
        EXPECT_THROW(vformat_call(spec, std::make_format_args(b)), std::format_error) << fill;
    }

    // A well-formed character that no alignment follows is not a fill, and is not a type.
    EXPECT_THROW(vformat_call("{:\xE2\x96\x88}", std::make_format_args(b)), std::format_error);
}

// ------------------------------------------------------------------------------------------
// Huge multi-limb values, where no 64-bit oracle exists: the digit body must equal the
// already-tested to_chars/to_string output, structure (sign/prefix/padding) must be correct,
// and the output must round-trip back through from_chars.
// ------------------------------------------------------------------------------------------
TEST(Format, HugeBodyMatchesToString) {
    big_int huge = 1234567890123456789012345678901234567890112233445566778899_n;

    EXPECT_EQ(std::format("{:b}", huge), to_string(huge, 2));
    EXPECT_EQ(std::format("{:o}", huge), to_string(huge, 8));
    EXPECT_EQ(std::format("{:d}", huge), to_string(huge, 10));
    EXPECT_EQ(std::format("{}", huge), to_string(huge, 10));
    EXPECT_EQ(std::format("{:x}", huge), to_string(huge, 16));
    EXPECT_EQ(std::format("{:X}", huge), to_upper(to_string(huge, 16)));
    EXPECT_EQ(std::format("{:B}", huge), to_upper(to_string(huge, 2)));

    // Negative magnitude carries the '-' sign.
    big_int neg = -huge;
    EXPECT_EQ(std::format("{}", neg), to_string(neg, 10));
    EXPECT_EQ(std::format("{:x}", neg), to_string(neg, 16));

    // Cross-check the decimal body against Boost.Multiprecision.
    boost::multiprecision::cpp_int ref("1234567890123456789012345678901234567890112233445566778899");
    EXPECT_EQ(std::format("{}", huge), ref.str());
}

TEST(Format, HugePrefixSignAndPadding) {
    big_int huge = 1_n << 600; // a clean multi-limb power of two
    big_int neg  = -huge;

    EXPECT_EQ(std::format("{:#x}", huge), "0x" + to_string(huge, 16));
    EXPECT_EQ(std::format("{:#x}", neg), "-0x" + to_string(huge, 16));
    EXPECT_EQ(std::format("{:+}", huge), "+" + to_string(huge, 10));

    const std::string body  = to_string(huge, 10);
    const std::size_t width = body.size() + 25;

    // Sign-aware zero padding: exactly width chars, all leading zeros then the body.
    {
        const std::string s = std::vformat("{:0" + std::to_string(width) + "}", std::make_format_args(huge));
        ASSERT_EQ(s.size(), width);
        const std::size_t pad = width - body.size();
        EXPECT_EQ(s.substr(0, pad), std::string(pad, '0'));
        EXPECT_EQ(s.substr(pad), body);
    }
    // Fill + right alignment: fill on the left.
    {
        const std::string s = std::vformat("{:*>" + std::to_string(width) + "}", std::make_format_args(huge));
        ASSERT_EQ(s.size(), width);
        const std::size_t pad = width - body.size();
        EXPECT_EQ(s.substr(0, pad), std::string(pad, '*'));
        EXPECT_EQ(s.substr(pad), body);
    }
    // Fill + left alignment: fill on the right.
    {
        const std::string s = std::vformat("{:*<" + std::to_string(width) + "}", std::make_format_args(huge));
        ASSERT_EQ(s.size(), width);
        EXPECT_EQ(s.substr(0, body.size()), body);
        EXPECT_EQ(s.substr(body.size()), std::string(width - body.size(), '*'));
    }
}

TEST(Format, HugeRoundTrip) {
    const big_int values[] = {
        1234567890123456789012345678901234567890112233445566778899_n,
        (1_n << 256) - 1_n,
        1_n << 257,
        -(1_n << 300),
        (1_n << 1000) + 12345_n,
    };
    for (const big_int& v : values) {
        for (const int base : {2, 8, 10, 16}) {
            const char* spec = base == 2    ? "{:*>2000b}"
                               : base == 8  ? "{:*>2000o}"
                               : base == 10 ? "{:*>2000}"
                                            : "{:*>2000x}";
            std::string s    = std::vformat(spec, std::make_format_args(v));
            // Strip the right-alignment fill and an optional leading sign.
            std::string_view  sv{s};
            const std::size_t first = sv.find_first_not_of('*');
            sv.remove_prefix(first);
            const std::size_t last = sv.find_last_not_of('*');
            sv                     = sv.substr(0, last + 1);
            bool negative          = !sv.empty() && sv.front() == '-';
            if (negative) {
                sv.remove_prefix(1);
            }
            big_int    parsed;
            const auto res = from_chars(sv.data(), sv.data() + sv.size(), parsed, base);
            ASSERT_EQ(res.ec, std::errc{});
            ASSERT_EQ(res.ptr, sv.data() + sv.size());
            if (negative) {
                parsed = -parsed;
            }
            EXPECT_EQ(parsed, v);
        }
    }
}

// ------------------------------------------------------------------------------------------
// The 'c' presentation type.
// ------------------------------------------------------------------------------------------
TEST(Format, CharType) {
    EXPECT_EQ(std::format("{:c}", big_int{65}), "A");
    EXPECT_EQ(std::format("{:5c}", big_int{65}), "A    "); // 'c' defaults to left alignment
    EXPECT_EQ(std::format("{:>5c}", big_int{65}), "    A");
    EXPECT_EQ(std::format("{:^5c}", big_int{65}), "  A  ");
    EXPECT_EQ(std::format("{:*^5c}", big_int{65}), "**A**");

    // Differential against the builtin for in-range code points.
    for (const long long v : {0LL, 1LL, 65LL, 90LL, 122LL, 127LL, -1LL}) {
        EXPECT_TRUE(same_fmt("{:c}", v));
        EXPECT_TRUE(same_fmt("{:4c}", v));
    }
}

TEST(Format, CharTypeErrors) {
    // Out of range for the target char type -> throw (matches builtin on this platform).
    EXPECT_TRUE(same_fmt("{:c}", 256LL));
    EXPECT_TRUE(same_fmt("{:c}", 100000LL));

    // A multi-limb value can never be a character.
    {
        big_int huge = 1_n << 200;
        EXPECT_THROW(vformat_call("{:c}", std::make_format_args(huge)), std::format_error);
    }
    // Modifiers forbidden with the 'c' type (LWG 3644: sign, '#', and '0' are ill-formed;
    // precision is ill-formed for any integer). Our formatter follows the adopted resolution
    // and throws. This is asserted directly rather than differentially because some standard
    // libraries (e.g. current libstdc++) have not yet implemented LWG 3644 and accept these,
    // so a differential check against the builtin would test the library's conformance, not ours.
    {
        big_int b{65};
        for (const char* spec : {"{:+c}", "{:-c}", "{: c}", "{:#c}", "{:0c}", "{:05c}", "{:.2c}"}) {
            EXPECT_THROW(vformat_call(spec, std::make_format_args(b)), std::format_error) << spec;
        }
    }
}

// ------------------------------------------------------------------------------------------
// The 'L' locale option.
// ------------------------------------------------------------------------------------------
TEST(Format, LocaleGrouping) {
    std::locale loc(std::locale::classic(), new comma_grouping);

    const long long values[] = {0, 9, 99, 999, 1000, 12345, 1234567, -1234, -1000000, 1000000007};
    const char*     specs[]  = {"{:L}", "{:Ld}", "{:Lx}", "{:LX}", "{:Lb}", "{:Lo}", "{:+L}", "{: L}"};
    for (const char* spec : specs) {
        for (const long long v : values) {
            EXPECT_TRUE(same_fmt(&loc, spec, v)) << spec;
        }
    }

    // The classic locale has empty grouping: L is a no-op.
    EXPECT_EQ(std::format(std::locale::classic(), "{:L}", big_int{1234567}), "1234567");
}

TEST(Format, LocaleZeroPadCombination) {
    // The interaction of '0' and 'L' is unspecified by the standard; assert parity with the
    // platform's own integer formatter (which is the only meaningful oracle here).
    std::locale loc(std::locale::classic(), new comma_grouping);

    const char* specs[] = {"{:08L}", "{:012L}", "{:+012L}", "{:#012Lx}", "{:020L}", "{:08Lx}"};
    for (const char* spec : specs) {
        for (const long long v : {0LL, 7LL, 1234LL, -1234LL, 1234567LL, 255LL}) {
            EXPECT_TRUE(same_fmt(&loc, spec, v)) << spec;
        }
    }
}

TEST(Format, LocaleGroupingHuge) {
    std::locale loc(std::locale::classic(), new comma_grouping);

    big_int     huge    = 1234567890123456789012345678901234567890112233445566778899_n;
    std::string grouped = add_commas(to_string(huge, 10));
    EXPECT_EQ(std::vformat(loc, "{:L}", std::make_format_args(huge)), grouped);

    big_int neg = -huge;
    EXPECT_EQ(std::vformat(loc, "{:L}", std::make_format_args(neg)), "-" + grouped);
}

// ------------------------------------------------------------------------------------------
// Wide-character (wchar_t / std::wformat) support.
// ------------------------------------------------------------------------------------------
TEST(Format, WideChar) {
    EXPECT_EQ(std::format(L"{}", big_int{255}), L"255");
    EXPECT_EQ(std::format(L"{:#06X}", big_int{255}), L"0X00FF");
    EXPECT_EQ(std::format(L"{:#06x}", big_int{255}), L"0x00ff");
    EXPECT_EQ(std::format(L"{:08b}", big_int{5}), L"00000101");
    EXPECT_EQ(std::format(L"{:+}", big_int{42}), L"+42");
    EXPECT_EQ(std::format(L"{:*^8}", big_int{42}), L"***42***");
    EXPECT_EQ(std::format(L"{:c}", big_int{0x41}), L"A");
    EXPECT_EQ(std::format(L"{:>5c}", big_int{0x41}), L"    A");
    EXPECT_EQ(std::format(L"{:{}}", big_int{42}, 6), L"    42");

    // A non-ASCII fill: one code unit where wchar_t is 32 bits, a surrogate pair where it is
    // 16, and in either case one character.
    {
        const std::wstring block = L"\u2588";
        const std::wstring emoji = L"\U0001F600";
        big_int            b{42};
        EXPECT_EQ(std::vformat(L"{:" + block + L">8}", std::make_wformat_args(b)), repeat(block, 6) + L"42");
        EXPECT_EQ(std::vformat(L"{:" + emoji + L"^7}", std::make_wformat_args(b)),
                  repeat(emoji, 2) + L"42" + repeat(emoji, 3));
    }

    // Huge value in wide form equals the widened narrow body.
    big_int      huge   = 1_n << 400;
    std::string  narrow = to_string(huge, 16);
    std::wstring wide(narrow.begin(), narrow.end());
    EXPECT_EQ(std::format(L"{:x}", huge), wide);
}

} // namespace

#else

TEST(Format, SkippedNoFormatSupport) { GTEST_SKIP() << "<format> is not available in this configuration."; }

#endif
