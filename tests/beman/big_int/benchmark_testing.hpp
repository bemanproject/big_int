// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BENCHMARK_TESTING_HPP
#define BEMAN_BIG_INT_BENCHMARK_TESTING_HPP

#include <cstdint>
#include <ctime>

#include <beman/big_int/detail/config.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace benchmark_testing {

struct stopwatch {
  public:
    using time_point_type = std::uintmax_t;

    void reset() { m_start = now(); }

    template <class Time>
    BEMAN_BIG_INT_NOINLINE static Time elapsed_time(const stopwatch& my_stopwatch) noexcept {
        return Time{static_cast<Time>(my_stopwatch.elapsed()) / Time{1'000'000'000}};
    }

    template <class Time = double, class F>
    BEMAN_BIG_INT_NOINLINE static Time measure_time(const F& f) {
        const time_point_type start = now();
        f();
        const time_point_type end = now();
        return static_cast<Time>(end - start) / Time{1'000'000'000};
    }

  private:
    time_point_type m_start{now()}; // NOLINT(readability-identifier-naming)

    BEMAN_BIG_INT_NOINLINE [[nodiscard]] static time_point_type now() {
        timespec ts{};

        const int ntsp{timespec_get(&ts, TIME_UTC)};
        BEMAN_BIG_INT_ASSERT(ntsp);

        return static_cast<time_point_type>(ts.tv_sec) * time_point_type{1'000'000'000} +
               static_cast<time_point_type>(ts.tv_nsec);
    }

    [[nodiscard]] time_point_type elapsed() const {
        const time_point_type stop{now()};

        const time_point_type elapsed_ns{stop - m_start};

        return elapsed_ns;
    }
};

} // namespace benchmark_testing
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_BENCHMARK_TESTING_HPP
