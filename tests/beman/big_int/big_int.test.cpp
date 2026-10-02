// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

BEMAN_BIG_INT_BEGIN_NAMESPACE

static_assert(detail::width_v<detail::wider_t<std::int8_t>> == 16);
static_assert(detail::width_v<detail::wider_t<std::int16_t>> == 32);
static_assert(detail::width_v<detail::wider_t<std::int32_t>> == 64);

static_assert(detail::width_v<detail::wider_t<std::uint8_t>> == 16);
static_assert(detail::width_v<detail::wider_t<std::uint16_t>> == 32);
static_assert(detail::width_v<detail::wider_t<std::uint32_t>> == 64);

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT
// The relationship between int_multiprecision_t and int_wide_t should be exact,
// not just based on width.
static_assert(std::is_same_v<detail::wider_t<detail::int_multiprecision_t>, detail::int_wide_t>);
static_assert(std::is_same_v<detail::wider_t<uint_multiprecision_t>, detail::uint_wide_t>);
#endif

#ifdef BEMAN_BIG_INT_HAS_INT128
static_assert(std::is_same_v<detail::wider_t<std::int64_t>, detail::int128_t>);
static_assert(std::is_same_v<detail::wider_t<std::uint64_t>, detail::uint128_t>);
#endif

#ifdef BEMAN_BIG_INT_HAS_BITINT
// wider_t should return a _BitInt when given a _BitInt.
static_assert(std::is_same_v<detail::wider_t<bit_int<32>>, bit_int<64>>);
static_assert(std::is_same_v<detail::wider_t<bit_uint<32>>, bit_uint<64>>);
#endif

template class basic_big_int<big_int::inplace_bits, uint_multiprecision_t, big_int::allocator_type>;

static_assert(basic_big_int<128, uint_multiprecision_t, big_int::allocator_type>::inplace_bits == 128);
static_assert(basic_big_int<127, uint_multiprecision_t, big_int::allocator_type>::inplace_bits == 128,
              "inplace_bits was expected to be rounded to the next multiple of 32.");

template <class T>
struct is_exact_big_int;

template <std::size_t b, class L, class A>
struct is_exact_big_int<basic_big_int<b, L, A>> : std::bool_constant<b == basic_big_int<b, L, A>::inplace_bits> {};

static_assert(is_exact_big_int<big_int>::value,
              "The min_inplace_bits should match inplace_bits exactly for big_int, "
              "though it can differ for other specializations.");
static_assert(sizeof(void*) != 8 || sizeof(big_int) == 16, "On 64-bit, big_int must be two pointers large.");

static_assert(detail::common_big_int_type_with<big_int, big_int>);
static_assert(detail::common_big_int_type_with<big_int, int>);
static_assert(detail::common_big_int_type_with<big_int, unsigned>);
static_assert(
    !detail::common_big_int_type_with<big_int,
                                      basic_big_int<1, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>>,
    "There must be no common type for mixed allocators.");
static_assert(!detail::common_big_int_type_with<big_int, float>,
              "There must be no common type between big_int and floating-point types.");
static_assert(!detail::common_big_int_type_with<int, int>,
              "There must be no common big_int type between two fundamental types.");

// The limb type is the second template parameter and defaults to uint_multiprecision_t, and the
// allocator defaults to std::allocator<Limb>, so the explicit spelling must name the same type
// as the abbreviated one.
static_assert(
    std::is_same_v<big_int, basic_big_int<64, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>>);
static_assert(std::is_same_v<basic_big_int<256>,
                             basic_big_int<256, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>>);
static_assert(std::is_same_v<pmr::big_int,
                             basic_big_int<big_int::inplace_bits,
                                           uint_multiprecision_t,
                                           std::pmr::polymorphic_allocator<uint_multiprecision_t>>>);
static_assert(std::is_same_v<pmr::basic_big_int<256>, pmr::basic_big_int<256, uint_multiprecision_t>>);
static_assert(
    detail::is_basic_big_int_v<basic_big_int<128, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>>);
static_assert(std::is_same_v<detail::limb_type_of_t<big_int>, uint_multiprecision_t>);

using namespace BEMAN_BIG_INT_NAMESPACE::literals;

static_assert(0_n == 0_n);
static_assert(1_n != 0_n);
static_assert(0xff_n == 255_n);
static_assert(0XFF_n == 255_n);
static_assert(0b1111_n == 15_n);
static_assert(0B1111_n == 15_n);

static_assert(1000_n == 1'0'00_n);
static_assert(1'000'000'000'000'000'000'000'000'000_n == 0x33b'2e3c'9fd0'803c'e800'0000_n);

BEMAN_BIG_INT_END_NAMESPACE

// Exercise the fully explicit three-argument spelling, so the limb parameter is instantiated
// through real arithmetic rather than only named in a type comparison.
TEST(BigIntType, ExplicitLimbTypeArgument) {
    using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
    using explicit_big_int =
        BEMAN_BIG_INT_NAMESPACE::basic_big_int<256, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>;

    explicit_big_int x{1};
    for (int i = 0; i < 40; ++i) {
        x *= 3;
    }

    EXPECT_EQ(to_string(x), "12157665459056928801"); // 3^40
    EXPECT_EQ(x % explicit_big_int{3}, explicit_big_int{0});
    EXPECT_EQ((x / explicit_big_int{3}) * explicit_big_int{3}, x);
    EXPECT_EQ(-x + x, explicit_big_int{0});

    // The default spelling names the same type, so hashing must agree with it.
    EXPECT_EQ(std::hash<explicit_big_int>{}(x), std::hash<BEMAN_BIG_INT_NAMESPACE::basic_big_int<256>>{}(x));
}
