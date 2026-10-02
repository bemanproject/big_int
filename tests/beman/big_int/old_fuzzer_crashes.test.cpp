// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "testing.hpp"
#include <functional>

using ::BEMAN_BIG_INT_NAMESPACE::boost_mp_testing::check_cpp_int_equal;

TEST(OldFuzzerCrashes, Div1) {
    EXPECT_TRUE(check_cpp_int_equal(std::divides<>{},
                                    "010000000000000000141400000000000000000000000000000000ffffffff0000",
                                    "00000000000000000065000000000000000000000000000000000000000000141402"));
}

TEST(OldFuzzerCrashes, Div2) {
    EXPECT_TRUE(check_cpp_int_equal(
        std::divides<>{}, "0100000000002400000000000000000000", "0000000000e7000000010000003100162400"));
}
