// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "testing.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <beman/big_int.hpp>
#include <gtest/gtest.h>
#include <array>
#include <span>

namespace local {

auto run_one_mersenne(const unsigned p2) -> void {
    using cpp_int_type =
        boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;
    using big_int_type = BEMAN_BIG_INT_NAMESPACE::big_int;

    // Mersenne value (2^p2 - 1) computed via square-and-multiply pow().
    // The exponents below produce limb counts well above the Toom-Cook 3
    // cutoff (detail::toom_cook_3_cutoff) at multiple recursion depths.

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

// Mersenne primes large enough to drive multiple Toom-Cook 3 recursion levels.
// At 64-bit limbs, p2/64 limbs are needed; with the toom_cook_3_cutoff of a few
// hundred limbs, Toom-3 still recurses several levels for these exponents
// (e.g. 13466917 bits = ~210k limbs, recurses ~5-6 levels before falling
// through to Karatsuba). Dropped the two largest exponents from the
// karatsuba-era list to keep test wall time under control now that each
// Toom-3 step does more work below the higher cutoff.
constexpr std::array<unsigned, std::size_t{2}> my_mersenne_powers_of_two{13466917U, 20996011U};

} // namespace local

// See also https://oeis.org/A057429 sequences of Mersenne primes.

TEST(Multiplication, ToomCook3Mersenne00) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{0}]);
}

TEST(Multiplication, ToomCook3Mersenne01) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{1}]);
}
