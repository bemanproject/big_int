// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Benchmark for tuning fast_input_basecase_chunks (and, in a later phase, the
// FastIntegerOutput thresholds). Three sweeps:
//   crossover  - at 2G and 4G chunks, the pure fused-Horner basecase
//                (override = whole input) against the ladder splitting at G
//                (override = G). The threshold is the first G where splitting
//                wins; crossovers are sawtoothed (recursion-depth
//                boundaries), so points report the MIN over repetitions.
//   curve      - digits_to_limbs with tuned defaults vs the pure basecase vs
//                the public from_chars, log-spaced base-10 sizes.
//   lone-top   - 2^k vs 2^k + 1 chunks: the cost of Algorithm 1.25's
//                unmultiplied lone top element (balanced-splitting probe).
//   bases      - spot bases: even bases ride the trimmed-power trick, odd
//                bases are the control.
//
// Output-vs-input ratio (2026-06-12, tuned configs): 2.7x on the M4-class
// box and 2.3x on the i9-11900K at 10M digits - the y-cruncher
// two-products-per-level expectation. A Bouvier-Zimmermann scaled remainder
// tree (division-free output, MCA exercise 1.35) is the known next step if
// that ratio ever matters: their integer-conversion crossover was ~250k
// limbs, so prototype the middle-product level (multiply_mod_bnm1 wraparound
// machinery) against the preinv Barrett march before committing.
//
// Disabled by default. To run:
//   cmake --preset appleclang-release (build dir carries
//     CMAKE_CXX_FLAGS=-DBEMAN_BIG_INT_RUN_BENCHMARKS=1)
//   cmake --build build/appleclang-release
//   ./build/appleclang-release/tests/beman/big_int/beman.big_int.tests.base_conversion_bench
//
// Output is CSV on stdout: kernel,base,digits,chunks,ns_per_op.

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/base_conversion.hpp>

#include <gtest/gtest.h>

#include "benchmark_testing.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace local {

namespace detail = ::BEMAN_BIG_INT_NAMESPACE::detail;
using uint_t     = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using stopwatch  = ::BEMAN_BIG_INT_NAMESPACE::benchmark_testing::stopwatch;

inline constexpr unsigned reps_per_point    = 5;
inline constexpr unsigned samples_per_point = 3;

// NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
static std::mt19937_64 rng{0xBA5EULL};

std::vector<unsigned char> random_digits(const std::size_t len, const int base) {
    std::uniform_int_distribution<int> dist(0, base - 1);
    std::vector<unsigned char>         digits(len);
    for (auto& d : digits) {
        d = static_cast<unsigned char>(dist(rng));
    }
    if (digits[0] == 0) {
        digits[0] = 1;
    }
    return digits;
}

// Per digit sample: `make(digits)` builds the timed closure (untimed
// per-sample setup such as string or limb conversion), then the minimum over
// reps_per_point repetitions of `iters` calls is taken; across samples: the
// median. `iters` self-scales off a warmup shot so every repetition lands in
// the low milliseconds.
template <class MakeFn>
double measure_ns(const std::size_t len, const int base, MakeFn make) {
    std::array<double, samples_per_point> sample_ns{};
    for (unsigned sample = 0; sample < samples_per_point; ++sample) {
        const auto digits = random_digits(len, base);
        auto       fn     = make(digits);

        const stopwatch warmup{};
        fn();
        const double   shot  = std::max(stopwatch::elapsed_time<double>(warmup), 1.0e-9);
        const unsigned iters = static_cast<unsigned>(std::clamp(0.005 / shot, 1.0, 5000.0));

        double best = 1.0e300;
        for (unsigned rep = 0; rep < reps_per_point; ++rep) {
            const stopwatch sw{};
            for (unsigned i = 0; i < iters; ++i) {
                fn();
            }
            best = std::min(best, stopwatch::elapsed_time<double>(sw) / iters);
        }
        sample_ns[sample] = best;
    }
    std::ranges::sort(sample_ns);
    return sample_ns[samples_per_point / 2] * 1.0e9;
}

std::string ascii_of(const std::vector<unsigned char>& digits) {
    std::string text(digits.size(), '0');
    for (std::size_t i = 0; i < digits.size(); ++i) {
        const auto d = digits[i];
        text[i]      = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    return text;
}

// Output benches run on the value of the random digit string so input and
// output rows at the same (base, digits) describe the same number.
std::vector<uint_t> value_of_digits(const std::vector<unsigned char>& digits, const int base) {
    std::vector<uint_t>    limbs(detail::base_conversion_limb_bound(digits.size(), base), 0);
    std::allocator<uint_t> alloc;
    const std::size_t      count =
        detail::digits_to_limbs(std::span<uint_t>{limbs}, std::span<const unsigned char>{digits}, base, alloc);
    limbs.resize(count);
    return limbs;
}

double run_fast_at(const std::size_t len, const int base, const std::size_t basecase_override) {
    return measure_ns(len, base, [&](const std::vector<unsigned char>& digits) {
        auto out = std::make_shared<std::vector<uint_t>>(detail::base_conversion_limb_bound(len, base), 0);
        return [&digits, &base, &basecase_override, out]() {
            std::allocator<uint_t> alloc;
            detail::digits_to_limbs(
                std::span<uint_t>{*out}, std::span<const unsigned char>{digits}, base, alloc, basecase_override);
        };
    });
}

double run_from_chars_at(const std::size_t len, const int base) {
    return measure_ns(len, base, [&](const std::vector<unsigned char>& digits) {
        auto text = std::make_shared<std::string>(ascii_of(digits));
        return [text, &base]() {
            ::BEMAN_BIG_INT_NAMESPACE::big_int            value;
            [[maybe_unused]] const std::from_chars_result result =
                ::BEMAN_BIG_INT_NAMESPACE::from_chars(text->data(), text->data() + text->size(), value, base);
        };
    });
}

double run_fast_out_at(const std::size_t len, const int base, const std::size_t basecase_override) {
    return measure_ns(len, base, [&](const std::vector<unsigned char>& digits) {
        auto value = std::make_shared<std::vector<uint_t>>(value_of_digits(digits, base));
        auto out   = std::make_shared<std::vector<unsigned char>>(
            detail::base_conversion_digit_bound(std::span<const uint_t>{*value}, base), 0);
        return [value, out, &base, &basecase_override]() {
            std::allocator<uint_t>             alloc;
            [[maybe_unused]] const std::size_t count = detail::limbs_to_digits(
                std::span<unsigned char>{*out}, std::span<const uint_t>{*value}, base, alloc, basecase_override);
        };
    });
}

double run_to_chars_at(const std::size_t len, const int base) {
    return measure_ns(len, base, [&](const std::vector<unsigned char>& digits) {
        auto                                          value = std::make_shared<::BEMAN_BIG_INT_NAMESPACE::big_int>();
        const auto                                    text  = ascii_of(digits);
        [[maybe_unused]] const std::from_chars_result parsed =
            ::BEMAN_BIG_INT_NAMESPACE::from_chars(text.data(), text.data() + text.size(), *value, base);
        auto out = std::make_shared<std::vector<char>>(digits.size() + 8, '\0');
        return [value, out, &base]() {
            [[maybe_unused]] const std::to_chars_result result =
                ::BEMAN_BIG_INT_NAMESPACE::to_chars(out->data(), out->data() + out->size(), *value, base);
        };
    });
}

void emit(const char* kernel, const int base, const std::size_t len, const double ns) {
    const std::size_t chunks = detail::base_conversion_chunk_count(len, base);
    std::cout << kernel << ',' << base << ',' << len << ',' << chunks << ',' << std::fixed << std::setprecision(1)
              << ns << '\n';
}

void run_sweep() {
    std::cout << "kernel,base,digits,chunks,ns_per_op\n";
    const int  base = 10;
    const auto cpl  = static_cast<std::size_t>(detail::limb_max_input_digits(base));

    // One large round-trip guards the preinv Barrett tier, which only
    // engages at bench scales: digits -> limbs -> digits must be identity.
    {
        const auto                 digits = random_digits(3000000, base);
        const auto                 value  = value_of_digits(digits, base);
        std::vector<unsigned char> out(detail::base_conversion_digit_bound(std::span<const uint_t>{value}, base), 0);
        std::allocator<uint_t>     alloc;
        const std::size_t          count =
            detail::limbs_to_digits(std::span<unsigned char>{out}, std::span<const uint_t>{value}, base, alloc);
        out.resize(count);
        if (out != digits) {
            std::cout << "FATAL: 3M-digit round-trip mismatch\n";
            std::abort();
        }
    }

    // Crossover: pure basecase vs one and two ladder levels at the same size.
    for (const std::size_t group : {std::size_t{8},
                                    std::size_t{16},
                                    std::size_t{32},
                                    std::size_t{64},
                                    std::size_t{128},
                                    std::size_t{256},
                                    std::size_t{512},
                                    std::size_t{1024}}) {
        for (const std::size_t mult : {std::size_t{2}, std::size_t{4}}) {
            const std::size_t chunks = mult * group;
            const std::size_t len    = chunks * cpl;
            emit(mult == 2 ? "basecase_2g" : "basecase_4g", base, len, run_fast_at(len, base, chunks));
            emit(mult == 2 ? "split_2g" : "split_4g", base, len, run_fast_at(len, base, group));
            std::cout.flush();
        }
    }

    // Headline curves.
    for (const std::size_t len : {std::size_t{1000},
                                  std::size_t{3000},
                                  std::size_t{10000},
                                  std::size_t{30000},
                                  std::size_t{100000},
                                  std::size_t{300000},
                                  std::size_t{1000000},
                                  std::size_t{3000000},
                                  std::size_t{10000000}}) {
        emit("input_fast", base, len, run_fast_at(len, base, 0));
        if (len <= 300000) {
            emit("input_basecase", base, len, run_fast_at(len, base, std::size_t{1} << 30));
        }
        if (len <= 10000000) {
            emit("input_from_chars", base, len, run_from_chars_at(len, base));
        }
        std::cout.flush();
    }

    // Lone-top probe: the odd element passes through every level unmultiplied.
    for (const std::size_t k : {std::size_t{10}, std::size_t{12}}) {
        const std::size_t pow2 = std::size_t{1} << k;
        emit("lone_top_even", base, pow2 * cpl, run_fast_at(pow2 * cpl, base, 0));
        emit("lone_top_odd", base, (pow2 + 1) * cpl, run_fast_at((pow2 + 1) * cpl, base, 0));
        std::cout.flush();
    }

    // Base spread: even bases (trimmed powers) vs odd controls. For the
    // before/after charconv-integration sweep we emit both kernel directions
    // and the public from_chars/to_chars at moderate sizes so the speedup
    // table spans bases, not just base 10. (1M is the long pole for the naive
    // output baseline; per-base stops there -- base 10 carries 3M/10M.)
    for (const int spot_base : {3, 7, 10, 26, 36}) {
        for (const std::size_t len : {std::size_t{10000}, std::size_t{100000}, std::size_t{1000000}}) {
            emit("input_fast", spot_base, len, run_fast_at(len, spot_base, 0));
            emit("output_fast", spot_base, len, run_fast_out_at(len, spot_base, 0));
            emit("input_from_chars", spot_base, len, run_from_chars_at(len, spot_base));
            emit("output_to_chars", spot_base, len, run_to_chars_at(len, spot_base));
            std::cout.flush();
        }
    }

    // Output crossover: pure short-division basecase vs one and two split
    // levels at the same size (tunes fast_output_basecase_chunks).
    for (const std::size_t group : {std::size_t{4},
                                    std::size_t{8},
                                    std::size_t{16},
                                    std::size_t{32},
                                    std::size_t{64},
                                    std::size_t{128},
                                    std::size_t{256}}) {
        for (const std::size_t mult : {std::size_t{2}, std::size_t{4}}) {
            const std::size_t chunks = mult * group;
            const std::size_t len    = chunks * cpl;
            emit(mult == 2 ? "out_basecase_2g" : "out_basecase_4g",
                 base,
                 len,
                 run_fast_out_at(len, base, std::size_t{1} << 30));
            emit(mult == 2 ? "out_split_2g" : "out_split_4g", base, len, run_fast_out_at(len, base, group));
            std::cout.flush();
        }
    }

    // Output headline curves (compare with the input rows above for the
    // div-vs-mul ratio).
    for (const std::size_t len : {std::size_t{1000},
                                  std::size_t{3000},
                                  std::size_t{10000},
                                  std::size_t{30000},
                                  std::size_t{100000},
                                  std::size_t{300000},
                                  std::size_t{1000000},
                                  std::size_t{3000000},
                                  std::size_t{10000000}}) {
        emit("output_fast", base, len, run_fast_out_at(len, base, 0));
        if (len <= 100000) {
            emit("output_basecase", base, len, run_fast_out_at(len, base, std::size_t{1} << 30));
        }
        if (len <= 3000000) {
            emit("output_to_chars", base, len, run_to_chars_at(len, base));
        }
        std::cout.flush();
    }
}

// Input-gate crossover probe (run in isolation: --gtest_filter=*InputXover*).
// from_chars under the compiled gate vs the kernel-only ceiling at fine sizes
// spanning the sub-gate band. Build with
// -DBEMAN_BIG_INT_INPUT_CHARCONV_MIN_CHUNKS=2 (forces kernel+glue) and with a
// huge value (forces the inline baseline), then compare xover_from_chars across
// the two runs to find where the whole conversion first beats the inline loop.
void run_input_xover() {
    std::cout << "kernel,base,digits,chunks,ns_per_op\n";
    const int base = 10;
    for (const std::size_t len : {std::size_t{19},
                                  std::size_t{38},
                                  std::size_t{57},
                                  std::size_t{76},
                                  std::size_t{95},
                                  std::size_t{133},
                                  std::size_t{190},
                                  std::size_t{285},
                                  std::size_t{500},
                                  std::size_t{1000},
                                  std::size_t{2000},
                                  std::size_t{5000}}) {
        emit("xover_from_chars", base, len, run_from_chars_at(len, base));
        emit("xover_fast", base, len, run_fast_at(len, base, 0));
        std::cout.flush();
    }
}

// Power-of-two coverage (run in isolation: --gtest_filter=*Po2Bench*). po2 bases
// never touch the sub-quadratic kernels; from_chars/to_chars handle them with
// O(n) bit-oriented chunk loops. Bases 2 and 16 exercise the max_pow == 0 path
// (one digit block maps to exactly one limb), 8 and 32 the general is_pow_2 path
// (funnel-shift writes). to_chars emits digits with direct shift/mask; from_chars
// currently parses each chunk with std::from_chars -- this sweep measures whether
// that costs against the bit-aligned ideal.
void run_po2_sweep() {
    std::cout << "kernel,base,digits,chunks,ns_per_op\n";
    for (const int base : {2, 8, 16, 32}) {
        for (const std::size_t len : {std::size_t{1000},
                                      std::size_t{10000},
                                      std::size_t{100000},
                                      std::size_t{1000000},
                                      std::size_t{10000000}}) {
            emit("po2_from_chars", base, len, run_from_chars_at(len, base));
            emit("po2_to_chars", base, len, run_to_chars_at(len, base));
            std::cout.flush();
        }
    }
}

} // namespace local

TEST(BaseConversion, InputBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_sweep();
    SUCCEED();
#else
    GTEST_SKIP() << "Benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}

TEST(BaseConversion, InputXover) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_input_xover();
    SUCCEED();
#else
    GTEST_SKIP() << "Benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}

TEST(BaseConversion, Po2Bench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_po2_sweep();
    SUCCEED();
#else
    GTEST_SKIP() << "Benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
