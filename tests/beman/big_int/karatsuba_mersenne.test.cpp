// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <gtest/gtest.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <span>

#include "testing.hpp"

namespace local {

auto run_one_mersenne(const unsigned p2) -> void {
    using cpp_int_type =
        boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;
    using big_int_type = BEMAN_BIG_INT_NAMESPACE::big_int;

    // Mersenne prime (2^1398269 - 1) approx. 8.147175644125731*10^420920

    // Mathematica:
    //
    // 2^1398269 - 1
    // N[%]

    const cpp_int_type cpp_int_two{2};
    const cpp_int_type cpp_int_mersenne{cpp_int_type{BEMAN_BIG_INT_NAMESPACE::pow(cpp_int_two, p2)} - 1};

    const big_int_type big_int_two{2};
    const big_int_type big_int_mersenne{big_int_type{BEMAN_BIG_INT_NAMESPACE::pow(big_int_two, p2)} - 1};

    const std::span<const ::boost::multiprecision::limb_type> cpp_int_rep{cpp_int_mersenne.backend().limbs(),
                                                                          cpp_int_mersenne.backend().size()};
    const auto big_int_bytes = std::as_bytes(big_int_mersenne.representation());
    const auto cpp_int_bytes = std::as_bytes(cpp_int_rep);

    const auto big_int_sig = BEMAN_BIG_INT_NAMESPACE::significant_byte_len(big_int_bytes);
    const auto cpp_int_sig = BEMAN_BIG_INT_NAMESPACE::significant_byte_len(cpp_int_bytes);

    const bool result_length_is_ok{big_int_sig == cpp_int_sig};

    EXPECT_TRUE(result_length_is_ok);

    bool result_is_ok{result_length_is_ok};

    bool result_bytes_same_is_ok{false};

    if (result_is_ok) {
        for (std::size_t i = 0; i < big_int_sig; ++i) {
            if (big_int_bytes[i] != cpp_int_bytes[i]) {
                result_bytes_same_is_ok = false;
                break;
            } else {
                result_bytes_same_is_ok = true;
            }
        }
    }

    EXPECT_TRUE(result_bytes_same_is_ok);

    result_is_ok = (result_bytes_same_is_ok && result_is_ok);

    EXPECT_TRUE(result_is_ok);
}

// Note: We test millons of decimal digits, since
// Mathematica:
//   N[2^20996011 - 1]
//   1.259768954503301*10^6320429

constexpr std::array<unsigned, std::size_t{7}> my_mersenne_powers_of_two{
    1257787U, 1398269U, 2976221U, 3021377U, 6972593U, 13466917U, 20996011U};
} // namespace local

// See also https://oeis.org/A057429 sequences of Mersenne primes.

TEST(Multiplication, KaratsubaMersenne00) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{0}]);
}

TEST(Multiplication, KaratsubaMersenne01) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{1}]);
}

TEST(Multiplication, KaratsubaMersenne02) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{2}]);
}

TEST(Multiplication, KaratsubaMersenne03) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{3}]);
}

TEST(Multiplication, KaratsubaMersenne04) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{4}]);
}

TEST(Multiplication, KaratsubaMersenne05) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{5}]);
}

TEST(Multiplication, KaratsubaMersenne06) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{6}]);
}
