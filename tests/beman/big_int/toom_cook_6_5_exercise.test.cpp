// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "testing.hpp"
#include <gtest/gtest.h>

namespace bmp = ::BEMAN_BIG_INT_NAMESPACE::boost_mp_testing;

namespace local {

namespace detail {

using random_engine_length_type =
    ::std::linear_congruential_engine<::std::uint32_t, UINT32_C(48271), UINT32_C(0), UINT32_C(2147483647)>;

random_engine_length_type generator_limb_length{static_cast<typename random_engine_length_type::result_type>(67)};

// Sizes 3100..6500 sit above detail::toom_cook_6_5_cutoff on some configurations and below it on others;
// they exercise the algorithm where they clear the cutoff:
// balanced pairs enter Toom-6.5 directly, asymmetric pairs may fall back to
// Toom-4 (max > 7*k or min <= 5*k), so both paths get coverage.
std::uniform_int_distribution distribution_limb_length{std::size_t{UINT16_C(3100)}, std::size_t{UINT16_C(6500)}};

} // namespace detail

auto test_one_multiplication() -> void {
    constexpr std::size_t limb_bits{
        static_cast<std::size_t>(std::numeric_limits<::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits)};

    const std::size_t len_a_in_bits{detail::distribution_limb_length(detail::generator_limb_length) * limb_bits};
    const std::size_t len_b_in_bits{detail::distribution_limb_length(detail::generator_limb_length) * limb_bits};

    const std::string str_a{bmp::random_big_int(len_a_in_bits)};
    const std::string str_b{bmp::random_big_int(len_b_in_bits)};

    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, str_a, str_b));
}

} // namespace local

TEST(Multiplication, ToomCook6_5Exercise01) {
    // Trial count kept small because each multiplication operates on
    // large (3100-6500 limb) operands to clear the Toom-6.5 cutoff.
    constexpr unsigned trials{16U};

    for (unsigned index{0U}; index < trials; ++index) {
        static_cast<void>(index);

        local::test_one_multiplication();
    }
}
