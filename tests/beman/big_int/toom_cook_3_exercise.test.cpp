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

random_engine_length_type generator_limb_length{static_cast<typename random_engine_length_type::result_type>(53)};

// Sizes 810..1500 sit above detail::toom_cook_3_cutoff on some configurations and below it on others;
// they exercise the algorithm where they clear the cutoff:
// balanced pairs enter Toom-3 directly, asymmetric pairs may fall back to
// Karatsuba (min <= 2*k), so both paths get coverage.
std::uniform_int_distribution distribution_limb_length{std::size_t{UINT16_C(810)}, std::size_t{UINT16_C(1500)}};

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

TEST(Multiplication, ToomCook3Exercise01) {
    // Trial count reduced from 128 because each multiplication now operates
    // on much larger (810-1500 limb) operands to clear the raised cutoff.
    constexpr unsigned trials{32U};

    for (unsigned index{0U}; index < trials; ++index) {
        static_cast<void>(index);

        local::test_one_multiplication();
    }
}
