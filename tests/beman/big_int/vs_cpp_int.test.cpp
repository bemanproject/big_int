// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"

#include <cstddef>
#include <functional>

#include "testing.hpp"

namespace {

namespace bmp = ::BEMAN_BIG_INT_NAMESPACE::boost_mp_testing;
using bmp::check_cpp_int_equal;
using bmp::random_big_int;

// ----- specific-value tests -----
//
// Good for pinning down regressions at known-interesting inputs
// (all-ones magnitudes, single bits high up, values that straddle a limb
// boundary, etc.) without relying on the random sweep to happen to hit them.

TEST(VsCppInt, AddSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "1", "1"));
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "ffffffffffffffff", "1"));                 // limb carry out
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "ffffffffffffffffffffffffffffffff", "1")); // two-limb carry out
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "-deadbeef", "deadbeef"));                 // cancel to zero
    EXPECT_TRUE(check_cpp_int_equal(
        std::plus<>{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "1234567890abcdeffedcba9876543210"));
}

TEST(VsCppInt, SubSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "1", "1"));
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "10000000000000000", "1")); // borrow through limb
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "1", "10000000000000000")); // negative result
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "-ffffffff", "-ffffffff")); // negative cancel
    EXPECT_TRUE(check_cpp_int_equal(
        std::minus<>{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "1234567890abcdeffedcba9876543210"));
}

TEST(VsCppInt, MulSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "0", "deadbeef"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "ffffffff", "ffffffff"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "ffffffffffffffff", "ffffffffffffffff"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "-abcdef", "123456"));
    EXPECT_TRUE(check_cpp_int_equal(
        std::multiplies<>{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "1234567890abcdeffedcba9876543210"));
}

// `gcd` is found by argument-dependent lookup for both backends: ours for
// beman::big_int, boost::multiprecision's for cpp_int. Both are documented to
// return the non-negative gcd of the magnitudes, with gcd(0, 0) == 0.
struct gcd_op {
    template <class T>
    [[nodiscard]] T operator()(const T& lhs, const T& rhs) const {
        return gcd(lhs, rhs);
    }
};

TEST(VsCppInt, GcdSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "0", "0"));
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "0", "deadbeef"));
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "deadbeef", "0"));
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "1", "deadbeefcafebabe"));
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "-deadbeef", "deadbeef"));            // sign is dropped
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "ffffffffffffffff", "ffffffffffff")); // single-limb operands
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "10000000000000000", "100000000"));   // powers of two
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "ffffffffffffffffffffffffffffffff", "ffffffffffffffff")); // 2^n - 1 pair
    EXPECT_TRUE(check_cpp_int_equal(
        gcd_op{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "1234567890abcdeffedcba9876543210"));
    // A common factor planted in both operands, and a wide size gap.
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, "24936d5b6f95ea1d800", "36d5b6f95ea1d8000"));
    EXPECT_TRUE(check_cpp_int_equal(gcd_op{}, random_big_int(4096), random_big_int(64)));
}

// `lcm` likewise resolves per backend. Both are documented to return the
// non-negative least common multiple of the magnitudes, with a zero operand
// giving zero.
struct lcm_op {
    template <class T>
    [[nodiscard]] T operator()(const T& lhs, const T& rhs) const {
        return lcm(lhs, rhs);
    }
};

TEST(VsCppInt, LcmSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "0", "0"));
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "0", "deadbeef"));
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "deadbeef", "0"));
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "1", "deadbeefcafebabe"));
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "-deadbeef", "deadbeef"));            // sign is dropped
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "ffffffffffffffff", "ffffffffffff")); // single-limb operands
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "ffffffffffffffff", "3"));            // result crosses a limb
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "10000000000000000", "100000000"));   // powers of two
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "ffffffffffffffffffffffffffffffff", "ffffffffffffffff")); // 2^n - 1 pair
    EXPECT_TRUE(check_cpp_int_equal(
        lcm_op{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "1234567890abcdeffedcba9876543210"));
    // A common factor planted in both operands, and a wide size gap.
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, "24936d5b6f95ea1d800", "36d5b6f95ea1d8000"));
    EXPECT_TRUE(check_cpp_int_equal(lcm_op{}, random_big_int(4096), random_big_int(64)));
}

// ----- random parity sweep -----
//
// Bit widths chosen to exercise a spread of limb counts:
//   32 bits -> single small limb
//  160 bits -> a few limbs
//  800 bits -> ~13 limbs, result of mul spans ~26 limbs
// 2000 bits -> large multi-limb, clearly heap-allocated

constexpr std::size_t kBitWidths[]   = {32, 160, 800, 2000};
constexpr std::size_t kTrialsPerSize = 25;

template <class BinOp>
void run_parity_sweep(BinOp op) {
    for (const std::size_t lhs_bits : kBitWidths) {
        for (const std::size_t rhs_bits : kBitWidths) {
            for (std::size_t trial = 0; trial < kTrialsPerSize; ++trial) {
                const bool lhs_neg = (trial & 0b01) != 0;
                const bool rhs_neg = (trial & 0b10) != 0;
                ASSERT_TRUE(
                    check_cpp_int_equal(op, random_big_int(lhs_bits, lhs_neg), random_big_int(rhs_bits, rhs_neg)))
                    << "lhs_bits=" << lhs_bits << " rhs_bits=" << rhs_bits << " trial=" << trial;
            }
        }
    }
}

TEST(VsCppInt, AddRandom) { run_parity_sweep(std::plus<>{}); }
TEST(VsCppInt, SubRandom) { run_parity_sweep(std::minus<>{}); }
TEST(VsCppInt, MulRandom) { run_parity_sweep(std::multiplies<>{}); }
TEST(VsCppInt, DivRandom) { run_parity_sweep(std::divides<>{}); }
TEST(VsCppInt, ModRandom) { run_parity_sweep(std::modulus<>{}); }
TEST(VsCppInt, GcdRandom) { run_parity_sweep(gcd_op{}); }
TEST(VsCppInt, LcmRandom) { run_parity_sweep(lcm_op{}); }

// Exercises zero-operand corner cases, which the random sweep is unlikely to
// hit on its own.
TEST(VsCppInt, ZeroCornerCases) {
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "0", "0"));
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "0"));
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "0", "deadbeefcafebabef00dfacec0ffee1234567890abcdef"));
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "deadbeefcafebabef00dfacec0ffee1234567890abcdef", "0"));
}

} // namespace
