// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using BEMAN_BIG_INT_NAMESPACE::detail::int_multiprecision_t;

TEST(BitShift, LeftShiftBasic) {
    big_int x{0};
    big_int y{1};
    big_int z{1};
    big_int w{-1};
    big_int q{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 0;
    y <<= 0;
    z <<= 1;
    w <<= 1;
    q <<= 1;

    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 1);
    EXPECT_EQ(z, 2);
    EXPECT_EQ(w, -2);
    EXPECT_EQ(q.representation().size(), 2U);
    EXPECT_EQ(q.representation()[0], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(q.representation()[1], 1ULL);
}

TEST(BitShift, LeftShiftAcrossLimbs) {
    big_int x{1};
    big_int y{1};
    big_int z{1};
    big_int expected{1};

    x <<= 64;
    y <<= 65;
    z <<= 128;
    expected <<= 64;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 1ULL);
    EXPECT_EQ(x, expected);

    EXPECT_EQ(y.representation().size(), 2U);
    EXPECT_EQ(y.representation()[0], 0ULL);
    EXPECT_EQ(y.representation()[1], 2ULL);

    EXPECT_EQ(z.representation().size(), 3U);
    EXPECT_EQ(z.representation()[0], 0ULL);
    EXPECT_EQ(z.representation()[1], 0ULL);
    EXPECT_EQ(z.representation()[2], 1ULL);
}

TEST(BitShift, LeftShiftAllocatedValue) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 1;
    x <<= 64;

    EXPECT_EQ(x.representation().size(), 3U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(x.representation()[2], 1ULL);
    EXPECT_FALSE(is_inplace(x));
}

TEST(BitShift, RightShiftBasic) {
    big_int x{0};
    big_int y{1};
    big_int z{2};
    big_int w{-1};
    big_int q{-2};

    x >>= 0;
    y >>= 0;
    z >>= 1;
    w >>= 1;
    q >>= 1;

    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 1);
    EXPECT_EQ(z, 1);
    EXPECT_EQ(w, -1);
    EXPECT_EQ(q, -1);
}

TEST(BitShift, RightShiftAcrossLimbs) {
    big_int x{1};
    big_int y{1};
    big_int z{1};

    x <<= 64;
    y <<= 65;
    z <<= 128;

    x >>= 64;
    y >>= 64;
    z >>= 128;

    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 2);
    EXPECT_EQ(z, 1);
}

TEST(BitShift, RightShiftNegativeRounding) {
    big_int x{-3};
    big_int y{-4};
    big_int z{std::numeric_limits<uint_multiprecision_t>::max()};
    z = -z;

    x >>= 1;
    y >>= 1;
    z >>= 64;

    EXPECT_EQ(x, -2);
    EXPECT_EQ(y, -2);
    EXPECT_EQ(z, -1);
}

TEST(BitShift, LeftShiftZeroRemainsZero) {
    big_int x{0};

    x <<= 1;
    x <<= 64;
    x <<= 129;

    EXPECT_EQ(x, 0);
    EXPECT_GE(x.representation().size(), 1U);
}

TEST(BitShift, LeftShiftWholeLimbPreservesMagnitudeDigits) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 64;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], std::numeric_limits<uint_multiprecision_t>::max());

    x <<= 64;

    EXPECT_EQ(x.representation().size(), 3U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 0ULL);
    EXPECT_EQ(x.representation()[2], std::numeric_limits<uint_multiprecision_t>::max());
}

TEST(BitShift, RightShiftCrossLimbBitPropagation) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 64;
    x >>= 1;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0x8000'0000'0000'0000ULL);
    EXPECT_EQ(x.representation()[1], 0x7FFF'FFFF'FFFF'FFFFULL);
}

TEST(BitShift, RightShiftLargePositiveBecomesZero) {
    big_int x{1};

    x >>= 64;
    EXPECT_EQ(x, 0);

    x = 1;
    x >>= 257;
    EXPECT_EQ(x, 0);
}

TEST(BitShift, RightShiftLargeNegativeBecomesMinusOne) {
    big_int x{-1};
    big_int y{-2};

    x >>= 64;
    y >>= 64;

    EXPECT_EQ(x, -1);
    EXPECT_EQ(y, -1);

    y = -2;
    y >>= 257;
    EXPECT_EQ(y, -1);
}

TEST(BitShift, LeftThenRightRoundTripForPositive) {
    big_int x{123'456'789};

    x <<= 17;
    x >>= 17;
    EXPECT_EQ(x, 123'456'789);

    x <<= 64;
    x >>= 64;
    EXPECT_EQ(x, 123'456'789);

    x <<= 130;
    x >>= 130;
    EXPECT_EQ(x, 123'456'789);
}

// ----- operator<< (non-mutating left shift) -----

TEST(BitShift, OperatorLeftShiftBasic) {
    const big_int x{1};
    const big_int r = x << 1;
    EXPECT_EQ(r, 2);
    EXPECT_EQ(x, 1); // original unchanged
}

TEST(BitShift, OperatorLeftShiftZeroShift) {
    const big_int x{42};
    const big_int r = x << 0;
    EXPECT_EQ(r, 42);
}

TEST(BitShift, OperatorLeftShiftZeroValue) {
    const big_int x{0};
    const big_int r = x << 100;
    EXPECT_EQ(r, 0);
}

TEST(BitShift, OperatorLeftShiftNegative) {
    const big_int x{-1};
    const big_int r = x << 1;
    EXPECT_EQ(r, -2);

    const big_int y{-3};
    const big_int s = y << 2;
    EXPECT_EQ(s, -12);
}

TEST(BitShift, OperatorLeftShiftMatchesCompound) {
    // operator<< on an lvalue should produce the same result as <<=
    const big_int x{123'456'789};
    big_int       y{123'456'789};

    const big_int r = x << 17;
    y <<= 17;
    EXPECT_EQ(r, y);

    const big_int a{std::numeric_limits<uint_multiprecision_t>::max()};
    big_int       b{std::numeric_limits<uint_multiprecision_t>::max()};

    const big_int r2 = a << 65;
    b <<= 65;
    EXPECT_EQ(r2, b);
}

TEST(BitShift, OperatorLeftShiftRvalue) {
    // rvalue path: should move the storage, no copy
    big_int r = big_int{1} << 10;
    EXPECT_EQ(r, 1024);

    big_int s = big_int{std::numeric_limits<uint_multiprecision_t>::max()} << 1;
    EXPECT_EQ(s.representation().size(), 2U);
    EXPECT_EQ(s.representation()[0], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(s.representation()[1], 1ULL);
}

TEST(BitShift, OperatorLeftShiftInplaceToHeap) {
    // default big_int has inplace_capacity=1 (one 64-bit limb).
    // Shifting 1 left by 63 bits: top limb = 0x8000...0, CLZ = 0.
    // A further shift by 1 bit must overflow into a second limb (heap).
    const big_int x{1ULL << 63};
    EXPECT_TRUE(is_inplace(x)); // inline

    const big_int r = x << 1;
    EXPECT_EQ(r.representation().size(), 2U);
    EXPECT_EQ(r.representation()[0], 0ULL);
    EXPECT_EQ(r.representation()[1], 1ULL);
}

TEST(BitShift, OperatorLeftShiftInplaceStaysInlineCLZ) {
    // Value 1 has CLZ = 63 in a 64-bit limb. Shifting left by up to 63 bits
    // should NOT overflow into a new limb, staying in inline storage.
    const big_int x{1};
    EXPECT_TRUE(is_inplace(x)); // inline

    const big_int r = x << 63;
    EXPECT_TRUE(is_inplace(r)); // still inline
    EXPECT_EQ(r.representation().size(), 1U);
    EXPECT_EQ(r.representation()[0], 1ULL << 63);
}

TEST(BitShift, OperatorLeftShiftWiderInplaceStaysInline) {
    // basic_big_int<256> has 4 inline limbs (256 bits). A small value shifted
    // within those 256 bits should never touch the heap.
    using big_int_256 = basic_big_int<256>;
    const big_int_256 x{1};
    EXPECT_TRUE(is_inplace(x));

    const big_int_256 r = x << 255;
    EXPECT_TRUE(is_inplace(r)); // still inline

    // Top limb should be 1 << (255 % 64) = 1 << 63
    EXPECT_EQ(r.representation().size(), 4U);
    EXPECT_EQ(r.representation()[3], 1ULL << 63);
    EXPECT_EQ(r.representation()[2], 0ULL);
    EXPECT_EQ(r.representation()[1], 0ULL);
    EXPECT_EQ(r.representation()[0], 0ULL);
}

TEST(BitShift, OperatorLeftShiftWiderInplaceToHeap) {
    // basic_big_int<256>: 4 inline limbs. Shifting 1 by 256 bits requires
    // 5 limbs, which must spill to the heap.
    using big_int_256 = basic_big_int<256>;
    const big_int_256 x{1};

    const big_int_256 r = x << 256;
    EXPECT_EQ(r.representation().size(), 5U);
    EXPECT_FALSE(is_inplace(r)); // heap
    EXPECT_EQ(r.representation()[4], 1ULL);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(r.representation()[static_cast<std::size_t>(i)], 0ULL);
    }
}

TEST(BitShift, OperatorLeftShiftAcrossLimbs) {
    const big_int x{1};

    const big_int r64  = x << 64;
    const big_int r65  = x << 65;
    const big_int r128 = x << 128;

    EXPECT_EQ(r64.representation().size(), 2U);
    EXPECT_EQ(r64.representation()[0], 0ULL);
    EXPECT_EQ(r64.representation()[1], 1ULL);

    EXPECT_EQ(r65.representation().size(), 2U);
    EXPECT_EQ(r65.representation()[0], 0ULL);
    EXPECT_EQ(r65.representation()[1], 2ULL);

    EXPECT_EQ(r128.representation().size(), 3U);
    EXPECT_EQ(r128.representation()[0], 0ULL);
    EXPECT_EQ(r128.representation()[1], 0ULL);
    EXPECT_EQ(r128.representation()[2], 1ULL);
}

TEST(BitShift, OperatorLeftShiftHuge) {
    // Shift a small value by a large amount (10000 bits).
    const big_int x{1};
    const big_int r = x << 10000;

    // 10000 / 64 = 156 whole limbs, 10000 % 64 = 16-bit shift
    EXPECT_EQ(r.representation().size(), 157U);
    EXPECT_EQ(r.representation()[156], 1ULL << 16);
    EXPECT_EQ(r.representation()[155], 0ULL);
    EXPECT_EQ(r.representation()[0], 0ULL);

    // Verify via round-trip with >>=
    big_int rt = r;
    rt >>= 10000;
    EXPECT_EQ(rt, 1);
}

TEST(BitShift, OperatorLeftShiftHeapSource) {
    // Source already on the heap; operator<< should pre-allocate headroom
    // so shift_left doesn't reallocate.
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    x <<= 1; // now 2 limbs, on heap
    EXPECT_FALSE(is_inplace(x));

    const big_int r = x << 64;
    EXPECT_EQ(r.representation().size(), 3U);
    EXPECT_EQ(r.representation()[0], 0ULL);
    EXPECT_EQ(r.representation()[1], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(r.representation()[2], 1ULL);
}

TEST(BitShift, OperatorLeftShiftSignedShiftAmount) {
    const big_int x{1};
    const big_int r = x << 10;
    EXPECT_EQ(r, 1024);
}

TEST(BitShift, OperatorLeftShiftPreservesOriginal) {
    // Verify operator<< doesn't modify the source even when the source
    // is large and heap-allocated.
    const big_int x     = big_int{1} << 200;
    const auto    x_rep = x.representation();

    const big_int r = x << 50;
    EXPECT_EQ(x.representation().size(), x_rep.size());
    for (std::size_t i = 0; i < x_rep.size(); ++i) {
        EXPECT_EQ(x.representation()[i], x_rep[i]);
    }

    // And the result should match doing it in two steps
    big_int expected{1};
    expected <<= 250;
    EXPECT_EQ(r, expected);
}

// ----- operator>> (non-mutating right shift) -----

TEST(BitShift, OperatorRightShiftBasic) {
    const big_int x{4};
    const big_int r = x >> 1;
    EXPECT_EQ(r, 2);
    EXPECT_EQ(x, 4); // original unchanged
}

TEST(BitShift, OperatorRightShiftZeroShift) {
    const big_int x{42};
    const big_int r = x >> 0;
    EXPECT_EQ(r, 42);
}

TEST(BitShift, OperatorRightShiftZeroValue) {
    const big_int x{0};
    const big_int r = x >> 100;
    EXPECT_EQ(r, 0);
}

TEST(BitShift, OperatorRightShiftToZero) {
    const big_int x{1};
    const big_int r = x >> 1;
    EXPECT_EQ(r, 0);

    const big_int s = x >> 64;
    EXPECT_EQ(s, 0);

    const big_int t = x >> 1000;
    EXPECT_EQ(t, 0);
}

TEST(BitShift, OperatorRightShiftNegativeRounding) {
    // Right shift of negative values rounds toward negative infinity.
    const big_int x{-3};
    const big_int r = x >> 1;
    EXPECT_EQ(r, -2);

    const big_int y{-4};
    const big_int s = y >> 1;
    EXPECT_EQ(s, -2);

    const big_int w{-1};
    const big_int t = w >> 64;
    EXPECT_EQ(t, -1);
}

TEST(BitShift, OperatorRightShiftMatchesCompound) {
    const big_int x = big_int{1} << 200;
    big_int       y = big_int{1} << 200;

    const big_int r = x >> 130;
    y >>= 130;
    EXPECT_EQ(r, y);

    big_int expected{1};
    expected <<= 70;
    EXPECT_EQ(r, expected);
}

TEST(BitShift, OperatorRightShiftRvalue) {
    big_int r = big_int{1024} >> 5;
    EXPECT_EQ(r, 32);

    // Rvalue from a heap source
    big_int r2 = (big_int{1} << 128) >> 64;
    big_int expected{1};
    expected <<= 64;
    EXPECT_EQ(r2, expected);
}

TEST(BitShift, OperatorRightShiftAcrossLimbs) {
    const big_int x = big_int{1} << 128; // 3 limbs
    EXPECT_EQ(x.representation().size(), 3U);

    const big_int r = x >> 64;
    EXPECT_EQ(r.representation().size(), 2U);
    EXPECT_EQ(r.representation()[0], 0ULL);
    EXPECT_EQ(r.representation()[1], 1ULL);

    const big_int s = x >> 128;
    EXPECT_EQ(s, 1);
}

TEST(BitShift, OperatorRightShiftFullDiscardSkipsCopy) {
    // When the shift discards every significant bit of the source, the
    // result is constructed directly without copying, so it does not
    // inherit the source's heap buffer.
    big_int x{1};
    x <<= 128; // 3 limbs, on heap
    EXPECT_FALSE(is_inplace(x));

    const big_int r = x >> 200;
    EXPECT_EQ(r, 0);
    EXPECT_TRUE(is_inplace(r));
}

TEST(BitShift, OperatorRightShiftPreservesOriginal) {
    const big_int x     = big_int{1} << 200;
    const auto    x_rep = x.representation();

    const big_int r = x >> 50;
    // Original unchanged
    EXPECT_EQ(x.representation().size(), x_rep.size());
    for (std::size_t i = 0; i < x_rep.size(); ++i) {
        EXPECT_EQ(x.representation()[i], x_rep[i]);
    }

    big_int expected{1};
    expected <<= 150;
    EXPECT_EQ(r, expected);
}

TEST(BitShift, OperatorRightShiftSignedShiftAmount) {
    const big_int x{1024};
    const big_int r = x >> 5;
    EXPECT_EQ(r, 32);
}

TEST(BitShift, OperatorLeftRightRoundTrip) {
    // Non-mutating round trip: (x << n) >> n == x for positive values
    const big_int x{123'456'789};

    EXPECT_EQ((x << 17) >> 17, x);
    EXPECT_EQ((x << 64) >> 64, x);
    EXPECT_EQ((x << 130) >> 130, x);
}

TEST(BitShift, LValueBiggerShiftThanRep) {
    // If we are shifting more bits than the bitwidth of an lvalue,
    // we can skip making a copy altogether.
    const big_int x = big_int{1} << 200;
    const big_int r = x >> 10000;

    EXPECT_EQ(r, big_int{0});
    EXPECT_TRUE(is_inplace(r)); // No allocation should have been made
}

TEST(BitShift, LValueBiggerShiftThanRepNegative) {
    // For a negative value, we should retain the sign
    const big_int x = -(big_int{1} << 200);
    const big_int r = x >> 10000;

    EXPECT_EQ(r, -1);
    EXPECT_TRUE(is_inplace(r));
}

TEST(BitShift, LValuePartialLimbSkip) {
    // Whole-limb shift of a positive, heap-allocated source: the low limbs
    // are dropped without being copied.
    const big_int x = (big_int{1} << 128) + (big_int{1} << 64) + big_int{1}; // limbs [1, 1, 1]
    EXPECT_EQ(x.representation().size(), 3U);

    const big_int r        = x >> 64;
    const big_int expected = (big_int{1} << 64) + big_int{1}; // [1, 1]
    EXPECT_EQ(r, expected);
    EXPECT_EQ(r.representation().size(), 2U);
}

TEST(BitShift, LValuePartialLimbSkipResidualBits) {
    // Mixed whole-limb + residual-bit shift: combine a `shifted_limbs > 0`
    // skip with a small bit-level shift on the surviving tail.
    big_int x{1};
    x <<= 130; // single bit at position 130; limbs [0, 0, 4]

    const big_int r = x >> 65; // should equal 1 << 65
    big_int       expected{1};
    expected <<= 65;
    EXPECT_EQ(r, expected);
}

TEST(BitShift, LValuePartialLimbSkipNegativeExact) {
    // Negative, partial-limb shift where every discarded bit is zero
    const big_int x = -(big_int{1} << 128);
    const big_int r = x >> 64;
    big_int       expected{1};
    expected <<= 64;
    expected = -expected;
    EXPECT_EQ(r, expected); // exact: -(2^128) / 2^64 == -(2^64)
}

TEST(BitShift, LValuePartialLimbSkipNegativeWholeLimbRounding) {
    // Negative value where a wholly discarded lower limb has a non-zero bit.
    // We must account for these bits
    const big_int x = -((big_int{1} << 128) + big_int{1}); // limbs [1, 0, 1], negative

    const big_int r        = x >> 64; // -(2^128 + 1) / 2^64 rounds to -(2^64 + 1)
    const big_int expected = -((big_int{1} << 64) + big_int{1});
    EXPECT_EQ(r, expected);
}

TEST(BitShift, LValuePartialLimbSkipNegativeResidualRounding) {
    // Negative source where the rounding-trigger bit is in the low portion
    // of the first surviving limb. `shift_right`'s own mask check covers
    // this case; make sure we don't double-decrement.
    big_int x{3};
    x <<= 64; // limbs [0, 3]
    x = -x;   // -3 * 2^64

    const big_int r = x >> 65; // -3 * 2^64 / 2^65 = -1.5 rounds to -2
    EXPECT_EQ(r, -2);
}

TEST(BitShift, LValueBitShiftDropsTopLimb) {
    // (2^64) >> 1 == 2^63: the source is on the heap but the result can be inlined
    // Should not allocate
    const big_int x = big_int{1} << 64;
    EXPECT_FALSE(is_inplace(x));

    const big_int r        = x >> 1;
    const big_int expected = big_int{1} << 63;
    EXPECT_EQ(r, expected);
    EXPECT_TRUE(is_inplace(r));
}

TEST(BitShift, LValueBitShiftDropsTopLimbNegative) {
    // Same situation as above but with a negative source whose discarded
    // bit forces a rounding decrement: -(2^64 + 1) >> 1 == -(2^63 + 1).
    const big_int x = -((big_int{1} << 64) + big_int{1});

    const big_int r        = x >> 1;
    const big_int expected = -((big_int{1} << 63) + big_int{1});
    EXPECT_EQ(r, expected);
    EXPECT_TRUE(is_inplace(r));
}

TEST(BitShift, LeftShiftSmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int s = 0; s <= 10; ++s) {
            const big_int bx{x};

            EXPECT_EQ(big_int{x} << s, x << s);
            EXPECT_EQ(bx << s, x << s);
        }
    }
}

TEST(BitShift, RightShiftSmallConsistencyWithInt) {
    for (int x = -10; x <= 10; ++x) {
        for (int s = 0; s <= 10; ++s) {
            const big_int bx{x};

            EXPECT_EQ(big_int{x} >> s, x >> s);
            EXPECT_EQ(bx >> s, x >> s);
        }
    }
}

} // namespace
