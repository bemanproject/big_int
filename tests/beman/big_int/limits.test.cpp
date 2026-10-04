// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// std::numeric_limits for basic_big_int. The type is unbounded, so the specialization
// reports no extreme value; every member here is cross-checked against what
// std::numeric_limits reports for an unbounded boost::multiprecision::cpp_int, which
// is the established answer for a heap-backed arbitrary-precision integer.
//
// std::denorm_absent and std::float_denorm_style are deprecated in C++23, so this file
// never names them: has_denorm is checked against Boost's value and against an integer
// comparison instead.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <type_traits>

#include <beman/big_int/big_int.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <gtest/gtest.h>

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

using pmr_big_int  = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;
using wide_big_int = basic_big_int<512>;

using lim       = std::numeric_limits<big_int>;
using pmr_lim   = std::numeric_limits<pmr_big_int>;
using wide_lim  = std::numeric_limits<wide_big_int>;
using boost_lim = std::numeric_limits<boost::multiprecision::cpp_int>;

// ----- compile-time tests -----

// The specialization exists at all. The primary template reports false, so every
// assertion below rests on this one.
static_assert(lim::is_specialized);
static_assert(pmr_lim::is_specialized);
static_assert(wide_lim::is_specialized);

// <limits> supplies the cv-qualified forwarders; they reach our specialization only
// because it is a specialization of the template they forward to.
static_assert(std::numeric_limits<const big_int>::is_specialized);
static_assert(std::numeric_limits<volatile big_int>::is_specialized);
static_assert(std::numeric_limits<const volatile big_int>::is_specialized);

// An exact, signed, radix-2 integer type.
static_assert(lim::is_signed);
static_assert(lim::is_integer);
static_assert(lim::is_exact);
static_assert(lim::radix == 2);

// Unbounded, and therefore not modular: an operation that would overflow a fixed-width
// type grows the representation instead of wrapping.
static_assert(!lim::is_bounded);
static_assert(!lim::is_modulo);

// Nothing floating-point applies.
static_assert(!lim::has_infinity);
static_assert(!lim::has_quiet_NaN);
static_assert(!lim::has_signaling_NaN);
static_assert(!lim::has_denorm_loss);
static_assert(!lim::is_iec559);
static_assert(!lim::traps);
static_assert(!lim::tinyness_before);
static_assert(lim::min_exponent == 0 && lim::min_exponent10 == 0);
static_assert(lim::max_exponent == 0 && lim::max_exponent10 == 0);
static_assert(lim::round_style == std::round_toward_zero);

// digits saturates at INT_MAX; the two decimal counts are derived from it.
static_assert(lim::digits == (std::numeric_limits<int>::max)());
static_assert(lim::digits10 == static_cast<int>(0.30102999566398119521 * static_cast<double>(lim::digits - 1)));
static_assert(lim::max_digits10 == static_cast<int>(0.30102999566398119521 * static_cast<double>(lim::digits)) + 2);
static_assert(lim::digits10 < lim::max_digits10);

// The constants do not depend on the inline capacity or the allocator.
static_assert(lim::digits == pmr_lim::digits && lim::digits == wide_lim::digits);
static_assert(lim::digits10 == pmr_lim::digits10 && lim::digits10 == wide_lim::digits10);
static_assert(lim::max_digits10 == pmr_lim::max_digits10 && lim::max_digits10 == wide_lim::max_digits10);
static_assert(lim::is_bounded == wide_lim::is_bounded && lim::is_signed == wide_lim::is_signed);

// Every value-returning member yields zero, and does so in a constant expression.
static_assert((lim::min)() == 0);
static_assert((lim::max)() == 0);
static_assert(lim::lowest() == 0);
static_assert(lim::epsilon() == 0);
static_assert(lim::round_error() == 0);
static_assert(lim::infinity() == 0);
static_assert(lim::quiet_NaN() == 0);
static_assert(lim::signaling_NaN() == 0);
static_assert(lim::denorm_min() == 0);
static_assert(lim::lowest() == (lim::min)());

// They return the type itself, not a reference or a built-in.
static_assert(std::is_same_v<decltype((lim::max)()), big_int>);
static_assert(std::is_same_v<decltype((pmr_lim::max)()), pmr_big_int>);
static_assert(std::is_same_v<decltype(wide_lim::lowest()), wide_big_int>);

// With a default-constructible-without-throwing allocator, every accessor is noexcept.
static_assert(noexcept((lim::min)()));
static_assert(noexcept((lim::max)()));
static_assert(noexcept(lim::lowest()));
static_assert(noexcept(lim::epsilon()));
static_assert(noexcept(lim::round_error()));
static_assert(noexcept(lim::infinity()));
static_assert(noexcept(lim::quiet_NaN()));
static_assert(noexcept(lim::signaling_NaN()));
static_assert(noexcept(lim::denorm_min()));
static_assert(noexcept((pmr_lim::max)()));

// An allocator whose default constructor may throw: the accessors are exactly as
// noexcept as the default constructor they call, and no more.
template <class T>
struct throwing_default_allocator {
    using value_type = T;

    constexpr throwing_default_allocator() {} // deliberately not noexcept

    template <class U>
    constexpr throwing_default_allocator(const throwing_default_allocator<U>&) noexcept {}

    constexpr T*   allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    constexpr void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }

    constexpr bool operator==(const throwing_default_allocator&) const noexcept { return true; }
};

using fussy_big_int = basic_big_int<64, uint_multiprecision_t, throwing_default_allocator<uint_multiprecision_t>>;
using fussy_lim     = std::numeric_limits<fussy_big_int>;

static_assert(fussy_lim::is_specialized);
static_assert(!std::is_nothrow_default_constructible_v<fussy_big_int>);
static_assert(!noexcept((fussy_lim::max)()));
static_assert((fussy_lim::max)() == 0);

// Generic code that dispatches on numeric_limits sees an integer type, which is the
// point of specializing at all.
template <class T>
concept exact_integer =
    std::numeric_limits<T>::is_specialized && std::numeric_limits<T>::is_integer && std::numeric_limits<T>::is_exact;

static_assert(exact_integer<big_int>);
static_assert(exact_integer<pmr_big_int>);
static_assert(exact_integer<int>);
static_assert(!exact_integer<double>);

// Unlike every built-in integer, big_int reports no bound. A bound-seeking algorithm
// must branch on is_bounded rather than blindly calling max().
static_assert(std::numeric_limits<int>::is_bounded && std::numeric_limits<unsigned>::is_bounded);
static_assert(!lim::is_bounded);

// ----- runtime tests: parity with boost::multiprecision::cpp_int -----

TEST(Limits, ClassificationMatchesCppInt) {
    EXPECT_EQ(lim::is_specialized, boost_lim::is_specialized);
    EXPECT_EQ(lim::is_signed, boost_lim::is_signed);
    EXPECT_EQ(lim::is_integer, boost_lim::is_integer);
    EXPECT_EQ(lim::is_exact, boost_lim::is_exact);
    EXPECT_EQ(lim::radix, boost_lim::radix);
    EXPECT_EQ(lim::is_bounded, boost_lim::is_bounded);
    EXPECT_EQ(lim::is_modulo, boost_lim::is_modulo);
    EXPECT_EQ(lim::is_iec559, boost_lim::is_iec559);
    EXPECT_EQ(lim::traps, boost_lim::traps);
    EXPECT_EQ(lim::tinyness_before, boost_lim::tinyness_before);
    EXPECT_EQ(lim::round_style, boost_lim::round_style);
}

TEST(Limits, DigitCountsMatchCppInt) {
    EXPECT_EQ(lim::digits, boost_lim::digits);
    EXPECT_EQ(lim::digits10, boost_lim::digits10);
    EXPECT_EQ(lim::max_digits10, boost_lim::max_digits10);

    // The values the shared formula produces for digits == INT_MAX, pinned so a change
    // in how they are computed is visible rather than silent.
    if constexpr (lim::digits == 2147483647) {
        EXPECT_EQ(lim::digits10, 646456992);
        EXPECT_EQ(lim::max_digits10, 646456994);
    }
}

TEST(Limits, FloatingPointTraitsMatchCppInt) {
    EXPECT_EQ(lim::min_exponent, boost_lim::min_exponent);
    EXPECT_EQ(lim::min_exponent10, boost_lim::min_exponent10);
    EXPECT_EQ(lim::max_exponent, boost_lim::max_exponent);
    EXPECT_EQ(lim::max_exponent10, boost_lim::max_exponent10);
    EXPECT_EQ(lim::has_infinity, boost_lim::has_infinity);
    EXPECT_EQ(lim::has_quiet_NaN, boost_lim::has_quiet_NaN);
    EXPECT_EQ(lim::has_signaling_NaN, boost_lim::has_signaling_NaN);
    EXPECT_EQ(lim::has_denorm_loss, boost_lim::has_denorm_loss);

    // has_denorm: compared as an integer so the deprecated enumerator is never named.
    EXPECT_EQ(static_cast<int>(lim::has_denorm), static_cast<int>(boost_lim::has_denorm));
    EXPECT_EQ(static_cast<int>(lim::has_denorm), 0); // denorm_absent
}

TEST(Limits, EveryValueIsZeroJustAsForCppInt) {
    EXPECT_EQ((lim::min)(), big_int{0});
    EXPECT_EQ((lim::max)(), big_int{0});
    EXPECT_EQ(lim::lowest(), big_int{0});
    EXPECT_EQ(lim::epsilon(), big_int{0});
    EXPECT_EQ(lim::round_error(), big_int{0});
    EXPECT_EQ(lim::infinity(), big_int{0});
    EXPECT_EQ(lim::quiet_NaN(), big_int{0});
    EXPECT_EQ(lim::signaling_NaN(), big_int{0});
    EXPECT_EQ(lim::denorm_min(), big_int{0});

    EXPECT_EQ((boost_lim::min)(), 0);
    EXPECT_EQ((boost_lim::max)(), 0);
    EXPECT_EQ(boost_lim::lowest(), 0);
}

// The reported absence of a bound is not a lie by omission: the type really does hold
// values far past where any fixed-width integer stops.
TEST(Limits, UnboundedInPractice) {
    big_int x{1};
    x <<= 4096;
    EXPECT_EQ(x.size(), 4097U);
    EXPECT_GT(x, big_int{(std::numeric_limits<std::uint64_t>::max)()});
    EXPECT_GT(x, (lim::max)());

    // A value above the reported max() is exactly what an unbounded type permits.
    EXPECT_FALSE(lim::is_bounded);
}

TEST(Limits, AllocatorDoesNotChangeWhatIsReported) {
    EXPECT_EQ(pmr_lim::digits, lim::digits);
    EXPECT_EQ(pmr_lim::digits10, lim::digits10);
    EXPECT_EQ(pmr_lim::is_bounded, lim::is_bounded);

    std::pmr::monotonic_buffer_resource pool;
    pmr_big_int                         from_pool{7, &pool};
    EXPECT_GT(from_pool, (pmr_lim::max)());
    EXPECT_EQ((pmr_lim::max)(), pmr_big_int{0});
}

TEST(Limits, WidthOfInlineStorageDoesNotChangeWhatIsReported) {
    EXPECT_EQ(wide_lim::digits, lim::digits);
    EXPECT_EQ(wide_lim::max_digits10, lim::max_digits10);
    EXPECT_EQ(wide_lim::is_bounded, lim::is_bounded);
    EXPECT_EQ(wide_lim::lowest(), wide_big_int{0});
}

} // namespace
