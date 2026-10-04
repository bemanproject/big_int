// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <boost/multiprecision/cpp_int.hpp>
#include <beman/big_int/big_int.hpp>
#include <gtest/gtest.h>
#include <span>

#include "benchmark_testing.hpp"
#include "testing.hpp"

namespace local {
template <typename BigIntType>
BigIntType fibonacci(unsigned int n) {
    if (n == 0)
        return BigIntType(0);
    if (n == 1)
        return BigIntType(1);

    BigIntType a = 0;
    BigIntType b = 1;

    for (unsigned int i = 2; i <= n; ++i) {
        BigIntType next = a + b;
        a               = b;
        b               = next;
    }

    return b;
}

} // namespace local

bool run_benchmarks() {
    using cpp_int_type =
        boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;
    using big_int_type = BEMAN_BIG_INT_NAMESPACE::big_int;

    using local_stopwatch_type = BEMAN_BIG_INT_NAMESPACE::benchmark_testing::stopwatch;

    local_stopwatch_type my_stopwatch{};

    const big_int_type big_int_fibonacci{local::fibonacci<big_int_type>(1000000)};
    std::cout << "stopwatch big_int: " << local_stopwatch_type::elapsed_time<double>(my_stopwatch) << std::endl;

    my_stopwatch.reset();

    const cpp_int_type cpp_int_fibonacci{local::fibonacci<cpp_int_type>(1000000)};
    std::cout << "stopwatch cpp_int: " << local_stopwatch_type::elapsed_time<double>(my_stopwatch) << std::endl;

    const std::span<const ::boost::multiprecision::limb_type> cpp_int_rep{cpp_int_fibonacci.backend().limbs(),
                                                                          cpp_int_fibonacci.backend().size()};
    const auto big_int_bytes = std::as_bytes(big_int_fibonacci.representation());
    const auto cpp_int_bytes = std::as_bytes(cpp_int_rep);

    const auto big_int_sig = BEMAN_BIG_INT_NAMESPACE::significant_byte_len(big_int_bytes);
    const auto cpp_int_sig = BEMAN_BIG_INT_NAMESPACE::significant_byte_len(cpp_int_bytes);

    const bool result_length_is_ok{big_int_sig == cpp_int_sig};

    std::cout << "result_length_is_ok    : " << result_length_is_ok << std::endl;

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

    std::cout << "result_bytes_same_is_ok: " << result_bytes_same_is_ok << std::endl;

    result_is_ok = (result_bytes_same_is_ok && result_is_ok);

    std::cout << "result_is_ok           : " << result_is_ok << std::endl;

    return result_is_ok;
}

TEST(Addition, AdditionBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    EXPECT_TRUE(run_benchmarks());
#else
    GTEST_SKIP() << "Benchmarks not run" << std::endl;
#endif
}
