// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/wide_ops.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using BEMAN_BIG_INT_NAMESPACE::detail::int_multiprecision_t;

TEST(WideOps, WideFromToIntUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;
    using BEMAN_BIG_INT_NAMESPACE::detail::wider_t;

    constexpr wider_t<uint_multiprecision_t> x1 = (static_cast<wider_t<uint_multiprecision_t>>(1ull) << 64) | 9ull;
    constexpr wider_t<uint_multiprecision_t> x2 =
        (static_cast<wider_t<uint_multiprecision_t>>(0xA5ull) << 64) | 0x5Aull;
    constexpr wider_t<uint_multiprecision_t> x3 = 0;
    constexpr wider_t<uint_multiprecision_t> x4 =
        static_cast<wider_t<uint_multiprecision_t>>(std::numeric_limits<uint_multiprecision_t>::max());

    constexpr auto w1 = wide<uint_multiprecision_t>::from_int(x1);
    constexpr auto w2 = wide<uint_multiprecision_t>::from_int(x2);
    constexpr auto w3 = wide<uint_multiprecision_t>::from_int(x3);
    constexpr auto w4 = wide<uint_multiprecision_t>::from_int(x4);

    EXPECT_EQ(w1.low_bits, 9ull);
    EXPECT_EQ(w1.high_bits, 1ull);
    EXPECT_EQ(w1.to_int(), x1);
    EXPECT_EQ(w2.low_bits, 0x5Aull);
    EXPECT_EQ(w2.high_bits, 0xA5ull);
    EXPECT_EQ(w2.to_int(), x2);
    EXPECT_EQ(w3.low_bits, 0ull);
    EXPECT_EQ(w3.high_bits, 0ull);
    EXPECT_EQ(w3.to_int(), x3);
    EXPECT_EQ(w4.low_bits, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(w4.high_bits, 0ull);
    EXPECT_EQ(w4.to_int(), x4);
}

TEST(WideOps, WideFromToIntInt) {
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;
    using BEMAN_BIG_INT_NAMESPACE::detail::wider_t;

    constexpr wider_t<int_multiprecision_t> x1 = static_cast<wider_t<int_multiprecision_t>>(-1);
    constexpr wider_t<int_multiprecision_t> x2 = static_cast<wider_t<int_multiprecision_t>>(1);
    constexpr wider_t<int_multiprecision_t> x3 = (static_cast<wider_t<int_multiprecision_t>>(-2) << 64) | 7;
    constexpr wider_t<int_multiprecision_t> x4 = 0;

    constexpr auto w1 = wide<int_multiprecision_t>::from_int(x1);
    constexpr auto w2 = wide<int_multiprecision_t>::from_int(x2);
    constexpr auto w3 = wide<int_multiprecision_t>::from_int(x3);
    constexpr auto w4 = wide<int_multiprecision_t>::from_int(x4);

    EXPECT_EQ(w1.low_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(w1.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(w1.to_int(), x1);
    EXPECT_EQ(w2.low_bits, static_cast<int_multiprecision_t>(1));
    EXPECT_EQ(w2.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(w2.to_int(), x2);
    EXPECT_EQ(w3.low_bits, static_cast<int_multiprecision_t>(7));
    EXPECT_EQ(w3.high_bits, static_cast<int_multiprecision_t>(-2));
    EXPECT_EQ(w3.to_int(), x3);
    EXPECT_EQ(w4.low_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(w4.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(w4.to_int(), x4);
}

TEST(WideOps, WideEquality) {
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    constexpr wide<uint_multiprecision_t> a{.low_bits = 1ull, .high_bits = 2ull};
    constexpr wide<uint_multiprecision_t> b{.low_bits = 1ull, .high_bits = 2ull};
    constexpr wide<uint_multiprecision_t> c{.low_bits = 2ull, .high_bits = 2ull};
    constexpr wide<uint_multiprecision_t> d{.low_bits = 1ull, .high_bits = 3ull};

    EXPECT_EQ(a, b);
    EXPECT_EQ(b, a);
    EXPECT_NE(a, c);
    EXPECT_NE(c, a);
    EXPECT_NE(a, d);
    EXPECT_NE(d, a);
    EXPECT_NE(b, c);
    EXPECT_NE(c, b);
    EXPECT_NE(b, d);
    EXPECT_NE(d, b);
    EXPECT_NE(c, d);
    EXPECT_NE(d, c);
}

TEST(WideOps, WideningMulInt) {
    using BEMAN_BIG_INT_NAMESPACE::detail::widening_mul;

    const auto r1 = widening_mul<int_multiprecision_t>(0, 0);
    const auto r2 = widening_mul<int_multiprecision_t>(1, 1);
    const auto r3 = widening_mul<int_multiprecision_t>(-1, 1);
    const auto r4 = widening_mul<int_multiprecision_t>(-3, 5);
    const auto r5 = widening_mul<int_multiprecision_t>(7, -8);
    const auto r6 = widening_mul<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), 2);

    EXPECT_EQ(r1.low_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r1.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r2.low_bits, static_cast<int_multiprecision_t>(1));
    EXPECT_EQ(r2.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r3.low_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r3.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r4.low_bits, static_cast<int_multiprecision_t>(-15));
    EXPECT_EQ(r4.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r5.low_bits, static_cast<int_multiprecision_t>(-56));
    EXPECT_EQ(r5.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r6.low_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r6.high_bits, static_cast<int_multiprecision_t>(-1));
}

TEST(WideOps, WideningMulUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::widening_mul;

    const auto r1 = widening_mul<uint_multiprecision_t>(0ull, 0ull);
    const auto r2 = widening_mul<uint_multiprecision_t>(1ull, 1ull);
    const auto r3 = widening_mul<uint_multiprecision_t>(3ull, 7ull);
    const auto r4 = widening_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 2ull);
    const auto r5 = widening_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(),
                                                        std::numeric_limits<uint_multiprecision_t>::max());
    const auto r6 = widening_mul<uint_multiprecision_t>(1ull << 32, 1ull << 32);

    EXPECT_EQ(r1.low_bits, 0ull);
    EXPECT_EQ(r1.high_bits, 0ull);
    EXPECT_EQ(r2.low_bits, 1ull);
    EXPECT_EQ(r2.high_bits, 0ull);
    EXPECT_EQ(r3.low_bits, 21ull);
    EXPECT_EQ(r3.high_bits, 0ull);
    EXPECT_EQ(r4.low_bits, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_EQ(r4.high_bits, 1ull);
    EXPECT_EQ(r5.low_bits, 1ull);
    EXPECT_EQ(r5.high_bits, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_EQ(r6.low_bits, 0ull);
    EXPECT_EQ(r6.high_bits, 1ull);
}

TEST(WideOps, FunnelShlInt) {
    using BEMAN_BIG_INT_NAMESPACE::detail::funnel_shl;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    constexpr wide<int_multiprecision_t> x{
        .low_bits  = static_cast<int_multiprecision_t>(0x0102'0304'0506'0708ull),
        .high_bits = 0,
    };

    EXPECT_EQ(funnel_shl(x, 0), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 1), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 2), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 3), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 4), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 5), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 6), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 7), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 8), static_cast<int_multiprecision_t>(0x0000'0000'0000'0001ull));
    EXPECT_EQ(funnel_shl(x, 9), static_cast<int_multiprecision_t>(0x0000'0000'0000'0002ull));
    EXPECT_EQ(funnel_shl(x, 10), static_cast<int_multiprecision_t>(0x0000'0000'0000'0004ull));
    EXPECT_EQ(funnel_shl(x, 11), static_cast<int_multiprecision_t>(0x0000'0000'0000'0008ull));
}

TEST(WideOps, FunnelShlUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::funnel_shl;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    constexpr wide<uint_multiprecision_t> x{
        .low_bits  = 0x0123'4567'89AB'CDEFull,
        .high_bits = 0xFEDC'BA98'7654'3210ull,
    };

    EXPECT_EQ(funnel_shl(x, 0), 0xFEDC'BA98'7654'3210ull);
    EXPECT_EQ(funnel_shl(x, 1), 0xFDB9'7530'ECA8'6420ull);
    EXPECT_EQ(funnel_shl(x, 2), 0xFB72'EA61'D950'C840ull);
    EXPECT_EQ(funnel_shl(x, 3), 0xF6E5'D4C3'B2A1'9080ull);
    EXPECT_EQ(funnel_shl(x, 4), 0xEDCB'A987'6543'2100ull);
    EXPECT_EQ(funnel_shl(x, 5), 0xDB97'530E'CA86'4200ull);
    EXPECT_EQ(funnel_shl(x, 6), 0xB72E'A61D'950C'8400ull);
    EXPECT_EQ(funnel_shl(x, 7), 0x6E5D'4C3B'2A19'0800ull);
    EXPECT_EQ(funnel_shl(x, 8), 0xDCBA'9876'5432'1001ull);
    EXPECT_EQ(funnel_shl(x, 9), 0xB975'30EC'A864'2002ull);
    EXPECT_EQ(funnel_shl(x, 10), 0x72EA'61D9'50C8'4004ull);
    EXPECT_EQ(funnel_shl(x, 11), 0xE5D4'C3B2'A190'8009ull);
}

TEST(WideOps, FunnelShrInt) {
    using BEMAN_BIG_INT_NAMESPACE::detail::funnel_shr;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    constexpr wide<int_multiprecision_t> x{
        .low_bits  = static_cast<int_multiprecision_t>(0x0102'0304'0506'0708ull),
        .high_bits = 0,
    };

    EXPECT_EQ(funnel_shr(x, 0), static_cast<int_multiprecision_t>(0x0102'0304'0506'0708ull));
    EXPECT_EQ(funnel_shr(x, 1), static_cast<int_multiprecision_t>(0x0081'0182'0283'0384ull));
    EXPECT_EQ(funnel_shr(x, 2), static_cast<int_multiprecision_t>(0x0040'80C1'0141'81C2ull));
    EXPECT_EQ(funnel_shr(x, 3), static_cast<int_multiprecision_t>(0x0020'4060'80A0'C0E1ull));
    EXPECT_EQ(funnel_shr(x, 4), static_cast<int_multiprecision_t>(0x0010'2030'4050'6070ull));
    EXPECT_EQ(funnel_shr(x, 5), static_cast<int_multiprecision_t>(0x0008'1018'2028'3038ull));
    EXPECT_EQ(funnel_shr(x, 6), static_cast<int_multiprecision_t>(0x0004'080C'1014'181Cull));
    EXPECT_EQ(funnel_shr(x, 7), static_cast<int_multiprecision_t>(0x0002'0406'080A'0C0Eull));
    EXPECT_EQ(funnel_shr(x, 8), static_cast<int_multiprecision_t>(0x0001'0203'0405'0607ull));
    EXPECT_EQ(funnel_shr(x, 9), static_cast<int_multiprecision_t>(0x0000'8101'8202'8303ull));
    EXPECT_EQ(funnel_shr(x, 10), static_cast<int_multiprecision_t>(0x0000'4080'C101'4181ull));
    EXPECT_EQ(funnel_shr(x, 11), static_cast<int_multiprecision_t>(0x0000'2040'6080'A0C0ull));
}

TEST(WideOps, FunnelShrUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::funnel_shr;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    constexpr wide<uint_multiprecision_t> x{
        .low_bits  = 0x0011'2233'4455'6677ull,
        .high_bits = 0x8899'AABB'CCDD'EEFFull,
    };

    EXPECT_EQ(funnel_shr(x, 0), 0x0011'2233'4455'6677ull);
    EXPECT_EQ(funnel_shr(x, 1), 0x8008'9119'A22A'B33Bull);
    EXPECT_EQ(funnel_shr(x, 2), 0xC004'488C'D115'599Dull);
    EXPECT_EQ(funnel_shr(x, 3), 0xE002'2446'688A'ACCEull);
    EXPECT_EQ(funnel_shr(x, 4), 0xF001'1223'3445'5667ull);
    EXPECT_EQ(funnel_shr(x, 5), 0xF800'8911'9A22'AB33ull);
    EXPECT_EQ(funnel_shr(x, 6), 0xFC00'4488'CD11'5599ull);
    EXPECT_EQ(funnel_shr(x, 7), 0xFE00'2244'6688'AACCull);
    EXPECT_EQ(funnel_shr(x, 8), 0xFF00'1122'3344'5566ull);
    EXPECT_EQ(funnel_shr(x, 9), 0x7F80'0891'19A2'2AB3ull);
    EXPECT_EQ(funnel_shr(x, 10), 0xBFC0'0448'8CD1'1559ull);
    EXPECT_EQ(funnel_shr(x, 11), 0xDFE0'0224'4668'8AACull);
}

TEST(WideOps, OverflowingAddInt) {
    using BEMAN_BIG_INT_NAMESPACE::detail::overflowing_add;

    const auto r1 = overflowing_add<int_multiprecision_t>(0, 0);
    const auto r2 = overflowing_add<int_multiprecision_t>(7, 5);
    const auto r3 = overflowing_add<int_multiprecision_t>(-7, -5);
    const auto r4 = overflowing_add<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::max(), 1);
    const auto r5 = overflowing_add<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), -1);
    const auto r6 = overflowing_add<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::max(), -1);

    EXPECT_EQ(r1.value, static_cast<int_multiprecision_t>(0));
    EXPECT_FALSE(r1.overflow);
    EXPECT_EQ(r2.value, static_cast<int_multiprecision_t>(12));
    EXPECT_FALSE(r2.overflow);
    EXPECT_EQ(r3.value, static_cast<int_multiprecision_t>(-12));
    EXPECT_FALSE(r3.overflow);
    EXPECT_EQ(r4.value, std::numeric_limits<int_multiprecision_t>::min());
    EXPECT_TRUE(r4.overflow);
    EXPECT_EQ(r5.value, std::numeric_limits<int_multiprecision_t>::max());
    EXPECT_TRUE(r5.overflow);
    EXPECT_EQ(r6.value, std::numeric_limits<int_multiprecision_t>::max() - 1);
    EXPECT_FALSE(r6.overflow);
}

TEST(WideOps, OverflowingAddUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::overflowing_add;

    const auto r1 = overflowing_add<uint_multiprecision_t>(0ull, 0ull);
    const auto r2 = overflowing_add<uint_multiprecision_t>(1ull, 2ull);
    const auto r3 = overflowing_add<uint_multiprecision_t>(7ull, 5ull);
    const auto r4 = overflowing_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 1ull);
    const auto r5 = overflowing_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 0ull);
    const auto r6 =
        overflowing_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max() - 1ull, 1ull);

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_FALSE(r1.overflow);
    EXPECT_EQ(r2.value, 3ull);
    EXPECT_FALSE(r2.overflow);
    EXPECT_EQ(r3.value, 12ull);
    EXPECT_FALSE(r3.overflow);
    EXPECT_EQ(r4.value, 0ull);
    EXPECT_TRUE(r4.overflow);
    EXPECT_EQ(r5.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_FALSE(r5.overflow);
    EXPECT_EQ(r6.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_FALSE(r6.overflow);
}

// Types narrower than int promote in x + y; the overflow must still be seen
// (the portable path, used by MSVC on ARM64 and in constant evaluation).
TEST(WideOps, OverflowingAddNarrowUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::carrying_add;
    using BEMAN_BIG_INT_NAMESPACE::detail::overflowing_add;

    static_assert(overflowing_add<std::uint16_t>(0xFFFFu, 1u).overflow);
    static_assert(overflowing_add<std::uint8_t>(0xFFu, 1u).overflow);
    static_assert(!overflowing_add<std::uint16_t>(0xFFFEu, 1u).overflow);
    static_assert(carrying_add<std::uint16_t>(0xFFFFu, 0u, true).carry);

    const auto r1 = overflowing_add<std::uint16_t>(0xFFFFu, 2u);
    const auto r2 = overflowing_add<std::uint8_t>(0x80u, 0x80u);
    const auto r3 = carrying_add<std::uint16_t>(0x8000u, 0x8000u, true);

    EXPECT_EQ(r1.value, 1u);
    EXPECT_TRUE(r1.overflow);
    EXPECT_EQ(r2.value, 0u);
    EXPECT_TRUE(r2.overflow);
    EXPECT_EQ(r3.value, 1u);
    EXPECT_TRUE(r3.carry);
}

TEST(WideOps, OverflowingSubInt) {
    using BEMAN_BIG_INT_NAMESPACE::detail::overflowing_sub;

    const auto r1 = overflowing_sub<int_multiprecision_t>(0, 0);
    const auto r2 = overflowing_sub<int_multiprecision_t>(7, 5);
    const auto r3 = overflowing_sub<int_multiprecision_t>(-7, -5);
    const auto r4 = overflowing_sub<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), 1);
    const auto r5 = overflowing_sub<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::max(), -1);
    const auto r6 = overflowing_sub<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), -1);

    EXPECT_EQ(r1.value, static_cast<int_multiprecision_t>(0));
    EXPECT_FALSE(r1.overflow);
    EXPECT_EQ(r2.value, static_cast<int_multiprecision_t>(2));
    EXPECT_FALSE(r2.overflow);
    EXPECT_EQ(r3.value, static_cast<int_multiprecision_t>(-2));
    EXPECT_FALSE(r3.overflow);
    EXPECT_EQ(r4.value, std::numeric_limits<int_multiprecision_t>::max());
    EXPECT_TRUE(r4.overflow);
    EXPECT_EQ(r5.value, std::numeric_limits<int_multiprecision_t>::min());
    EXPECT_TRUE(r5.overflow);
    EXPECT_EQ(r6.value, std::numeric_limits<int_multiprecision_t>::min() + 1);
    EXPECT_FALSE(r6.overflow);
}

TEST(WideOps, OverflowingSubUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::overflowing_sub;

    const auto r1 = overflowing_sub<uint_multiprecision_t>(0ull, 0ull);
    const auto r2 = overflowing_sub<uint_multiprecision_t>(7ull, 5ull);
    const auto r3 = overflowing_sub<uint_multiprecision_t>(0ull, 1ull);
    const auto r4 = overflowing_sub<uint_multiprecision_t>(1ull, 0ull);
    const auto r5 = overflowing_sub<uint_multiprecision_t>(1ull, 1ull);
    const auto r6 = overflowing_sub<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(),
                                                           std::numeric_limits<uint_multiprecision_t>::max());

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_FALSE(r1.overflow);
    EXPECT_EQ(r2.value, 2ull);
    EXPECT_FALSE(r2.overflow);
    EXPECT_EQ(r3.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_TRUE(r3.overflow);
    EXPECT_EQ(r4.value, 1ull);
    EXPECT_FALSE(r4.overflow);
    EXPECT_EQ(r5.value, 0ull);
    EXPECT_FALSE(r5.overflow);
    EXPECT_EQ(r6.value, 0ull);
    EXPECT_FALSE(r6.overflow);
}

TEST(WideOps, OverflowingMulUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::overflowing_mul;

    const auto r1 = overflowing_mul<uint_multiprecision_t>(0ull, 7ull);
    const auto r2 = overflowing_mul<uint_multiprecision_t>(1ull, 1ull);
    const auto r3 = overflowing_mul<uint_multiprecision_t>(7ull, 5ull);
    const auto r4 = overflowing_mul<uint_multiprecision_t>(1ull << 32, 1ull << 32);
    const auto r5 = overflowing_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 2ull);
    const auto r6 = overflowing_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(),
                                                           std::numeric_limits<uint_multiprecision_t>::max());

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_FALSE(r1.overflow);
    EXPECT_EQ(r2.value, 1ull);
    EXPECT_FALSE(r2.overflow);
    EXPECT_EQ(r3.value, 35ull);
    EXPECT_FALSE(r3.overflow);
    EXPECT_EQ(r4.value, 0ull);
    EXPECT_TRUE(r4.overflow);
    EXPECT_EQ(r5.value, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_TRUE(r5.overflow);
    EXPECT_EQ(r6.value, 1ull);
    EXPECT_TRUE(r6.overflow);
}

TEST(WideOps, CarryingAddUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::carrying_add;

    const auto r1 = carrying_add<uint_multiprecision_t>(0ull, 0ull, false);
    const auto r2 = carrying_add<uint_multiprecision_t>(1ull, 2ull, false);
    const auto r3 = carrying_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 0ull, true);
    const auto r4 = carrying_add<uint_multiprecision_t>(
        std::numeric_limits<uint_multiprecision_t>::max(), std::numeric_limits<uint_multiprecision_t>::max(), false);
    const auto r5 = carrying_add<uint_multiprecision_t>(
        std::numeric_limits<uint_multiprecision_t>::max(), std::numeric_limits<uint_multiprecision_t>::max(), true);
    const auto r6 = carrying_add<uint_multiprecision_t>(42ull, 58ull, false);

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_FALSE(r1.carry);
    EXPECT_EQ(r2.value, 3ull);
    EXPECT_FALSE(r2.carry);
    EXPECT_EQ(r3.value, 0ull);
    EXPECT_TRUE(r3.carry);
    EXPECT_EQ(r4.value, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_TRUE(r4.carry);
    EXPECT_EQ(r5.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_TRUE(r5.carry);
    EXPECT_EQ(r6.value, 100ull);
    EXPECT_FALSE(r6.carry);
}

TEST(WideOps, BorrowingSubUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::borrowing_sub;

    const auto r1 = borrowing_sub<uint_multiprecision_t>(0ull, 0ull, false);
    const auto r2 = borrowing_sub<uint_multiprecision_t>(5ull, 3ull, false);
    const auto r3 = borrowing_sub<uint_multiprecision_t>(0ull, 0ull, true);
    const auto r4 = borrowing_sub<uint_multiprecision_t>(0ull, 1ull, false);
    const auto r5 = borrowing_sub<uint_multiprecision_t>(1ull, 1ull, false);
    const auto r6 = borrowing_sub<uint_multiprecision_t>(100ull, 58ull, false);

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_FALSE(r1.borrow);
    EXPECT_EQ(r2.value, 2ull);
    EXPECT_FALSE(r2.borrow);
    EXPECT_EQ(r3.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_TRUE(r3.borrow);
    EXPECT_EQ(r4.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_TRUE(r4.borrow);
    EXPECT_EQ(r5.value, 0ull);
    EXPECT_FALSE(r5.borrow);
    EXPECT_EQ(r6.value, 42ull);
    EXPECT_FALSE(r6.borrow);
}

TEST(WideOps, NarrowingDivUint) {
    using BEMAN_BIG_INT_NAMESPACE::detail::narrowing_div;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    const auto r1 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 5ull, .high_bits = 1ull}, 3ull);
    const auto r2 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 20ull, .high_bits = 0ull}, 6ull);
    const auto r3 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 0ull, .high_bits = 0ull}, 7ull);
    const auto r4 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 1ull, .high_bits = 0ull}, 1ull);
    const auto r5 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 13ull, .high_bits = 1ull}, 5ull);
    const auto r6 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 14ull, .high_bits = 1ull}, 5ull);

    EXPECT_EQ(r1.quotient, 6'148'914'691'236'517'207ull);
    EXPECT_EQ(r1.remainder, 0ull);
    EXPECT_EQ(r2.quotient, 3ull);
    EXPECT_EQ(r2.remainder, 2ull);
    EXPECT_EQ(r3.quotient, 0ull);
    EXPECT_EQ(r3.remainder, 0ull);
    EXPECT_EQ(r4.quotient, 1ull);
    EXPECT_EQ(r4.remainder, 0ull);
    EXPECT_EQ(r5.quotient, 3'689'348'814'741'910'325ull);
    EXPECT_EQ(r5.remainder, 4ull);
    EXPECT_EQ(r6.quotient, 3'689'348'814'741'910'326ull);
    EXPECT_EQ(r6.remainder, 0ull);
}

TEST(WideOps, DivideWideByWideFast) {
    using BEMAN_BIG_INT_NAMESPACE::detail::divide_wide_by_wide;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    // Fast path: T == uint_multiprecision_t, wider_t<T> exists.
    const auto r1 = divide_wide_by_wide(wide<uint_multiprecision_t>{.low_bits = 1ull, .high_bits = 2ull},
                                        wide<uint_multiprecision_t>{.low_bits = 0ull, .high_bits = 1ull});
    EXPECT_EQ(r1.quotient, 2ull);
    EXPECT_EQ(r1.remainder.low_bits, 1ull);
    EXPECT_EQ(r1.remainder.high_bits, 0ull);

    // a == b.
    const auto r2 = divide_wide_by_wide(wide<uint_multiprecision_t>{.low_bits = 42ull, .high_bits = 7ull},
                                        wide<uint_multiprecision_t>{.low_bits = 42ull, .high_bits = 7ull});
    EXPECT_EQ(r2.quotient, 1ull);
    EXPECT_EQ(r2.remainder.low_bits, 0ull);
    EXPECT_EQ(r2.remainder.high_bits, 0ull);

    // a < b.
    const auto r3 = divide_wide_by_wide(wide<uint_multiprecision_t>{.low_bits = 0ull, .high_bits = 1ull},
                                        wide<uint_multiprecision_t>{.low_bits = 0ull, .high_bits = 2ull});
    EXPECT_EQ(r3.quotient, 0ull);
    EXPECT_EQ(r3.remainder.low_bits, 0ull);
    EXPECT_EQ(r3.remainder.high_bits, 1ull);

    // Maximum dividend divided by the smallest valid 2-limb divisor.
    constexpr auto max_limb = std::numeric_limits<uint_multiprecision_t>::max();
    const auto     r4 = divide_wide_by_wide(wide<uint_multiprecision_t>{.low_bits = max_limb, .high_bits = max_limb},
                                            wide<uint_multiprecision_t>{.low_bits = 0ull, .high_bits = 1ull});
    EXPECT_EQ(r4.quotient, max_limb);
    EXPECT_EQ(r4.remainder.low_bits, max_limb);
    EXPECT_EQ(r4.remainder.high_bits, 0ull);
}

TEST(WideOps, DivideWideByWidePortable) {
    using BEMAN_BIG_INT_NAMESPACE::detail::divide_wide_by_wide_portable;
    using BEMAN_BIG_INT_NAMESPACE::detail::wide;

    // Call the portable long-division directly.
    using T = std::uint16_t;

    auto check = [](T a_lo, T a_hi, T b_lo, T b_hi) {
        const auto          r     = divide_wide_by_wide_portable(wide<T>{.low_bits = a_lo, .high_bits = a_hi},
                                                                 wide<T>{.low_bits = b_lo, .high_bits = b_hi});
        const std::uint32_t a     = (static_cast<std::uint32_t>(a_hi) << 16) | a_lo;
        const std::uint32_t b     = (static_cast<std::uint32_t>(b_hi) << 16) | b_lo;
        const std::uint32_t got_r = (static_cast<std::uint32_t>(r.remainder.high_bits) << 16) | r.remainder.low_bits;
        EXPECT_EQ(static_cast<std::uint32_t>(r.quotient), a / b);
        EXPECT_EQ(got_r, a % b);
    };

    // Boundary cases.
    check(0x0000, 0x0000, 0x0000, 0x0001); // a = 0.
    check(0x0001, 0x0001, 0x0001, 0x0001); // a == b.
    check(0xFFFF, 0x7FFF, 0x0001, 0x8000); // a < b.
    check(0xFFFF, 0xFFFF, 0x0000, 0x0001); // max / minimum 2-limb divisor.
    check(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF); // max / max.
    check(0x0000, 0x8000, 0x0000, 0x8000); // normalized divisor, exact quotient.
    check(0xDEAD, 0xBEEF, 0xCAFE, 0x1234); // arbitrary pattern.

    // Q correction path: dividend top equals divisor top so the Knuth-D-style
    // trial quotient would saturate; the algorithm must still converge.
    check(0x0000, 0x8000, 0x0001, 0x8000);
    check(0xFFFE, 0x8000, 0xFFFF, 0x8000);

    // Sweep a grid of values to exercise the bit-by-bit loop thoroughly.
    for (std::uint32_t a_hi : {0u, 1u, 2u, 0x7FFFu, 0x8000u, 0xABCDu, 0xFFFFu}) {
        for (std::uint32_t b_hi : {1u, 2u, 0x7FFFu, 0x8000u, 0xABCDu, 0xFFFFu}) {
            for (std::uint32_t a_lo : {0u, 1u, 0x00FFu, 0x5A5Au, 0xA5A5u, 0xFFFFu}) {
                for (std::uint32_t b_lo : {0u, 1u, 0x00FFu, 0x5A5Au, 0xA5A5u, 0xFFFFu}) {
                    check(static_cast<T>(a_lo), static_cast<T>(a_hi), static_cast<T>(b_lo), static_cast<T>(b_hi));
                }
            }
        }
    }
}

// ----- Moller-Granlund preinv division primitives -----

using BEMAN_BIG_INT_NAMESPACE::detail::div_2by1_preinv;
using BEMAN_BIG_INT_NAMESPACE::detail::div_3by2_preinv;
using BEMAN_BIG_INT_NAMESPACE::detail::narrowing_div;
using BEMAN_BIG_INT_NAMESPACE::detail::reciprocal_word;
using BEMAN_BIG_INT_NAMESPACE::detail::reciprocal_word_3by2;
using BEMAN_BIG_INT_NAMESPACE::detail::wide;
using BEMAN_BIG_INT_NAMESPACE::detail::widening_mul;

TEST(WideOps, WiderTraitMatchesPlatform) {
    using BEMAN_BIG_INT_NAMESPACE::detail::has_int128_v;
    using BEMAN_BIG_INT_NAMESPACE::detail::has_wider;
    using BEMAN_BIG_INT_NAMESPACE::detail::wider_t;

    static_assert(std::is_same_v<wider_t<std::uint8_t>, std::uint16_t>);
    static_assert(std::is_same_v<wider_t<std::uint16_t>, std::uint32_t>);
    static_assert(std::is_same_v<wider_t<std::uint32_t>, std::uint64_t>);
    static_assert(std::is_same_v<wider_t<std::int8_t>, std::int16_t>);
    static_assert(std::is_same_v<wider_t<std::int16_t>, std::int32_t>);
    static_assert(std::is_same_v<wider_t<std::int32_t>, std::int64_t>);

    // On targets without any 128-bit type, wider_t<uint64_t> must be quietly
    // absent, so that wide<uint64_t> falls back onto the data-only primary
    // template. A hard error from this query is what used to break the NTT's
    // Montgomery arithmetic on 32-bit microcontrollers.
    static_assert(has_wider<std::uint64_t> == has_int128_v);
    static_assert(has_wider<std::int64_t> == has_int128_v);
}

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wuseless-cast")

TEST(WideOps, NarrowingDivPortable) {
    using BEMAN_BIG_INT_NAMESPACE::detail::narrowing_div_portable;

    // Check the bit-by-bit loop with uint16 limbs against uint32 division.
    using T = std::uint16_t;

    auto check = [](T x_lo, T x_hi, T y) {
        const auto          r = narrowing_div_portable(wide<T>{.low_bits = x_lo, .high_bits = x_hi}, y);
        const std::uint32_t x = (static_cast<std::uint32_t>(x_hi) << 16) | x_lo;
        EXPECT_EQ(r.quotient, static_cast<T>(x / y)) << "x=" << x << " y=" << y;
        EXPECT_EQ(r.remainder, static_cast<T>(x % y)) << "x=" << x << " y=" << y;
    };

    check(0x0000, 0x0000, 0x0001); // 0 / 1.
    check(0xFFFF, 0x0000, 0x0001); // single-limb dividend, identity quotient.
    check(0xFFFF, 0xFFFE, 0xFFFF); // largest dividend for the largest divisor.
    check(0x0001, 0x8000, 0x8001); // quotient near B with a near-normalized divisor.
    check(0xDEAD, 0x1234, 0x5678); // arbitrary pattern.

    for (std::uint32_t x_hi : {0u, 1u, 2u, 0x7FFFu, 0x8000u, 0xABCDu, 0xFFFEu}) {
        for (std::uint32_t y : {1u, 2u, 3u, 0x7FFFu, 0x8000u, 0xABCDu, 0xFFFFu}) {
            if (x_hi >= y) {
                continue; // precondition: the quotient must fit in one limb
            }
            for (std::uint32_t x_lo : {0u, 1u, 0x00FFu, 0x5A5Au, 0xA5A5u, 0xFFFFu}) {
                check(static_cast<T>(x_lo), static_cast<T>(x_hi), static_cast<T>(y));
            }
        }
    }

    // The limb-width instantiation must agree with narrowing_div.
    std::mt19937_64 rng{0x47u};
    for (int i = 0; i < 1000; ++i) {
        const auto y    = static_cast<uint_multiprecision_t>(rng() | 1u);
        const auto x_hi = static_cast<uint_multiprecision_t>(rng() % y);
        const auto x_lo = static_cast<uint_multiprecision_t>(rng());

        const wide<uint_multiprecision_t> x{.low_bits = x_lo, .high_bits = x_hi};
        const auto                        expected = narrowing_div(x, y);
        const auto                        actual   = narrowing_div_portable(x, y);
        EXPECT_EQ(actual.quotient, expected.quotient);
        EXPECT_EQ(actual.remainder, expected.remainder);
    }

    // The portable path must also work during constant evaluation.
    static_assert(narrowing_div_portable(wide<std::uint64_t>{.low_bits = 5u, .high_bits = 1u}, std::uint64_t{3}) ==
                  BEMAN_BIG_INT_NAMESPACE::div_result<std::uint64_t>{6'148'914'691'236'517'207ull, 0ull});
}

BEMAN_BIG_INT_DIAGNOSTIC_POP()

// v must equal floor((B^2 - 1) / d) - B, computed independently via wider_t.
template <class T>
void check_reciprocal_word(const T d) {
    using W                        = BEMAN_BIG_INT_NAMESPACE::detail::wider_t<T>;
    constexpr std::size_t w        = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    const W               all_ones = static_cast<W>(static_cast<W>(0) - 1);
    const T               expected = static_cast<T>(all_ones / d - (static_cast<W>(1) << w));
    EXPECT_EQ(reciprocal_word(d), expected) << "d=" << d;
}

TEST(WideOps, ReciprocalWordExhaustive16) {
    using T = std::uint16_t;
    for (std::uint32_t d = 0x8000u; d <= 0xFFFFu; ++d) {
        check_reciprocal_word(static_cast<T>(d));
    }
}

TEST(WideOps, ReciprocalWordLimb) {
    using T                   = uint_multiprecision_t;
    constexpr std::size_t w   = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           top = T{1} << (w - 1);
    constexpr T           max = std::numeric_limits<T>::max();

    check_reciprocal_word(top);
    check_reciprocal_word(top + 1);
    check_reciprocal_word(max - 1);
    check_reciprocal_word(max);

    std::mt19937_64 rng{0x42u};
    for (int i = 0; i < 10000; ++i) {
        check_reciprocal_word(static_cast<T>(rng()) | top);
    }
}

// Compile-time spot checks: floor((B^2-1)/(B/2)) - B == B - 1 and
// floor((B^2-1)/(B-1)) - B == 1.
consteval bool ce_reciprocal_word() {
    using T                   = uint_multiprecision_t;
    constexpr std::size_t w   = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           max = std::numeric_limits<T>::max();
    return reciprocal_word(T{1} << (w - 1)) == max && reciprocal_word(max) == 1;
}
static_assert(ce_reciprocal_word());

// The defining property of the 3/2 reciprocal, checked with limb arithmetic
// only: (B + v) * <d1,d0> <= B^3 - 1 < (B + v + 1) * <d1,d0>.
template <class T>
void check_reciprocal_word_3by2(const T d1, const T d0) {
    using BEMAN_BIG_INT_NAMESPACE::detail::carrying_add;
    const T v = reciprocal_word_3by2(d1, d0);

    // 4-limb accumulation of (B + v) * (d1*B + d0) = v*d0 + (v*d1 + d0)*B + d1*B^2.
    const wide<T> p00   = widening_mul(v, d0);
    const wide<T> p01   = widening_mul(v, d1);
    const T       limb0 = p00.low_bits;
    const auto    s1    = carrying_add(p00.high_bits, p01.low_bits);
    const auto    s1b   = carrying_add(s1.value, d0);
    const T       limb1 = s1b.value;
    const auto    s2    = carrying_add(p01.high_bits, d1, s1.carry);
    const auto    s2b   = carrying_add(s2.value, static_cast<T>(0), s1b.carry);
    const T       limb2 = s2b.value;
    const T       limb3 = static_cast<T>(static_cast<T>(s2.carry) + static_cast<T>(s2b.carry));

    // <= B^3 - 1 is exactly "the product fits three limbs".
    EXPECT_EQ(limb3, 0u) << "d1=" << d1 << " d0=" << d0;

    // One more divisor must overflow the third limb.
    const auto a0 = carrying_add(limb0, d0);
    const auto a1 = carrying_add(limb1, d1, a0.carry);
    const auto a2 = carrying_add(limb2, static_cast<T>(0), a1.carry);
    EXPECT_TRUE(a2.carry) << "d1=" << d1 << " d0=" << d0;
}

TEST(WideOps, ReciprocalWord3by2Exhaustive16) {
    using T = std::uint16_t;
    for (std::uint32_t d1 = 0x8000u; d1 <= 0xFFFFu; ++d1) {
        for (const std::uint32_t d0 : {0x0000u, 0x0001u, 0x7FFFu, 0x8000u, 0xABCDu, 0xFFFEu, 0xFFFFu}) {
            check_reciprocal_word_3by2(static_cast<T>(d1), static_cast<T>(d0));
        }
        // A zero low limb must reduce exactly to the 2/1 reciprocal.
        EXPECT_EQ(reciprocal_word_3by2(static_cast<T>(d1), static_cast<T>(0)), reciprocal_word(static_cast<T>(d1)));
    }
}

TEST(WideOps, ReciprocalWord3by2Limb) {
    using T                   = uint_multiprecision_t;
    constexpr std::size_t w   = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           top = T{1} << (w - 1);
    constexpr T           max = std::numeric_limits<T>::max();

    check_reciprocal_word_3by2(top, static_cast<T>(0));
    check_reciprocal_word_3by2(top, static_cast<T>(1));
    check_reciprocal_word_3by2(top, max);
    check_reciprocal_word_3by2(max, max);
    check_reciprocal_word_3by2(max, static_cast<T>(0));

    std::mt19937_64 rng{0x43u};
    for (int i = 0; i < 10000; ++i) {
        const T d1 = static_cast<T>(rng()) | top;
        const T d0 = static_cast<T>(rng());
        check_reciprocal_word_3by2(d1, d0);
        EXPECT_EQ(reciprocal_word_3by2(d1, static_cast<T>(0)), reciprocal_word(d1));
    }
}

// div_2by1_preinv against the trusted narrowing_div.
template <class T>
void check_div_2by1(const wide<T> u, const T d) {
    const T    v        = reciprocal_word(d);
    const auto got      = div_2by1_preinv(u, d, v);
    const auto expected = narrowing_div(u, d);
    EXPECT_EQ(got.quotient, expected.quotient) << "u=<" << u.high_bits << ',' << u.low_bits << "> d=" << d;
    EXPECT_EQ(got.remainder, expected.remainder) << "u=<" << u.high_bits << ',' << u.low_bits << "> d=" << d;
}

TEST(WideOps, Div2by1PreinvSweep16) {
    using T = std::uint16_t;
    // Full divisor range x a value grid; the 16-bit space is dense enough to
    // hit the rare final-correction path.
    for (std::uint32_t d = 0x8000u; d <= 0xFFFFu; ++d) {
        for (const std::uint32_t hi : {0u, 1u, 0x7FFFu, d - 1u}) {
            for (const std::uint32_t lo : {0u, 1u, 0x8000u, 0xFFFEu, 0xFFFFu}) {
                check_div_2by1(wide<T>{.low_bits = static_cast<T>(lo), .high_bits = static_cast<T>(hi)},
                               static_cast<T>(d));
            }
        }
    }
}

TEST(WideOps, Div2by1PreinvLimb) {
    using T                   = uint_multiprecision_t;
    constexpr std::size_t w   = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           top = T{1} << (w - 1);
    constexpr T           max = std::numeric_limits<T>::max();

    check_div_2by1(wide<T>{.low_bits = max, .high_bits = top - 1}, top);
    check_div_2by1(wide<T>{.low_bits = max, .high_bits = max - 1}, max);
    check_div_2by1(wide<T>{.low_bits = 0, .high_bits = 0}, top);

    std::mt19937_64 rng{0x44u};
    for (int i = 0; i < 50000; ++i) {
        const T d  = static_cast<T>(rng()) | top;
        const T hi = static_cast<T>(rng()) % d;
        const T lo = static_cast<T>(rng());
        check_div_2by1(wide<T>{.low_bits = lo, .high_bits = hi}, d);
    }
}

consteval bool ce_div_2by1_preinv() {
    using T                 = uint_multiprecision_t;
    constexpr std::size_t w = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           d = T{1} << (w - 1);
    // <d-1, max> / d: quotient max, remainder d - 1.
    const auto r =
        div_2by1_preinv(wide<T>{.low_bits = std::numeric_limits<T>::max(), .high_bits = d - 1}, d, reciprocal_word(d));
    return r.quotient == std::numeric_limits<T>::max() && r.remainder == d - 1;
}
static_assert(ce_div_2by1_preinv());

// div_3by2_preinv checked by reconstruction: q * <d1,d0> + R == <u2,u1,u0>
// with R < <d1,d0>, using limb arithmetic only.
template <class T>
void check_div_3by2(const T u2, const T u1, const T u0, const T d1, const T d0) {
    using BEMAN_BIG_INT_NAMESPACE::detail::carrying_add;

    const T    v   = reciprocal_word_3by2(d1, d0);
    const auto got = div_3by2_preinv(u2, u1, u0, d1, d0, v);
    const T    q   = got.quotient;
    const T    r0  = got.remainder.low_bits;
    const T    r1  = got.remainder.high_bits;

    EXPECT_TRUE(r1 < d1 || (r1 == d1 && r0 < d0))
        << "u=<" << u2 << ',' << u1 << ',' << u0 << "> d=<" << d1 << ',' << d0 << '>';

    // q * <d1,d0> accumulated over three limbs, plus R, must equal U.
    const wide<T> p0 = widening_mul(q, d0);
    const wide<T> p1 = widening_mul(q, d1);
    T             l0 = p0.low_bits;
    const auto    m1 = carrying_add(p0.high_bits, p1.low_bits);
    T             l1 = m1.value;
    T             l2 = static_cast<T>(p1.high_bits + static_cast<T>(m1.carry));

    const auto a0 = carrying_add(l0, r0);
    l0            = a0.value;
    const auto a1 = carrying_add(l1, r1, a0.carry);
    l1            = a1.value;
    l2            = static_cast<T>(l2 + static_cast<T>(a1.carry));

    EXPECT_EQ(l0, u0);
    EXPECT_EQ(l1, u1);
    EXPECT_EQ(l2, u2) << "u=<" << u2 << ',' << u1 << ',' << u0 << "> d=<" << d1 << ',' << d0 << '>';
}

TEST(WideOps, Div3by2PreinvSweep16) {
    using T = std::uint16_t;
    std::mt19937_64 rng{0x45u};
    // Dense randomized sweep; at 16 bits the rare-correction branches fire.
    for (int i = 0; i < 300000; ++i) {
        const T d1 = static_cast<T>(static_cast<T>(rng()) | 0x8000u);
        const T d0 = static_cast<T>(rng());
        T       a2 = static_cast<T>(static_cast<T>(rng()) % (static_cast<std::uint32_t>(d1) + 1u));
        T       a1 = static_cast<T>(rng());
        if (a2 == d1 && a1 >= d0) {
            if (d0 == 0) {
                --a2;
            } else {
                a1 = static_cast<T>(d0 - 1);
            }
        }
        check_div_3by2(a2, a1, static_cast<T>(rng()), d1, d0);
    }
}

TEST(WideOps, Div3by2PreinvLimb) {
    using T                   = uint_multiprecision_t;
    constexpr std::size_t w   = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           top = T{1} << (w - 1);
    constexpr T           max = std::numeric_limits<T>::max();

    // Boundary shapes: top pair one below the divisor pair, zero low limbs,
    // saturated low limbs, minimal normalized divisor.
    check_div_3by2(top - 1, max, max, top, static_cast<T>(0));
    check_div_3by2(top, max - 1, max, top, max);
    check_div_3by2(static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), top, static_cast<T>(1));
    check_div_3by2(max - 1, max, max, max, max);

    std::mt19937_64 rng{0x46u};
    for (int i = 0; i < 50000; ++i) {
        const T d1 = static_cast<T>(rng()) | top;
        const T d0 = static_cast<T>(rng());
        T       a2 = d1 == max ? static_cast<T>(rng()) : static_cast<T>(rng()) % (d1 + 1);
        T       a1 = static_cast<T>(rng());
        if (a2 > d1) {
            a2 = d1;
        }
        if (a2 == d1 && a1 >= d0) {
            if (d0 == 0) {
                --a2;
            } else {
                a1 = d0 - 1;
            }
        }
        check_div_3by2(a2, a1, static_cast<T>(rng()), d1, d0);
    }
}

consteval bool ce_div_3by2_preinv() {
    using T                   = uint_multiprecision_t;
    constexpr std::size_t w   = BEMAN_BIG_INT_NAMESPACE::detail::width_v<T>;
    constexpr T           top = T{1} << (w - 1);
    // <top-1, max, max> / <top, 0>: quotient max, remainder <top-1, max>.
    const auto r = div_3by2_preinv(top - 1,
                                   std::numeric_limits<T>::max(),
                                   std::numeric_limits<T>::max(),
                                   top,
                                   static_cast<T>(0),
                                   reciprocal_word_3by2(top, static_cast<T>(0)));
    return r.quotient == std::numeric_limits<T>::max() && r.remainder.high_bits == top - 1 &&
           r.remainder.low_bits == std::numeric_limits<T>::max();
}
static_assert(ce_div_3by2_preinv());

} // namespace
