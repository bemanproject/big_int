// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

TEST(IntegerAssignment, AssignPositive) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = 42;
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(IntegerAssignment, AssignNegative) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = -42;
    // TODO(alcxpr): how can we test sign?
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(IntegerAssignment, AssignZero) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(42);
    x = 0;
    EXPECT_EQ(x.representation()[0], 0U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(IntegerAssignment, AssignUnsigned) {
    BEMAN_BIG_INT_NAMESPACE::big_int x;
    x = 1000000000000000ULL;
    EXPECT_EQ(x.representation()[0], 1000000000000000ULL);
}

TEST(IntegerAssignment, AssignOverwritesLargerValue) {
    BEMAN_BIG_INT_NAMESPACE::big_int x(static_cast<double>(1ULL << 63) * 4.0);
    x = 1;
    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_EQ(x.representation()[0], 1U);
}

TEST(IntegerAssignment, AssignCrossAllocator) {
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<256> src(42);
    BEMAN_BIG_INT_NAMESPACE::big_int            dst;
    dst = src;
    EXPECT_EQ(dst.representation()[0], 42U);
}

TEST(IntegerAssignment, AssignCrossInplaceBitsForcesReallocation) {
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<64>  x(1);
    BEMAN_BIG_INT_NAMESPACE::basic_big_int<256> big(static_cast<double>(1ULL << 63) * 4.0); // 2 limbs via ctor
    x = big;
    EXPECT_EQ(x.representation().size(), 2U);
}
