// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Micro-benchmarks for the division kernels: submul_single_limb throughput
// at production-realistic span lengths, the dependent-chain latencies of the
// preinv quotient steps and reciprocals, whole-call short division, and the
// wrapped-product-to-full-product ratio. These are the before/after numbers
// for the Phase D scalar and architecture work.
//
// Disabled by default; define BEMAN_BIG_INT_RUN_BENCHMARKS to run.
// Output is CSV on stdout: kernel,param,value (ns per limb, call, or digit;
// the mulmod rows report the wrapped/full ratio in the value column).

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#include <gtest/gtest.h>

#include "benchmark_testing.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace local {

using uint_t           = ::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using stopwatch        = ::BEMAN_BIG_INT_NAMESPACE::benchmark_testing::stopwatch;
using std_allocator    = std::allocator<uint_t>;
using scratch_for_test = ::BEMAN_BIG_INT_NAMESPACE::detail::scratch_allocator<std_allocator>;

inline constexpr unsigned reps_per_point = 5;

constexpr uint_t top_bit = uint_t{1} << (::BEMAN_BIG_INT_NAMESPACE::detail::width_v<uint_t> - 1);

// NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
static std::mt19937_64 rng{0xd0ULL};

inline void fill_random(const std::span<uint_t> dest) {
    std::uniform_int_distribution<uint_t> dist;
    for (auto& x : dest) {
        x = dist(rng);
    }
    if (!dest.empty() && dest.back() == 0) {
        dest.back() = 1;
    }
}

void emit(const char* kernel, const std::size_t param, const double value) {
    std::cout << kernel << ',' << param << ',' << std::fixed << std::setprecision(3) << value << '\n';
}

// Throughput of result -= a * val over n-limb spans, ns per limb.
double submul_ns_per_limb(const std::size_t n) {
    std::vector<uint_t> r(n);
    std::vector<uint_t> a(n);
    fill_random(r);
    fill_random(a);
    const auto     r_view = std::span<uint_t>{r};
    const auto     a_view = std::span<const uint_t>{a};
    const unsigned iters  = static_cast<unsigned>(std::clamp<std::size_t>(20'000'000 / n, 16, 2'000'000));

    uint_t sink = 1;
    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            sink ^= ::BEMAN_BIG_INT_NAMESPACE::detail::submul_single_limb(r_view, a_view, sink | 1u);
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    if (sink == 42u) { // defeat dead-code elimination
        std::cout << "#\n";
    }
    return best * 1.0e9 / (static_cast<double>(iters) * static_cast<double>(n));
}

// Dependent-chain latency of one 3/2 quotient step, ns per digit (each
// iteration consumes the previous remainder, like the real digit loop).
double div_3by2_chain_ns() {
    const uint_t d1 = (static_cast<uint_t>(rng()) | top_bit);
    const uint_t d0 = static_cast<uint_t>(rng());
    const uint_t v  = ::BEMAN_BIG_INT_NAMESPACE::detail::reciprocal_word_3by2(d1, d0);

    uint_t         u2    = d1 - 1;
    uint_t         u1    = static_cast<uint_t>(rng());
    uint_t         u0    = static_cast<uint_t>(rng());
    const unsigned iters = 400'000;

    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            const auto step = ::BEMAN_BIG_INT_NAMESPACE::detail::div_3by2_preinv(u2, u1, u0, d1, d0, v);
            u2              = step.remainder.high_bits; // remainder < d keeps the precondition
            u1              = step.remainder.low_bits;
            u0 ^= step.quotient;
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    if ((u2 ^ u1 ^ u0) == 42u) {
        std::cout << "#\n";
    }
    return best * 1.0e9 / iters;
}

// Dependent-chain latency of the reciprocal setups, ns per call. The loop
// counter is folded into the chain so a fixed point cannot make the call
// loop-invariant (hoistable).
template <class F>
double reciprocal_chain_ns(F f) {
    uint_t         d     = static_cast<uint_t>(rng()) | top_bit;
    const unsigned iters = 200'000;

    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            d = (f(d) ^ static_cast<uint_t>(i)) | top_bit | 1u;
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    [[maybe_unused]] volatile uint_t sink = d; // the chain result must be observed
    return best * 1.0e9 / iters;
}

// Whole divide_unsigned_short calls (fresh divisor each call, like real
// small public divisions), ns per call.
double short_division_ns(const std::size_t m) {
    std::vector<uint_t> dividend(m);
    fill_random(dividend);
    std::vector<uint_t> quotient(m);
    uint_t              divisor = static_cast<uint_t>(rng()) | 1u;
    const unsigned      iters   = 200'000;

    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            const uint_t r = ::BEMAN_BIG_INT_NAMESPACE::detail::divide_unsigned_short(
                std::span<uint_t>{quotient}, std::span<const uint_t>{dividend}, divisor);
            divisor = (divisor ^ (r << 1)) | (uint_t{1} << 9) | 1u;
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    if (divisor == 42u) {
        std::cout << "#\n";
    }
    return best * 1.0e9 / iters;
}

// Wrapped product (multiply_mod_bnm1 at w = next_size(n + 1)) versus the
// full 2n-limb product of the same operands: the ratio the cyclic NTT entry
// is meant to push toward ~0.5 at FFT sizes.
double mulmod_ratio(const std::size_t n) {
    const std::size_t w = ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_mod_bnm1_next_size(
        n + 1, ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_mod_bnm1_cutoff);
    std::vector<uint_t> a(n);
    std::vector<uint_t> b(n);
    fill_random(a);
    fill_random(b);
    std::vector<uint_t> full(2 * n, 0);
    std::vector<uint_t> wrapped(w, 0);
    const auto          a_view = std::span<const uint_t>{a};
    const auto          b_view = std::span<const uint_t>{b};
    std_allocator       alloc;
    const unsigned      iters = static_cast<unsigned>(std::clamp<std::size_t>(3'000'000 / n, 1, 64));

    double best_full = 1.0e300;
    double best_mm   = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        {
            const stopwatch sw{};
            for (unsigned i = 0; i < iters; ++i) {
                std::ranges::fill(full, uint_t{0});
                ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_dispatch(std::span<uint_t>{full}, a_view, b_view, alloc);
            }
            best_full = std::min(best_full, stopwatch::elapsed_time<double>(sw));
        }
        {
            scratch_for_test scratch(::BEMAN_BIG_INT_NAMESPACE::detail::multiply_mod_bnm1_storage_size(w), alloc);
            const stopwatch  sw{};
            for (unsigned i = 0; i < iters; ++i) {
                ::BEMAN_BIG_INT_NAMESPACE::detail::multiply_mod_bnm1(
                    std::span<uint_t>{wrapped}, a_view, b_view, scratch);
            }
            best_mm = std::min(best_mm, stopwatch::elapsed_time<double>(sw));
        }
    }
    return best_mm / best_full;
}

// Direct cyclic NTT kernel (workspaces allocated per call, mirroring the
// production tier) versus the CRT split forced via cutoff_override, at the
// same chooser wrap size. The crossover locates fft_cyclic_cutoff; sizes
// below the linear FFT cutoff are probed too, because the cyclic transform
// can pay before the full product does.
double cyclic_over_crt(const std::size_t min_w, std::size_t& wrap_out) {
    namespace bd = ::BEMAN_BIG_INT_NAMESPACE::detail;

    const bd::fft_cyclic_params params = bd::multiply_fft_cyclic_next_size(min_w);
    const std::size_t           w      = params.wrap_limbs;
    wrap_out                           = w;

    std::vector<uint_t> a(w);
    std::vector<uint_t> b(w);
    fill_random(a);
    fill_random(b);
    std::vector<uint_t> out(w, 0);
    const auto          a_view = std::span<const uint_t>{a};
    const auto          b_view = std::span<const uint_t>{b};
    std_allocator       alloc;
    const unsigned      iters = static_cast<unsigned>(std::clamp<std::size_t>(2'000'000 / w, 1, 64));

    double best_cyc = 1.0e300;
    double best_crt = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        {
            const stopwatch sw{};
            for (unsigned i = 0; i < iters; ++i) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                std::vector<double>        fp_ws(bd::fft_cyclic_fp_storage_size(params));
                std::vector<std::uint64_t> int_ws(bd::fft_cyclic_int_storage_size(params));
                bd::multiply_fft_cyclic(std::span<uint_t>{out},
                                        a_view,
                                        b_view,
                                        params,
                                        std::span<double>{fp_ws},
                                        std::span<std::uint64_t>{int_ws});
#else
                std::vector<std::uint64_t> ws(bd::fft_cyclic_storage_size(params));
                bd::multiply_fft_cyclic(std::span<uint_t>{out}, a_view, b_view, params, std::span<std::uint64_t>{ws});
#endif
            }
            best_cyc = std::min(best_cyc, stopwatch::elapsed_time<double>(sw));
        }
        {
            scratch_for_test scratch(bd::multiply_mod_bnm1_storage_size(w), alloc);
            const stopwatch  sw{};
            for (unsigned i = 0; i < iters; ++i) {
                bd::multiply_mod_bnm1(std::span<uint_t>{out}, a_view, b_view, scratch, bd::multiply_mod_bnm1_cutoff);
            }
            best_crt = std::min(best_crt, stopwatch::elapsed_time<double>(sw));
        }
    }
    return best_cyc / best_crt;
}

// Quotient-only division over full divmod at a balanced dc-band shape
// (m = 2n by n): the S1 divappr acceptance metric. Median over operand
// samples of min-of-reps ratios, matching the stress bench discipline.
double div_q_ratio(const std::size_t n) {
    constexpr int samples = 5;
    std_allocator alloc;

    double ratios[samples];
    for (int sample = 0; sample < samples; ++sample) {
        std::vector<uint_t> a(2 * n);
        std::vector<uint_t> b(n);
        fill_random(a);
        fill_random(b);
        const auto          a_view = std::span<const uint_t>{a};
        const auto          b_view = std::span<const uint_t>{b};
        std::vector<uint_t> q(n + 1, 0);
        std::vector<uint_t> r(2 * n + 1, 0);

        double best_qr = 1.0e300;
        double best_q  = 1.0e300;
        for (unsigned rep = 0; rep < 3; ++rep) {
            {
                const stopwatch sw{};
                ::BEMAN_BIG_INT_NAMESPACE::detail::divide_burnikel_ziegler(
                    std::span<uint_t>{q}, std::span<uint_t>{r}, a_view, b_view, alloc);
                best_qr = std::min(best_qr, stopwatch::elapsed_time<double>(sw));
            }
            {
                const stopwatch sw{};
                ::BEMAN_BIG_INT_NAMESPACE::detail::divide_quotient(std::span<uint_t>{q}, a_view, b_view, alloc);
                best_q = std::min(best_q, stopwatch::elapsed_time<double>(sw));
            }
        }
        ratios[sample] = best_q / best_qr;
    }
    std::sort(ratios, ratios + samples);
    return ratios[samples / 2];
}

void run_all() {
    std::cout << "kernel,param,value\n";

    for (const std::size_t n : {std::size_t{8},
                                std::size_t{16},
                                std::size_t{32},
                                std::size_t{40},
                                std::size_t{64},
                                std::size_t{256},
                                std::size_t{4096}}) {
        emit("submul_ns_per_limb", n, submul_ns_per_limb(n));
        std::cout.flush();
    }

    emit("div_3by2_chain_ns", 0, div_3by2_chain_ns());
    emit("reciprocal_word_ns", 0, reciprocal_chain_ns([](const uint_t d) {
             return ::BEMAN_BIG_INT_NAMESPACE::detail::reciprocal_word(d) ^ d;
         }));
    emit("reciprocal_word_3by2_ns", 0, reciprocal_chain_ns([](const uint_t d) {
             return ::BEMAN_BIG_INT_NAMESPACE::detail::reciprocal_word_3by2(d, ~d) ^ d;
         }));
    std::cout.flush();

    for (const std::size_t m : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{6}, std::size_t{8}}) {
        emit("short_division_ns", m, short_division_ns(m));
    }
    std::cout.flush();

    for (const std::size_t n : {std::size_t{4096}, std::size_t{16384}, std::size_t{65536}, std::size_t{262144}}) {
        emit("mulmod_over_full", n, mulmod_ratio(n));
        std::cout.flush();
    }

    for (const std::size_t n : {std::size_t{512},
                                std::size_t{1024},
                                std::size_t{2048},
                                std::size_t{4096},
                                std::size_t{8192},
                                std::size_t{16384},
                                std::size_t{32768}}) {
        emit("div_q_over_div_qr", n, div_q_ratio(n));
        std::cout.flush();
    }

    // The param column is the chooser wrap size the probe actually ran.
    // 1664, 3328, 6656, and 13312 are b = 26 band bottoms (the chooser's
    // least efficient coefficient width); the rest alternate b = 32 / b = 48
    // sizes.
    for (const std::size_t min_w : {std::size_t{512},
                                    std::size_t{1024},
                                    std::size_t{1536},
                                    std::size_t{1664},
                                    std::size_t{2048},
                                    std::size_t{3072},
                                    std::size_t{3328},
                                    std::size_t{4096},
                                    std::size_t{6144},
                                    std::size_t{6656},
                                    std::size_t{8192},
                                    std::size_t{12288},
                                    std::size_t{13312},
                                    std::size_t{16384},
                                    std::size_t{24576},
                                    std::size_t{32768}}) {
        std::size_t  w     = 0;
        const double ratio = cyclic_over_crt(min_w, w);
        emit("cyclic_over_crt", w, ratio);
        std::cout.flush();
    }
}

} // namespace local

TEST(Division, KernelMicroBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_all();
    SUCCEED();
#else
    GTEST_SKIP() << "Stress benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
