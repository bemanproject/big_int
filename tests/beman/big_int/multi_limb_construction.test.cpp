// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include <climits>
#include <cstddef>
#include <cstdint>

#include "testing.hpp"

// Tests for the multi-limb path of `assign_magnitude`, exercised when the source
// integer type is wider than the limb type (i.e. `value_limbs > 1`).
// `__int128` and `_BitInt` are guarded individually since each is only
// conditionally available.

// clang-19 totally crashes running these tests
#if defined(__clang__) && __clang_major__ == 19

TEST(MultiLimbConstruction, SkippedUnderClang19) {
    GTEST_SKIP() << "Skipped: clang-19 crashes compiling these tests.";
}

#else

namespace {

constexpr std::size_t test_limb_bits = sizeof(BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t) * CHAR_BIT;

    #ifdef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL

using i128 = BEMAN_BIG_INT_NAMESPACE::detail::int128_t;
using u128 = BEMAN_BIG_INT_NAMESPACE::detail::uint128_t;

constexpr u128 make_u128(std::uint64_t high, std::uint64_t low) noexcept {
    return (static_cast<u128>(high) << 64) | static_cast<u128>(low);
}

// ----- compile-time tests -----

consteval bool test_int128_construction_unsigned_large() {
    const u128                       value = make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    return static_cast<u128>(x) == value;
}
static_assert(test_int128_construction_unsigned_large());

consteval bool test_int128_construction_signed_negative() {
    const i128 value = -static_cast<i128>(make_u128(0x1234567890ABCDEFULL, 0x0123456789ABCDEFULL));
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    return static_cast<i128>(x) == value;
}
static_assert(test_int128_construction_signed_negative());

// ----- runtime tests -----

TEST(MultiLimbConstruction, FromUnsignedInt128SmallValueFitsInplace) {
    // Value statically requires multiple limbs, but compresses to a single limb at runtime.
    // Exercises the inplace-fit shortcut inside `assign_magnitude` when
    // `value_limbs > inplace_capacity`.
    const u128                       value = 42U;
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(x, 42U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(MultiLimbConstruction, FromUnsignedInt128LargeValueGrows) {
    // High limb is nonzero so the inplace shortcut cannot trigger on `big_int<64>`;
    // the constructor must `grow()` to hold both limbs.
    const u128                       value = make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_TRUE(x == value);
    EXPECT_EQ(static_cast<u128>(x), value);
    if constexpr (test_limb_bits == 64) {
        ASSERT_EQ(x.representation().size(), 2U);
        EXPECT_EQ(x.representation()[0], 0xFEDCBA9876543210ULL);
        EXPECT_EQ(x.representation()[1], 0xABCDEF0123456789ULL);
    }
}

TEST(MultiLimbConstruction, FromUnsignedInt128MaxValue) {
    const u128                       value = static_cast<u128>(-1);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<u128>(x), value);
}

TEST(MultiLimbConstruction, FromSignedInt128Negative) {
    const i128 value = -static_cast<i128>(make_u128(0x1234567890ABCDEFULL, 0x0123456789ABCDEFULL));
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<i128>(x), value);
}

TEST(MultiLimbConstruction, FromSignedInt128MinValue) {
    // i128 minimum: high limb is 0x8000...000, low limb zero.
    const i128                       value = static_cast<i128>(make_u128(0x8000000000000000ULL, 0));
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<i128>(x), value);
}

TEST(MultiLimbConstruction, FromSignedInt128Positive) {
    const i128 value = static_cast<i128>(make_u128(0x1234567890ABCDEFULL, 0x0123456789ABCDEFULL));
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<i128>(x), value);
}

TEST(MultiLimbConstruction, FromInt128InplacePathLargeBigInt) {
    // basic_big_int<256> has enough inplace capacity to hold a 128-bit value
    // (4 limbs on 64-bit limbs, 8 on 32-bit limbs), so `value_limbs <= inplace_capacity`
    // and `assign_magnitude` skips the grow check entirely.
    const u128                                  value = make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<256> x(value);
    EXPECT_EQ(static_cast<u128>(x), value);
    EXPECT_TRUE(is_inplace(x));
}

TEST(MultiLimbConstruction, AssignFromUnsignedInt128) {
    const u128                       value = make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = value;
    EXPECT_EQ(static_cast<u128>(x), value);
}

TEST(MultiLimbConstruction, AssignFromSignedInt128Negative) {
    const i128 value = -static_cast<i128>(make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL));
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = value;
    EXPECT_EQ(static_cast<i128>(x), value);
}

TEST(MultiLimbConstruction, AssignOverwritesWithLargerInt128) {
    // Start with a small one-limb value, then assign a multi-limb value.
    BEMAN_BIG_INT_NAMESPACE::big_int x(1);
    const u128                       value = make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    x                                      = value;
    EXPECT_EQ(static_cast<u128>(x), value);
}

TEST(MultiLimbConstruction, ConstructFromInt128WithAllocator) {
    std::allocator<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t> a;
    const u128                       value = make_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value, a);
    EXPECT_EQ(static_cast<u128>(x), value);
}

    #endif // BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL

// =============================================================================
// _BitInt
// =============================================================================

    #ifdef BEMAN_BIG_INT_HAS_BITINT

        #if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 96

using bui96 = bit_uint<96>;
using bi96  = bit_int<96>;

// ----- compile-time tests -----

consteval bool test_bitint96_construction() {
    const bui96 value = (static_cast<bui96>(0xCAFEBABEU) << 64) | static_cast<bui96>(0xDEADBEEF12345678ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    return static_cast<bui96>(x) == value;
}
static_assert(test_bitint96_construction());

// ----- runtime tests -----

TEST(MultiLimbConstruction, FromUnsignedBitInt96Small) {
    // 96-bit static width forces the multi-limb path on 32- and 64-bit limbs;
    // small magnitude exercises the trim/inplace-shortcut.
    const bui96                      value = 42U;
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(x, 42U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(MultiLimbConstruction, FromUnsignedBitInt96Large) {
    // Set bits in the upper portion to force multi-limb storage on 64-bit limbs.
    const bui96 value = (static_cast<bui96>(0xCAFEBABEU) << 64) | static_cast<bui96>(0xDEADBEEF12345678ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_TRUE(x == value);
    EXPECT_EQ(static_cast<bui96>(x), value);
}

TEST(MultiLimbConstruction, FromUnsignedBitInt96Max) {
    const bui96                      value = static_cast<bui96>(-1);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bui96>(x), value);
}

TEST(MultiLimbConstruction, FromSignedBitInt96Negative) {
    const bi96                       value = -static_cast<bi96>(0x123456789ABCDEFULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bi96>(x), value);
}

TEST(MultiLimbConstruction, FromSignedBitInt96LargeNegative) {
    // Lower 64 bits set; magnitude is wider than one 64-bit limb.
    const bi96                       value = -static_cast<bi96>(static_cast<bui96>(0xFFFFFFFFFFFFFFFFULL) << 16);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bi96>(x), value);
}

TEST(MultiLimbConstruction, ConstructFromSignedBitInt96WithAllocator) {
    // The allocator-taking constructor is a separate overload from the one the
    // tests above exercise, and it has to classify a bit-precise operand as
    // signed the same way. `std::is_signed_v` is not specified for `_BitInt`, so
    // a standard library that answers false for it would send a signed operand
    // down the unsigned path.
    std::allocator<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t> a;
    const bi96                       value = -static_cast<bi96>(static_cast<bui96>(0xFFFFFFFFFFFFFFFFULL) << 16);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value, a);
    EXPECT_EQ(static_cast<bi96>(x), value);
    EXPECT_LT(x, 0);
}

TEST(MultiLimbConstruction, AssignFromUnsignedBitInt96) {
    const bui96 value = (static_cast<bui96>(0xCAFEBABEU) << 64) | static_cast<bui96>(0xDEADBEEF12345678ULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = value;
    EXPECT_EQ(static_cast<bui96>(x), value);
}

        #endif // _BitInt(96)

        #if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 192

using bui192 = bit_uint<192>;
using bi192  = bit_int<192>;

// ----- compile-time tests -----

consteval bool test_bitint192_construction() {
    const bui192                     value = (static_cast<bui192>(1) << 191) | static_cast<bui192>(0xDEADBEEFULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    return static_cast<bui192>(x) == value;
}
static_assert(test_bitint192_construction());

// ----- runtime tests -----

TEST(MultiLimbConstruction, FromUnsignedBitInt192) {
    const bui192 value = (static_cast<bui192>(1) << 191) | (static_cast<bui192>(0xCAFEBABEU) << 64) |
                         static_cast<bui192>(0xDEADBEEFULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bui192>(x), value);
}

TEST(MultiLimbConstruction, FromUnsignedBitInt192Max) {
    const bui192                     value = static_cast<bui192>(-1);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bui192>(x), value);
}

TEST(MultiLimbConstruction, FromUnsignedBitInt192InplacePath) {
    // basic_big_int<192> has 3 inplace limbs on 64-bit limbs (6 on 32-bit),
    // so a 192-bit value satisfies `value_limbs <= inplace_capacity`
    // and the constructor never grows.
    const bui192 value = (static_cast<bui192>(1) << 191) | (static_cast<bui192>(0xCAFEBABEU) << 64) |
                         static_cast<bui192>(0xDEADBEEFULL);
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<192> x(value);
    EXPECT_EQ(static_cast<bui192>(x), value);
    EXPECT_TRUE(is_inplace(x));
}

TEST(MultiLimbConstruction, FromSignedBitInt192Negative) {
    const bi192 value = -static_cast<bi192>((static_cast<bui192>(1) << 130) | static_cast<bui192>(0xDEADBEEFULL));
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bi192>(x), value);
}

TEST(MultiLimbConstruction, FromSignedBitInt192Min) {
    // i192 minimum: -2^191.
    const bi192                      value = -(static_cast<bi192>(1) << 190) - (static_cast<bi192>(1) << 190);
    BEMAN_BIG_INT_NAMESPACE::big_int x(value);
    EXPECT_EQ(static_cast<bi192>(x), value);
}

TEST(MultiLimbConstruction, AssignFromUnsignedBitInt192) {
    const bui192 value = (static_cast<bui192>(1) << 191) | (static_cast<bui192>(0xCAFEBABEU) << 64) |
                         static_cast<bui192>(0xDEADBEEFULL);
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = value;
    EXPECT_EQ(static_cast<bui192>(x), value);
}

        #endif // _BitInt(192)

    #endif // BEMAN_BIG_INT_HAS_BITINT

} // namespace

#endif // clang-19 guard
