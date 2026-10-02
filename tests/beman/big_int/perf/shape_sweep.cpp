// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// End-to-end shape sweep for tuning: times multiplication, squaring and division through the public big_int
// operators (so the real dispatcher runs) over a list of operand shapes, and prints CSV on stdout. Every other line
// starts with '#'; the first is a '#const' line listing every tuning constant and the build configuration, which the
// scratch drivers (build/tune/bin/variant.sh, abab.sh) use to detect stale or mismatched binaries.
//
//   shape_sweep <mul|sqr|div|rem|divrem> [--path auto|sliced|unsliced|fft] [--rounds N] [--round-ms X] [--seed S]
//               [--gmp] [--grid FILE] [AxB ...]
//
// Shapes are limb counts: mul is la x lb, div/rem/divrem is dividend x divisor, sqr is N or NxN. --path
// sliced|unsliced|fft (mul only) calls the runtime kernels directly on raw limb spans; sliced and unsliced need the
// shape-aware dispatch (BEMAN_BIG_INT_HAS_SHAPE_DISPATCH), fft needs 64-bit limbs and, like the dispatcher, zeroes
// the result and allocates its workspaces inside the timed region. Every shape is checked against residues modulo
// two primes before it is timed and again on the last product of the timed loop. --gmp needs
// -DBEMAN_BIG_INT_SWEEP_GMP, 64-bit limbs and -lgmp; div and rem time only the named operator, so GMP's mpn_tdiv_qr
// (quotient and remainder) is paired with divrem only.
//
// Build through CMake (target beman.big_int.benchmarks.shape_sweep, BEMAN_BIG_INT_BUILD_BENCHMARKS=ON), which passes
// the library's PUBLIC definitions (BMI2_ADX, AVX512_IFMA, SIMD_MUL, FORCED_LIMB_WIDTH) and the compile flags. A
// manual build must reproduce that full set: the same BEMAN_BIG_INT_X86_64_BMI2_ADX / _AVX512_IFMA values, SIMD_MUL,
// BEMAN_BIG_INT_FORCED_LIMB_WIDTH, -march / -mavx2 style flags and NDEBUG as the library was built with (see the
// library target's entries in the build directory's compile_commands.json), and must pass
// -DBEMAN_BIG_INT_SWEEP_MANUAL_OK. Otherwise the harness TU can disagree with the library and #const would lie.
//   L=build/appleclang-release/libbeman.big_int.a
//   c++ -std=c++23 -O2 -DNDEBUG -DBEMAN_BIG_INT_SWEEP_MANUAL_OK -Iinclude -o shape_sweep $L
//       tests/beman/big_int/perf/shape_sweep.cpp    (one command; keep the source before or after $L as usual)

#if defined(BEMAN_BIG_INT_SWEEP_VIA_CMAKE)
    #include "shape_sweep_build_info.hpp"
#elif !defined(BEMAN_BIG_INT_SWEEP_MANUAL_OK)
    #error "build shape_sweep through CMake, or define BEMAN_BIG_INT_SWEEP_MANUAL_OK and match the library flags"
#endif

#include <beman/big_int.hpp>
#include <beman/big_int/detail/base_conversion.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/multiply_long_runtime.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/square_long_runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(BEMAN_BIG_INT_SWEEP_GMP)
static_assert(sizeof(unsigned long) == 8, "the GMP column assumes a 64-bit mp_limb_t");

// The x86 box has libgmp but no gmp.h, so declare the few mpn entry points ourselves.
extern "C" {
using mp_limb_t = unsigned long;
extern const char* const __gmp_version;
void                     __gmpn_mul(mp_limb_t* rp, const mp_limb_t* up, long un, const mp_limb_t* vp, long vn);
void                     __gmpn_sqr(mp_limb_t* rp, const mp_limb_t* up, long n);
void                     __gmpn_tdiv_qr(
    mp_limb_t* qp, mp_limb_t* rp, long qxn, const mp_limb_t* np, long nn, const mp_limb_t* dp, long dn);
}
#endif

namespace {

namespace bb = ::BEMAN_BIG_INT_NAMESPACE;
namespace dt = ::BEMAN_BIG_INT_NAMESPACE::detail;

using limb_t = bb::uint_multiprecision_t;
using u64    = std::uint64_t;

constexpr unsigned limb_bits = std::numeric_limits<limb_t>::digits;

// Residue primes below 2^31, so every product fits a u64 on any host.
constexpr u64 prime_a = 2147483647ULL; // 2^31 - 1
constexpr u64 prime_b = 2147483629ULL;

// ---------------------------------------------------------------------------
// The '#const' line.
// ---------------------------------------------------------------------------

template <class T>
void kv(std::string& s, const char* key, const T value) {
    s += ' ';
    s += key;
    s += '=';
    s += std::to_string(static_cast<unsigned long long>(value));
}

void kv_str(std::string& s, const char* key, std::string value) {
    std::ranges::replace(value, ' ', '_');
    if (value.empty()) {
        value = "-";
    }
    s += ' ';
    s += key;
    s += '=';
    s += value;
}

std::string compiler_string() {
#if defined(__apple_build_version__)
    return std::string("appleclang-") + __clang_version__;
#elif defined(__clang__)
    return std::string("clang-") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc-") + __VERSION__;
#elif defined(_MSC_VER)
    return "msvc-" + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

std::string const_line() {
    std::string s = "#const";

    // mul_impl.hpp
    kv(s, "square_long_cutoff", dt::square_long_cutoff);
    kv(s, "karatsuba_cutoff", dt::karatsuba_cutoff);
    kv(s, "karatsuba_fallback", dt::karatsuba_fallback);
    kv(s, "square_karatsuba_cutoff", dt::square_karatsuba_cutoff);
    kv(s, "toom_cook_3_cutoff", dt::toom_cook_3_cutoff);
    kv(s, "toom_cook_4_cutoff", dt::toom_cook_4_cutoff);
    kv(s, "toom_cook_6_5_cutoff", dt::toom_cook_6_5_cutoff);
    kv(s, "toom_cook_8_5_cutoff", dt::toom_cook_8_5_cutoff);
    kv(s, "square_toom_cook_3_cutoff", dt::square_toom_cook_3_cutoff);
    kv(s, "square_toom_cook_4_cutoff", dt::square_toom_cook_4_cutoff);
    kv(s, "square_toom_cook_6_5_cutoff", dt::square_toom_cook_6_5_cutoff);
    kv(s, "square_toom_cook_8_5_cutoff", dt::square_toom_cook_8_5_cutoff);
#if defined(BEMAN_BIG_INT_HAS_FFT_COST_MODEL)
    kv(s, "fft_mul_min_limbs", dt::fft_mul_min_limbs);
    kv(s, "fft_mul_model_num", dt::fft_mul_model_num);
    kv(s, "fft_mul_model_den", dt::fft_mul_model_den);
    kv(s, "square_fft_min_limbs", dt::square_fft_min_limbs);
    kv(s, "square_fft_model_num", dt::square_fft_model_num);
    kv(s, "square_fft_model_den", dt::square_fft_model_den);
    kv(s, "fft_model_log_power", dt::fft_model_log_power);
    kv(s, "fft_model_root_degree", dt::fft_model_root_degree);
#else
    kv(s, "fft_mul_cutoff", dt::fft_mul_cutoff);
    kv(s, "square_fft_cutoff", dt::square_fft_cutoff);
#endif
    kv(s, "fft_cyclic_cutoff", dt::fft_cyclic_cutoff);
    kv(s, "multiply_mod_bnm1_cutoff", dt::multiply_mod_bnm1_cutoff);
#if defined(BEMAN_BIG_INT_HAS_SHAPE_DISPATCH)
    #if !defined(BEMAN_BIG_INT_HAS_FFT_COST_MODEL)
    kv(s, "fft_mul_unbalanced_cutoff", dt::fft_mul_unbalanced_cutoff);
    kv(s, "fft_mul_unbalanced_num", dt::fft_mul_unbalanced_num);
    kv(s, "fft_mul_unbalanced_den", dt::fft_mul_unbalanced_den);
    #endif
    #if defined(BEMAN_BIG_INT_HAS_SLICE_ZONE_TABLE)
    kv(s, "mul_slice_z0_min", dt::mul_slice_z0_min);
    kv(s, "mul_slice_z0_num", dt::mul_slice_z0_num);
    kv(s, "mul_slice_z0_den", dt::mul_slice_z0_den);
    kv(s, "mul_slice_z1_min", dt::mul_slice_z1_min);
    kv(s, "mul_slice_z1_num", dt::mul_slice_z1_num);
    kv(s, "mul_slice_z1_den", dt::mul_slice_z1_den);
    kv(s, "mul_slice_z2_min", dt::mul_slice_z2_min);
    kv(s, "mul_slice_z2_num", dt::mul_slice_z2_num);
    kv(s, "mul_slice_z2_den", dt::mul_slice_z2_den);
    kv(s, "mul_slice_z3_min", dt::mul_slice_z3_min);
    kv(s, "mul_slice_z3_num", dt::mul_slice_z3_num);
    kv(s, "mul_slice_z3_den", dt::mul_slice_z3_den);
    kv(s, "mul_slice_z4_min", dt::mul_slice_z4_min);
    kv(s, "mul_slice_z4_num", dt::mul_slice_z4_num);
    kv(s, "mul_slice_z4_den", dt::mul_slice_z4_den);
    kv(s, "mul_slice_z5_min", dt::mul_slice_z5_min);
    kv(s, "mul_slice_z5_num", dt::mul_slice_z5_num);
    kv(s, "mul_slice_z5_den", dt::mul_slice_z5_den);
    kv(s, "mul_slice_zone_count", dt::mul_slice_zone_count);
    #else
    kv(s, "mul_slice_karatsuba_num", dt::mul_slice_karatsuba_num);
    kv(s, "mul_slice_karatsuba_den", dt::mul_slice_karatsuba_den);
    kv(s, "mul_slice_toom34_num", dt::mul_slice_toom34_num);
    kv(s, "mul_slice_toom34_den", dt::mul_slice_toom34_den);
    kv(s, "mul_slice_toom6585_num", dt::mul_slice_toom6585_num);
    kv(s, "mul_slice_toom6585_den", dt::mul_slice_toom6585_den);
    #endif
#endif

    // multiply_long_runtime.hpp / square_long_runtime.hpp (x86-64 only)
#if defined(BEMAN_BIG_INT_ARCH_X86_64)
    kv(s, "ifma_multiply_min_limbs", dt::ifma_multiply_min_limbs);
    #if defined(BEMAN_BIG_INT_HAS_IFMA_MULTIPLY_GATE)
    kv(s, "ifma_multiply_mid_limbs", dt::ifma_multiply_mid_limbs);
    kv(s, "ifma_multiply_high_limbs", dt::ifma_multiply_high_limbs);
    kv(s, "ifma_multiply_always_limbs", dt::ifma_multiply_always_limbs);
    kv(s, "ifma_multiply_min_product_low", dt::ifma_multiply_min_product_low);
    kv(s, "ifma_multiply_min_product_mid", dt::ifma_multiply_min_product_mid);
    kv(s, "ifma_multiply_min_product_high", dt::ifma_multiply_min_product_high);
    #endif
    kv(s, "ifma_square_min_limbs", dt::ifma_square_min_limbs);
    kv(s, "ifma_square_native_max_limbs", dt::ifma_square_native_max_limbs);
#endif

    // div_impl.hpp
    kv(s, "burnikel_ziegler_cutoff", dt::burnikel_ziegler_cutoff);
    kv(s, "burnikel_ziegler_offset", dt::burnikel_ziegler_offset);
    kv(s, "barrett_march_cutoff", dt::barrett_march_cutoff);
    kv(s, "barrett_march8_cutoff", dt::barrett_march8_cutoff);
    kv(s, "barrett_quarter_cutoff", dt::barrett_quarter_cutoff);
    kv(s, "barrett_balanced_cutoff", dt::barrett_balanced_cutoff);
    kv(s, "reciprocal_span_cutoff", dt::reciprocal_span_cutoff);

    // base_conversion.hpp
    kv(s, "fast_input_basecase_chunks", dt::fast_input_basecase_chunks);
    kv(s, "fast_output_basecase_chunks", dt::fast_output_basecase_chunks);
    kv(s, "fast_input_charconv_min_chunks", dt::fast_input_charconv_min_chunks);
    kv(s, "fast_output_preinv_min_limbs", dt::fast_output_preinv_min_limbs);

    // Configuration as this TU saw it; the library was built with the same PUBLIC definitions when built via CMake.
#if defined(BEMAN_BIG_INT_ARCH_X86_64)
    kv_str(s, "ARCH", "x86_64");
#elif defined(BEMAN_BIG_INT_ARCH_AARCH64)
    kv_str(s, "ARCH", "aarch64");
#else
    kv_str(s, "ARCH", "portable");
#endif
    kv(s, "BMI2_ADX", BEMAN_BIG_INT_X86_64_BMI2_ADX);
    kv(s, "AVX512_IFMA", BEMAN_BIG_INT_X86_64_AVX512_IFMA);
#if defined(BEMAN_BIG_INT_SIMD_MUL)
    kv(s, "SIMD_MUL", 1);
#else
    kv(s, "SIMD_MUL", 0);
#endif
    kv(s, "limb_bits", limb_bits);
#if defined(NDEBUG)
    kv(s, "ndebug", 1);
#else
    kv(s, "ndebug", 0);
#endif
#if defined(__OPTIMIZE__)
    kv(s, "optimize", 1);
#else
    kv(s, "optimize", 0);
#endif
#if defined(__BMI2__)
    kv(s, "isa_bmi2", 1);
#else
    kv(s, "isa_bmi2", 0);
#endif
#if defined(__ADX__)
    kv(s, "isa_adx", 1);
#else
    kv(s, "isa_adx", 0);
#endif
#if defined(__AVX2__)
    kv(s, "isa_avx2", 1);
#else
    kv(s, "isa_avx2", 0);
#endif
#if defined(__AVX512F__)
    kv(s, "isa_avx512f", 1);
#else
    kv(s, "isa_avx512f", 0);
#endif
#if defined(__AVX512IFMA__)
    kv(s, "isa_avx512ifma", 1);
#else
    kv(s, "isa_avx512ifma", 0);
#endif
    kv_str(s, "compiler", compiler_string());
#if defined(BEMAN_BIG_INT_SWEEP_VIA_CMAKE)
    kv_str(s, "via", "cmake");
    kv_str(s, "build_type", BEMAN_BIG_INT_SWEEP_BUILD_TYPE);
    kv_str(s, "cxx_flags", BEMAN_BIG_INT_SWEEP_CXX_FLAGS);
#else
    kv_str(s, "via", "manual");
#endif
    return s;
}

// ---------------------------------------------------------------------------
// Operands and residues.
// ---------------------------------------------------------------------------

u64 splitmix(u64 x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Random limbs with a nonzero top limb.
std::vector<limb_t> random_limbs(std::mt19937_64& rng, const std::size_t n) {
    std::vector<limb_t> v(n);
    for (auto& x : v) {
        x = static_cast<limb_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

u64 mulmod(const u64 x, const u64 y, const u64 p) { return x * y % p; }

// Value of x modulo p (p < 2^31), folded over 32-bit halves of each limb from the top.
u64 residue(const std::span<const limb_t> x, const u64 p) {
    u64 r = 0;
    for (std::size_t i = x.size(); i-- > 0;) {
        for (unsigned s = limb_bits; s > 0;) {
            s -= 32;
            r = ((r << 32) | ((static_cast<u64>(x[i]) >> s) & 0xFFFFFFFFULL)) % p;
        }
    }
    return r;
}

bb::big_int make_big(const std::vector<limb_t>& v) { return bb::big_int(v.begin(), v.end()); }

[[noreturn]] void fail(const std::string& msg) {
    std::fprintf(stderr, "shape_sweep: FAIL: %s\n", msg.c_str());
    std::exit(1);
}

[[noreturn]] void usage_error(const std::string& msg) {
    std::fprintf(stderr, "shape_sweep: %s\n", msg.c_str());
    std::exit(2);
}

// ---------------------------------------------------------------------------
// Timing.
// ---------------------------------------------------------------------------

using clock_type = std::chrono::steady_clock;

struct options {
    std::string                                      op;
    std::string                                      path = "auto";
    unsigned                                         rounds{9};
    double                                           round_ms{20.0};
    u64                                              seed{1};
    bool                                             gmp{false};
    std::vector<std::pair<std::size_t, std::size_t>> shapes;
};

struct timing {
    std::size_t reps;
    double      median_ns;
    double      min_ns;
    double      round_ms_actual; // mean length of the timed rounds
};

// Keeps `p` and the memory it points at observable so the timed work cannot be elided.
template <class T>
inline void escape(const T* p) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(p) : "memory");
#else
    static const volatile void* sink;
    sink = p;
#endif
}

double elapsed_ns(const clock_type::time_point since) {
    return std::chrono::duration<double, std::nano>(clock_type::now() - since).count();
}

// Nanoseconds per call over `reps` back-to-back calls.
template <class F>
double time_round(F& f, const std::size_t reps) {
    const auto t0 = clock_type::now();
    for (std::size_t i = 0; i < reps; ++i) {
        f();
    }
    return elapsed_ns(t0) / static_cast<double>(reps);
}

// Warms up for at least max(round_ms, 50 ms), sizing reps so a round lasts about round_ms, then times `rounds`
// rounds; a round shorter than 0.9 * round_ms is resized and redone.
template <class F>
timing measure(F&& f, const options& opt) {
    const double target_ns = opt.round_ms * 1e6;
    const double warm_ns   = std::max(opt.round_ms, 50.0) * 1e6;
    const auto   reps_for  = [&](const double per_op) {
        return std::max<std::size_t>(1, static_cast<std::size_t>(target_ns / std::max(per_op, 1.0)) + 1);
    };

    f(); // page in the operands and the allocator
    double      per_op = time_round(f, 1);
    std::size_t reps   = reps_for(per_op);

    const auto warm_start = clock_type::now();
    do {
        per_op = time_round(f, reps);
        reps   = reps_for(per_op);
    } while (elapsed_ns(warm_start) < warm_ns);

    std::vector<double> samples;
    double              total_round_ns = 0.0;
    for (unsigned r = 0; r < opt.rounds; ++r) {
        double t = time_round(f, reps);
        for (int attempt = 0; attempt < 10 && t * static_cast<double>(reps) < 0.9 * target_ns; ++attempt) {
            reps = reps_for(t);
            t    = time_round(f, reps);
        }
        samples.push_back(t);
        total_round_ns += t * static_cast<double>(reps);
    }
    std::ranges::sort(samples);
    const std::size_t n      = samples.size();
    const double      median = (n % 2 == 1) ? samples[n / 2] : 0.5 * (samples[n / 2 - 1] + samples[n / 2]);
    return {reps, median, samples.front(), total_round_ns / static_cast<double>(n) / 1e6};
}

void print_row(const options& opt, const char* path, const std::size_t la, const std::size_t lb, const timing& t) {
    std::printf("%s,%s,%zu,%zu,%zu,%u,%.2f,%.2f,%.2f\n",
                opt.op.c_str(),
                path,
                la,
                lb,
                t.reps,
                opt.rounds,
                t.median_ns,
                t.min_ns,
                t.round_ms_actual);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Correctness (untimed).
// ---------------------------------------------------------------------------

std::string shape_str(const std::size_t la, const std::size_t lb) {
    return std::to_string(la) + "x" + std::to_string(lb);
}

void check_product(const char*                   what,
                   const std::size_t             la,
                   const std::size_t             lb,
                   const std::span<const limb_t> a,
                   const std::span<const limb_t> b,
                   const std::span<const limb_t> c) {
    for (const u64 p : {prime_a, prime_b}) {
        if (mulmod(residue(a, p), residue(b, p), p) != residue(c, p)) {
            fail(std::string(what) + " residue mismatch mod " + std::to_string(p) + " at " + shape_str(la, lb));
        }
    }
}

// q * b + r == a modulo both primes, and 0 <= r < b, q >= 0.
void check_division(const char*                   what,
                    const std::size_t             la,
                    const std::size_t             lb,
                    const std::span<const limb_t> a,
                    const std::span<const limb_t> b,
                    const bb::big_int&            q,
                    const bb::big_int&            r,
                    const bb::big_int&            divisor) {
    for (const u64 p : {prime_a, prime_b}) {
        const u64 lhs =
            (mulmod(residue(q.representation(), p), residue(b, p), p) + residue(r.representation(), p)) % p;
        if (lhs != residue(a, p)) {
            fail(std::string(what) + " q*b+r residue mismatch mod " + std::to_string(p) + " at " + shape_str(la, lb));
        }
    }
    if (!(r < divisor) || r < 0 || q < 0) {
        fail(std::string(what) + " remainder out of range at " + shape_str(la, lb));
    }
}

// ---------------------------------------------------------------------------
// Drivers.
// ---------------------------------------------------------------------------

using path_fn = std::size_t (*)(std::span<limb_t>,
                                std::span<const limb_t>,
                                std::span<const limb_t>,
                                const dt::scratch_heap_source&);

path_fn select_runtime_path(const std::string& path) {
#if defined(BEMAN_BIG_INT_HAS_SHAPE_DISPATCH)
    if (path == "sliced") {
        return &dt::multiply_runtime_sliced;
    }
    if (path == "unsliced") {
        return &dt::multiply_runtime_unsliced;
    }
#else
    if (path == "sliced" || path == "unsliced") {
        usage_error("--path " + path + " needs a tree that defines BEMAN_BIG_INT_HAS_SHAPE_DISPATCH");
    }
#endif
    usage_error("unknown --path " + path);
}

std::size_t trimmed_size(const std::span<const limb_t> x) {
    std::size_t n = x.size();
    while (n > 1 && x[n - 1] == 0) {
        --n;
    }
    return n;
}

#if defined(BEMAN_BIG_INT_SWEEP_GMP)
// GMP timing shared by the ops; check(gr) validates the last result.
template <class Work, class Check>
void time_gmp(const options& opt, const std::size_t la, const std::size_t lb, Work&& work, Check&& check) {
    work();
    check();
    const timing t = measure(work, opt);
    check();
    print_row(opt, "gmp", la, lb, t);
}
#endif

void run_mul(const options& opt, const std::size_t la, const std::size_t lb) {
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    const auto      va = random_limbs(rng, la);
    const auto      vb = random_limbs(rng, lb);

    if (opt.path == "auto") {
        const bb::big_int a = make_big(va);
        const bb::big_int b = make_big(vb);
        {
            const bb::big_int c = a * b;
            check_product("mul", la, lb, va, vb, c.representation());
        }
        bb::big_int c;
        auto        work = [&] {
            c = a * b;
            escape(&c);
            escape(c.representation().data());
        };
        const timing t = measure(work, opt);
        check_product("mul (last timed product)", la, lb, va, vb, c.representation());
        print_row(opt, "auto", la, lb, t);
    } else if (opt.path == "sliced" || opt.path == "unsliced") {
        if (la < 2 || lb < 2) {
            usage_error("--path " + opt.path + " needs both operands of at least 2 limbs");
        }
        const path_fn                                       fn = select_runtime_path(opt.path);
        std::allocator<limb_t>                              alloc;
        const dt::scratch_allocator<std::allocator<limb_t>> hooks(alloc);
        std::vector<limb_t>                                 result(la + lb);
        std::size_t                                         n    = 0;
        auto                                                work = [&] {
            std::ranges::fill(result, limb_t{0});
            n = fn(result, va, vb, hooks.heap());
            escape(&n);
            escape(result.data());
        };
        const auto verify = [&](const char* what) {
            check_product(what, la, lb, va, vb, result);
            if (n != trimmed_size(result)) {
                fail(opt.path + " returned size " + std::to_string(n) + " but the trimmed size is " +
                     std::to_string(trimmed_size(result)) + " at " + shape_str(la, lb));
            }
        };
        work();
        verify(opt.path.c_str());
        const timing t = measure(work, opt);
        verify((opt.path + " (last timed product)").c_str());
        print_row(opt, opt.path.c_str(), la, lb, t);
    } else if (opt.path == "fft") {
        if constexpr (limb_bits != 64) {
            usage_error("--path fft needs 64-bit limbs");
        } else {
            // Like mul_dispatch.cpp: zeroed result, workspaces from the heap hooks, inside the timed region.
            std::allocator<limb_t>                              alloc;
            const dt::scratch_allocator<std::allocator<limb_t>> hooks(alloc);
            std::vector<limb_t>                                 result(la + lb);
            auto                                                work = [&] {
                std::ranges::fill(result, limb_t{0});
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                dt::scratch_heap_array<double> fp_ws(hooks.heap(), dt::fft_mul_fp_storage_size(la, lb));
                dt::scratch_heap_array<u64>    int_ws(hooks.heap(), dt::fft_mul_int_storage_size(la, lb));
                dt::multiply_fft(result, va, vb, fp_ws.span(), int_ws.span());
#else
                dt::scratch_heap_array<u64> ws(hooks.heap(), dt::fft_mul_storage_size(la, lb));
                dt::multiply_fft(result, va, vb, ws.span());
#endif
                escape(result.data());
            };
            work();
            check_product("fft", la, lb, va, vb, result);
            const timing t = measure(work, opt);
            check_product("fft (last timed product)", la, lb, va, vb, result);
            print_row(opt, "fft", la, lb, t);
        }
    } else {
        usage_error("unknown --path " + opt.path);
    }

#if defined(BEMAN_BIG_INT_SWEEP_GMP)
    if (opt.gmp) {
        const std::vector<mp_limb_t> ga(va.begin(), va.end());
        const std::vector<mp_limb_t> gb(vb.begin(), vb.end());
        std::vector<mp_limb_t>       gr(la + lb);
        time_gmp(
            opt,
            la,
            lb,
            [&] {
                if (la >= lb) {
                    __gmpn_mul(gr.data(), ga.data(), static_cast<long>(la), gb.data(), static_cast<long>(lb));
                } else {
                    __gmpn_mul(gr.data(), gb.data(), static_cast<long>(lb), ga.data(), static_cast<long>(la));
                }
                escape(gr.data());
            },
            [&] { check_product("gmp mul", la, lb, va, vb, std::vector<limb_t>(gr.begin(), gr.end())); });
    }
#endif
}

void run_sqr(const options& opt, const std::size_t n) {
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(n * 1000003 + n)));
    const auto      va = random_limbs(rng, n);

    const bb::big_int a = make_big(va);
    {
        const bb::big_int c = a * a; // same object on both sides selects the squaring path
        check_product("sqr", n, n, va, va, c.representation());
    }
    bb::big_int c;
    auto        work = [&] {
        c = a * a;
        escape(&c);
        escape(c.representation().data());
    };
    const timing t = measure(work, opt);
    check_product("sqr (last timed product)", n, n, va, va, c.representation());
    print_row(opt, "auto", n, n, t);

#if defined(BEMAN_BIG_INT_SWEEP_GMP)
    if (opt.gmp) {
        const std::vector<mp_limb_t> ga(va.begin(), va.end());
        std::vector<mp_limb_t>       gr(2 * n);
        time_gmp(
            opt,
            n,
            n,
            [&] {
                __gmpn_sqr(gr.data(), ga.data(), static_cast<long>(n));
                escape(gr.data());
            },
            [&] { check_product("gmp sqr", n, n, va, va, std::vector<limb_t>(gr.begin(), gr.end())); });
    }
#endif
}

// div, rem and divrem: each times only its own operator; the other half of the pair is computed untimed so both
// are always checked.
void run_div(const options& opt, const std::size_t la, const std::size_t lb) {
    if (la < lb) {
        usage_error(opt.op + " needs dividend limbs >= divisor limbs, got " + shape_str(la, lb));
    }
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    const auto      va = random_limbs(rng, la);
    const auto      vb = random_limbs(rng, lb);

    const bb::big_int a = make_big(va);
    const bb::big_int b = make_big(vb);
    {
        const bb::big_int q = a / b;
        const bb::big_int r = a % b;
        check_division(opt.op.c_str(), la, lb, va, vb, q, r, b);
    }
    bb::big_int q;
    bb::big_int r;
    auto        work = [&] {
        if (opt.op == "div") {
            q = a / b;
            escape(&q);
            escape(q.representation().data());
        } else if (opt.op == "rem") {
            r = a % b;
            escape(&r);
            escape(r.representation().data());
        } else {
            auto qr = bb::div_rem_to_zero(a, b);
            q       = std::move(qr.quotient);
            r       = std::move(qr.remainder);
            escape(&q);
            escape(&r);
            escape(q.representation().data());
        }
    };
    const timing t = measure(work, opt);
    if (opt.op == "div") {
        r = a % b;
    } else if (opt.op == "rem") {
        q = a / b;
    }
    check_division((opt.op + " (last timed result)").c_str(), la, lb, va, vb, q, r, b);
    print_row(opt, "auto", la, lb, t);

#if defined(BEMAN_BIG_INT_SWEEP_GMP)
    if (opt.gmp) {
        if (opt.op != "divrem") {
            std::printf("# gmp row for %s skipped: GMP computes quotient and remainder together, use divrem\n",
                        opt.op.c_str());
            return;
        }
        const std::vector<mp_limb_t> ga(va.begin(), va.end());
        const std::vector<mp_limb_t> gb(vb.begin(), vb.end());
        std::vector<mp_limb_t>       gq(la - lb + 1);
        std::vector<mp_limb_t>       gr(lb);
        time_gmp(
            opt,
            la,
            lb,
            [&] {
                __gmpn_tdiv_qr(
                    gq.data(), gr.data(), 0, ga.data(), static_cast<long>(la), gb.data(), static_cast<long>(lb));
                escape(gq.data());
                escape(gr.data());
            },
            [&] {
                const bb::big_int gq_big(gq.begin(), gq.end());
                const bb::big_int gr_big(gr.begin(), gr.end());
                check_division("gmp divrem", la, lb, va, vb, gq_big, gr_big, b);
            });
    }
#endif
}

// ---------------------------------------------------------------------------
// Command line.
// ---------------------------------------------------------------------------

std::pair<std::size_t, std::size_t> parse_shape(const std::string& tok, const bool square) {
    const auto  x = tok.find_first_of("xX");
    std::size_t a = 0;
    std::size_t b = 0;
    try {
        std::size_t pos = 0;
        if (x == std::string::npos) {
            a = std::stoull(tok, &pos);
            b = a;
            if (pos != tok.size()) {
                throw std::invalid_argument(tok);
            }
        } else {
            std::size_t p1 = 0;
            std::size_t p2 = 0;
            a              = std::stoull(tok.substr(0, x), &p1);
            b              = std::stoull(tok.substr(x + 1), &p2);
            if (p1 != x || p2 != tok.size() - x - 1) {
                throw std::invalid_argument(tok);
            }
        }
    } catch (const std::exception&) {
        usage_error("bad shape token '" + tok + "'");
    }
    if (a == 0 || b == 0) {
        usage_error("shape '" + tok + "' has a zero size");
    }
    if (x == std::string::npos && !square) {
        usage_error("shape '" + tok + "' needs the AxB form for this op");
    }
    if (square && a != b) {
        usage_error("sqr shape '" + tok + "' must be N or NxN");
    }
    return {a, b};
}

void read_grid(const std::string& file, std::vector<std::string>& tokens) {
    std::ifstream in(file);
    if (!in) {
        usage_error("cannot open grid file '" + file + "'");
    }
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream ss(line);
        std::string        tok;
        while (ss >> tok) {
            tokens.push_back(tok);
        }
    }
}

options parse_args(const int argc, char** argv) {
    options                  opt;
    std::vector<std::string> tokens;
    for (int i = 1; i < argc; ++i) {
        const std::string arg   = argv[i];
        auto              value = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage_error("missing value after " + arg);
            }
            return argv[++i];
        };
        if (arg == "--path") {
            opt.path = value();
        } else if (arg == "--rounds") {
            opt.rounds = static_cast<unsigned>(std::stoul(value()));
        } else if (arg == "--round-ms") {
            opt.round_ms = std::stod(value());
        } else if (arg == "--seed") {
            opt.seed = std::stoull(value());
        } else if (arg == "--gmp") {
            opt.gmp = true;
        } else if (arg == "--grid") {
            read_grid(value(), tokens);
        } else if (arg.starts_with("--")) {
            usage_error("unknown option " + arg);
        } else if (opt.op.empty()) {
            opt.op = arg;
        } else {
            tokens.push_back(arg);
        }
    }
    if (opt.op != "mul" && opt.op != "sqr" && opt.op != "div" && opt.op != "rem" && opt.op != "divrem") {
        usage_error("usage: shape_sweep <mul|sqr|div|rem|divrem> [--path auto|sliced|unsliced|fft] [--rounds N] "
                    "[--round-ms X] [--seed S] [--gmp] [--grid FILE] [AxB ...]");
    }
    if (opt.rounds == 0 || opt.round_ms <= 0.0) {
        usage_error("--rounds and --round-ms must be positive");
    }
    if (opt.path != "auto" && opt.op != "mul") {
        usage_error("--path other than auto is for mul only");
    }
    if (opt.path != "auto" && opt.path != "sliced" && opt.path != "unsliced" && opt.path != "fft") {
        usage_error("unknown --path " + opt.path);
    }
#if !defined(BEMAN_BIG_INT_SWEEP_GMP)
    if (opt.gmp) {
        usage_error("--gmp needs a build with -DBEMAN_BIG_INT_SWEEP_GMP");
    }
#endif
    if (opt.gmp && limb_bits != 64) {
        usage_error("--gmp needs 64-bit limbs");
    }
    if (opt.path == "fft" && limb_bits != 64) {
        usage_error("--path fft needs 64-bit limbs");
    }
#if !defined(BEMAN_BIG_INT_HAS_SHAPE_DISPATCH)
    if (opt.path == "sliced" || opt.path == "unsliced") {
        usage_error("--path " + opt.path + " needs a tree that defines BEMAN_BIG_INT_HAS_SHAPE_DISPATCH");
    }
#endif
    if (tokens.empty()) {
        usage_error("no shapes given");
    }
    for (const auto& tok : tokens) {
        opt.shapes.push_back(parse_shape(tok, opt.op == "sqr"));
    }
    return opt;
}

} // namespace

int main(int argc, char** argv) {
    const options opt = parse_args(argc, argv);

    std::printf("%s\n", const_line().c_str());
    std::printf("# op=%s path=%s rounds=%u round_ms=%g seed=%llu gmp=%d\n",
                opt.op.c_str(),
                opt.path.c_str(),
                opt.rounds,
                opt.round_ms,
                static_cast<unsigned long long>(opt.seed),
                opt.gmp ? 1 : 0);
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
    std::printf("# gmp_version=%s\n", __gmp_version);
#endif
    std::printf("op,path,la,lb,reps,rounds,median_ns,min_ns,round_ms_actual\n");

    for (const auto& [la, lb] : opt.shapes) {
        if (opt.op == "mul") {
            run_mul(opt, la, lb);
        } else if (opt.op == "sqr") {
            run_sqr(opt, la);
        } else {
            run_div(opt, la, lb);
        }
    }
    return 0;
}
