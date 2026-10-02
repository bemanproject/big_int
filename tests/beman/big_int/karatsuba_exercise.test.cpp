// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//

#include "boost_mp_testing.hpp"

#include <gtest/gtest.h>

#include "testing.hpp"

namespace bmp = ::BEMAN_BIG_INT_NAMESPACE::boost_mp_testing;

namespace local {

namespace detail {

using random_engine_length_type =
    ::std::linear_congruential_engine<::std::uint32_t, UINT32_C(48271), UINT32_C(0), UINT32_C(2147483647)>;

random_engine_length_type generator_limb_length{static_cast<typename random_engine_length_type::result_type>(42)};

std::uniform_int_distribution distribution_limb_length{std::size_t{UINT8_C(48)}, std::size_t{UINT8_C(128)}};

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

TEST(Multiplication, KaratsubaExercise01) {
    // For future reference, include a sample Mathematica session that
    // performs a big_int multiplication. It uses hexadecimal inputs
    // and also presents the result in hexadecimal format.

    // Mathematica:
    //
    // a = FromDigits["cb2cf1421408c408b8e1c6841d3bbb12", 16]
    // b = FromDigits["d35253e128902afb74af7110e78c0a4e388181820f00a5b6", 16]
    // a b
    // IntegerString[%, 16]
    //

    constexpr unsigned trials{256U};

    for (unsigned index{0U}; index < trials; ++index) {
        static_cast<void>(index);

        local::test_one_multiplication();
    }
}
