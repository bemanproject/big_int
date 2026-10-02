// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// The standard-library boundary: std::hash, std::numeric_limits and std::formatter
// are partial specializations of templates owned by <functional>/<limits>/<format>,
// declared inside the module purview and attached to the global module by the
// `extern "C++"` block in big_int.cppm. What makes a specialization visible to an importer is reachability
// rather than name lookup ([module.reach]), so this file is the regression test that
// the compiler actually finds them from outside the module, plus the standard
// concepts and algorithms that lean on the operators the other module tests already
// cover.
//
// `std::formatter<T, charT>`'s *primary* template has deleted special member
// functions, precisely so that is_default_constructible_v<std::formatter<T, charT>>
// is false for a type with no formatter. That makes the check itself the test: it
// passes only because the library's partial specialization was found.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <version>

// Hoisted out of the std::formatter section below so that every standard header
// this translation unit uses is included before the import.
#if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
    #include <format>
#endif

#include <gtest/gtest.h>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::to_string;

// ============================================================================
// std::hash
// ============================================================================

// The digest is computed noexcept, matching the header's declaration.
static_assert(noexcept(std::hash<big_int>{}(std::declval<const big_int&>())));
static_assert(std::is_nothrow_invocable_r_v<std::size_t, std::hash<big_int>, const big_int&>);

TEST(StdIntegration, HashIsValueBasedNotStorageBased) {
    const std::hash<big_int> h;
    const big_int            in_place{12345};
    big_int                  heap{12345};

    const auto cap_before = heap.representation_capacity();
    heap.reserve(4096); // bits; far past inplace_bits, so this forces a heap allocation
    ASSERT_GT(heap.representation_capacity(), cap_before);

    EXPECT_EQ(in_place, heap);       // still the same value...
    EXPECT_EQ(h(in_place), h(heap)); // ...and therefore the same digest.
}

TEST(StdIntegration, HashAgreesAcrossSpecializations) {
    const big_int value = (big_int{1} << 100) + big_int{7}; // wider than 64 bits

    const BEMAN_BIG_INT_NAMESPACE::pmr::big_int pmr_value(value);
    const basic_big_int<512>                    wide_value(value);

    const auto h_default = std::hash<big_int>{}(value);
    const auto h_pmr     = std::hash<BEMAN_BIG_INT_NAMESPACE::pmr::big_int>{}(pmr_value);
    const auto h_wide    = std::hash<basic_big_int<512>>{}(wide_value);

    EXPECT_EQ(h_default, h_pmr);
    EXPECT_EQ(h_default, h_wide);
}

TEST(StdIntegration, UnorderedSetWithNegativeAndWideKeys) {
    std::unordered_set<big_int> s;
    const big_int               wide = (big_int{1} << 200) + big_int{3};
    s.insert(big_int{-5});
    s.insert(big_int{5});
    s.insert(wide);
    s.insert(-wide);
    s.insert(big_int{-5}); // duplicate

    EXPECT_EQ(s.size(), 4U);
    EXPECT_TRUE(s.contains(big_int{-5}));
    EXPECT_TRUE(s.contains(wide));
    EXPECT_TRUE(s.contains(-wide));
    EXPECT_FALSE(s.contains(big_int{0}));
}

TEST(StdIntegration, UnorderedMapWithNegativeAndWideKeys) {
    std::unordered_map<big_int, int> m;
    const big_int                    wide = big_int{1} << 128;
    m[big_int{-1}]                        = 1;
    m[wide]                               = 2;
    m[big_int{-1}]                        = 11; // overwrite

    EXPECT_EQ(m.size(), 2U);
    EXPECT_EQ(m.at(big_int{-1}), 11);
    EXPECT_EQ(m.at(wide), 2);
    EXPECT_EQ(m.count(big_int{0}), 0U);
}

// ============================================================================
// Standard concepts and algorithms riding on the operators
// ============================================================================

static_assert(std::regular<big_int>);
static_assert(std::totally_ordered<big_int>);
static_assert(std::three_way_comparable<big_int, std::strong_ordering>);
static_assert(std::equality_comparable_with<big_int, int>);
static_assert(std::swappable<big_int>);

TEST(StdIntegration, RangesSwapFindsLibrarySwapByAdl) {
    big_int a{1};
    big_int b{2};
    std::ranges::swap(a, b); // resolves via the exported `swap` free function, not std::swap
    EXPECT_EQ(a, big_int{2});
    EXPECT_EQ(b, big_int{1});
}

TEST(StdIntegration, VectorElementAndRangesSort) {
    std::vector<big_int> v{big_int{5}, big_int{-3}, big_int{100}, big_int{0}, -(big_int{1} << 80)};
    std::ranges::sort(v);
    const std::vector<big_int> expected{-(big_int{1} << 80), big_int{-3}, big_int{0}, big_int{5}, big_int{100}};
    EXPECT_EQ(v, expected);
}

// ============================================================================
// std::numeric_limits
// ============================================================================
//
// A third partial specialization of a std template declared in the module purview,
// found from outside the module by reachability rather than by name lookup. The
// values it reports are the subject of limits.test.cpp; what matters here is that an
// importer sees the specialization at all, rather than the unspecialized primary.

static_assert(std::numeric_limits<big_int>::is_specialized);
static_assert(std::numeric_limits<basic_big_int<512>>::is_specialized);

TEST(StdIntegration, NumericLimitsIsVisibleThroughTheModule) {
    // The primary template reports is_specialized == false and digits == 0, so both
    // checks fail if the importer falls back to it.
    EXPECT_TRUE(std::numeric_limits<big_int>::is_specialized);
    EXPECT_EQ(std::numeric_limits<big_int>::digits, (std::numeric_limits<int>::max)());
    EXPECT_FALSE(std::numeric_limits<big_int>::is_bounded);
    EXPECT_EQ((std::numeric_limits<big_int>::max)(), big_int{0});
}

} // namespace

// ============================================================================
// std::formatter
// ============================================================================

#if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
namespace {

// std::vformat is [[nodiscard]] and this target builds with -Werror; these
// non-nodiscard wrappers let the throwing tests below call it without tripping
// -Werror=unused-result.
std::string  vformat_call(const std::string_view fmt, const std::format_args args) { return std::vformat(fmt, args); }
std::wstring vformat_call(const std::wstring_view fmt, const std::wformat_args args) {
    return std::vformat(fmt, args);
}

} // namespace

// The primary std::formatter is not default-constructible (its special members are
// deleted); passing this is only possible because the library's specialization for
// basic_big_int was found, for both character types and more than one width.
static_assert(std::is_default_constructible_v<std::formatter<big_int, char>>);
static_assert(std::is_default_constructible_v<std::formatter<big_int, wchar_t>>);
static_assert(std::is_default_constructible_v<std::formatter<basic_big_int<512>, char>>);
static_assert(std::is_copy_constructible_v<std::formatter<big_int, char>>);
static_assert(std::is_move_constructible_v<std::formatter<big_int, char>>);

TEST(StdIntegration, FormatBasesAndAlternateForms) {
    const big_int x{255};
    EXPECT_EQ(std::format("{:d}", x), "255");
    EXPECT_EQ(std::format("{:x}", x), "ff");
    EXPECT_EQ(std::format("{:X}", x), "FF");
    EXPECT_EQ(std::format("{:b}", x), "11111111");
    EXPECT_EQ(std::format("{:o}", x), "377");
    EXPECT_EQ(std::format("{:#x}", x), "0xff");
    EXPECT_EQ(std::format("{:#X}", x), "0XFF");
    EXPECT_EQ(std::format("{:#b}", x), "0b11111111");
    EXPECT_EQ(std::format("{:#o}", x), "0377");
}

TEST(StdIntegration, FormatSignFillAlignAndZeroPad) {
    EXPECT_EQ(std::format("{:+}", big_int{42}), "+42");
    EXPECT_EQ(std::format("{: }", big_int{42}), " 42");
    EXPECT_EQ(std::format("{:<8}", big_int{42}), "42      ");
    EXPECT_EQ(std::format("{:>8}", big_int{42}), "      42");
    EXPECT_EQ(std::format("{:^8}", big_int{42}), "   42   ");
    EXPECT_EQ(std::format("{:*^8}", big_int{42}), "***42***");
    // Sign-aware zero padding on a negative value: the zeros land between the sign
    // and the digits, not to the left of the sign.
    EXPECT_EQ(std::format("{:08}", big_int{-42}), "-0000042");
}

TEST(StdIntegration, FormatDynamicWidth) {
    EXPECT_EQ(std::format("{:{}}", big_int{42}, 6), "    42");
    EXPECT_EQ(std::format("{:0{}x}", big_int{255}, 6), "0000ff");
}

TEST(StdIntegration, FormatValueWiderThan64Bits) {
    const big_int huge = (big_int{1} << 128) - big_int{1};
    EXPECT_EQ(std::format("{}", huge), to_string(huge));
    EXPECT_EQ(std::format("{:x}", huge), to_string(huge, 16));
    EXPECT_EQ(std::format("{:#x}", -huge), "-0x" + to_string(huge, 16));
}

TEST(StdIntegration, WideCharFormatting) {
    EXPECT_EQ(std::format(L"{}", big_int{255}), L"255");
    EXPECT_EQ(std::format(L"{:#06x}", big_int{255}), L"0x00ff");
    EXPECT_EQ(std::format(L"{:+}", big_int{42}), L"+42");

    const big_int      huge   = big_int{1} << 128;
    const std::string  narrow = to_string(huge, 16);
    const std::wstring wide(narrow.begin(), narrow.end());
    EXPECT_EQ(std::format(L"{:x}", huge), wide);
}

TEST(StdIntegration, FormatToBackInsertIterator) {
    std::string out;
    std::format_to(std::back_inserter(out), "{:#06x}", big_int{255});
    EXPECT_EQ(out, "0x00ff");
}

TEST(StdIntegration, VformatViaMakeFormatArgs) {
    // A different code path from directly instantiating the formatter: the format
    // arguments are type-erased through std::format_args first.
    const big_int x{42};
    EXPECT_EQ(vformat_call("{:x}", std::make_format_args(x)), "2a");
}

TEST(StdIntegration, FormatErrorIsCaughtAsImportersOwnType) {
    // The consteval format-string check would reject an ill-formed spec at compile
    // time for a string-literal call, so the runtime path (vformat) is used to reach
    // the throw. Precision is not a valid option for an integer presentation type.
    const big_int x{42};
    EXPECT_THROW(vformat_call("{:.3d}", std::make_format_args(x)), std::format_error);
    EXPECT_THROW(vformat_call(L"{:.3d}", std::make_wformat_args(x)), std::format_error);
}

#else

TEST(StdIntegration, FormatSkippedNoFormatSupport) {
    GTEST_SKIP() << "<format> is not available in this configuration.";
}

#endif // __has_include(<format>) && __cpp_lib_format >= 201907L
