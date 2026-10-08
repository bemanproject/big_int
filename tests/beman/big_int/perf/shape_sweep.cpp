// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// End-to-end shape sweep for tuning and for the GMP gap analysis: times big_int operations over a list of operand
// shapes and prints CSV on stdout. Every other line starts with '#'; the first is a '#const' line listing every tuning
// constant and the build configuration, which the scratch drivers (build/tune/bin/variant.sh, abab.sh,
// build/gmpgap/bin/gap_sweep.sh) use to detect stale or mismatched binaries.
//
//   shape_sweep <op> [--rows R1,R2,...] [--path R] [--rounds N] [--round-ms X] [--seed S] [--gmp] [--grid FILE]
//               [SHAPE ...]
//
// ops and shapes (limb counts unless noted):
//   add sub cmp gcd  AxB (cmp: N or NxN, operands equal except limb 0; gcd: b is made odd for every row)
//   shl shr          NxS: N limbs shifted by S bits
//   mul              AxB            sqr  N or NxN          div rem divrem  dividend x divisor
//   tochars          NxBASE         fromchars  NxBASE (the decimal-or-other string of a random N-limb value)
//
// Rows (CSV column 'path'; default 'auto'; rows of one shape run back to back):
//   auto     the public big_int API into a pre-existing destination (c = a + b, ...)
//   inplace  add/sub: c += b; c -= b, shl/shr: c <<= s; c >>= s. The pair is timed and the reported per-op time is
//   half. kernel   the library's detail-level span kernels on preallocated buffers; for mul/sqr it is the runtime tier
//   ladder
//            (unsliced) and shares its timings with --rows unsliced. Kernels that work in place (shifts, gcd, tochars
//            and gmp's get_str/gcd/set_str inputs) include the copy-in of their operands in the timed loop.
//   kernelip shl/shr only: in-place pair shl then shr on one buffer, halved, no copy-in
//   gmp      GMP mpn on preallocated buffers (needs -DBEMAN_BIG_INT_SWEEP_GMP, 64-bit limbs, -lgmp)
//   gmpz     GMP mpz with the destination reused, operands loaded with mpz_import
//   sliced unsliced fft   (mul only) the runtime kernels called directly on raw limb spans; sliced and unsliced need
//            BEMAN_BIG_INT_HAS_SHAPE_DISPATCH, fft needs 64-bit limbs and, like the dispatcher, zeroes the result and
//            allocates its workspaces inside the timed region.
// --path X is an alias for --rows X and --gmp appends the gmp row. A row that does not apply to an op or shape is
// reported on a '# skip' line. div and rem time only the named operator, so the kernel and GMP rows exist for divrem
// only. Every row is checked (residue identities, identical limbs / sign / digits, gcd against the divisibility and
// mpz_gcd) before it is timed and again after.
//
// Build through CMake (target beman.big_int.benchmarks.shape_sweep, BEMAN_BIG_INT_BUILD_BENCHMARKS=ON), which puts
// the library's generated detail/config_generated.hpp (BMI2_ADX, AVX512_IFMA, SIMD_MUL) on the include path and passes
// the compile flags. A manual build must reproduce that: the build directory's generated/include on the include path,
// plus the same BEMAN_BIG_INT_FORCED_LIMB_WIDTH, -march / -mavx2 style flags and NDEBUG as the library was built with
// (see the library target's entries in the build directory's compile_commands.json), and must pass
// -DBEMAN_BIG_INT_SWEEP_MANUAL_OK. Otherwise the harness TU can disagree with the library and #const would lie.
//   B=build/appleclang-release
//   c++ -std=c++23 -O2 -DNDEBUG -DBEMAN_BIG_INT_SWEEP_MANUAL_OK -I$B/generated/include -Iinclude -o shape_sweep
//       $B/libbeman.big_int.a tests/beman/big_int/perf/shape_sweep.cpp    (one command; source before or after the .a)

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
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// The x86 box has libgmp but no gmp.h, so declare the few mpn/mpz entry points ourselves.
extern "C" {
using mp_limb_t = unsigned long;
struct __mpz_struct {
    int        _mp_alloc;
    int        _mp_size;
    mp_limb_t* _mp_d;
};
extern const char* const __gmp_version;
void                     __gmpn_mul(mp_limb_t* rp, const mp_limb_t* up, long un, const mp_limb_t* vp, long vn);
void                     __gmpn_sqr(mp_limb_t* rp, const mp_limb_t* up, long n);
void                     __gmpn_tdiv_qr(
    mp_limb_t* qp, mp_limb_t* rp, long qxn, const mp_limb_t* np, long nn, const mp_limb_t* dp, long dn);
mp_limb_t   __gmpn_add_n(mp_limb_t* rp, const mp_limb_t* up, const mp_limb_t* vp, long n);
mp_limb_t   __gmpn_add(mp_limb_t* rp, const mp_limb_t* up, long un, const mp_limb_t* vp, long vn);
mp_limb_t   __gmpn_sub_n(mp_limb_t* rp, const mp_limb_t* up, const mp_limb_t* vp, long n);
mp_limb_t   __gmpn_sub(mp_limb_t* rp, const mp_limb_t* up, long un, const mp_limb_t* vp, long vn);
mp_limb_t   __gmpn_lshift(mp_limb_t* rp, const mp_limb_t* up, long n, unsigned cnt);
mp_limb_t   __gmpn_rshift(mp_limb_t* rp, const mp_limb_t* up, long n, unsigned cnt);
int         __gmpn_cmp(const mp_limb_t* up, const mp_limb_t* vp, long n);
mp_limb_t   __gmpn_divrem_1(mp_limb_t* qp, long qxn, const mp_limb_t* up, long un, mp_limb_t d);
std::size_t __gmpn_get_str(unsigned char* str, int base, mp_limb_t* up, long un);
long        __gmpn_set_str(mp_limb_t* rp, const unsigned char* str, std::size_t len, int base);
long        __gmpn_gcd(mp_limb_t* rp, mp_limb_t* up, long un, mp_limb_t* vp, long vn);
void        __gmpz_init(__mpz_struct* z);
void        __gmpz_clear(__mpz_struct* z);
void        __gmpz_import(
    __mpz_struct* z, std::size_t count, int order, std::size_t size, int endian, std::size_t nails, const void* data);
void  __gmpz_add(__mpz_struct* r, const __mpz_struct* a, const __mpz_struct* b);
void  __gmpz_sub(__mpz_struct* r, const __mpz_struct* a, const __mpz_struct* b);
void  __gmpz_mul(__mpz_struct* r, const __mpz_struct* a, const __mpz_struct* b);
void  __gmpz_mul_2exp(__mpz_struct* r, const __mpz_struct* a, unsigned long bits);
void  __gmpz_tdiv_q_2exp(__mpz_struct* r, const __mpz_struct* a, unsigned long bits);
void  __gmpz_tdiv_qr(__mpz_struct* q, __mpz_struct* r, const __mpz_struct* n, const __mpz_struct* d);
void  __gmpz_gcd(__mpz_struct* g, const __mpz_struct* a, const __mpz_struct* b);
int   __gmpz_cmp(const __mpz_struct* a, const __mpz_struct* b);
char* __gmpz_get_str(char* str, int base, const __mpz_struct* z);
int   __gmpz_set_str(__mpz_struct* z, const char* str, int base);
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
    std::vector<std::string>                         rows{"auto"};
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

using limbs = std::vector<limb_t>;

// Trimmed copy of x (a zero value is the single limb 0, whatever its representation).
limbs trimmed(const std::span<const limb_t> x) {
    const std::size_t n = x.empty() ? 0 : trimmed_size(x);
    limbs             v(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n));
    if (v.empty()) {
        v.push_back(0);
    }
    return v;
}

void expect_same(const std::string&            what,
                 const std::string&            shape,
                 const std::span<const limb_t> got,
                 const limbs&                  ref) {
    if (trimmed(got) != ref) {
        fail(what + " result differs from the reference at " + shape);
    }
}

u64 pow2mod(u64 e, const u64 p) {
    u64 r = 1;
    u64 b = 2 % p;
    for (; e != 0; e >>= 1) {
        if ((e & 1) != 0) {
            r = mulmod(r, b, p);
        }
        b = mulmod(b, b, p);
    }
    return r;
}

bool has_row(const options& opt, const char* row) { return std::ranges::find(opt.rows, row) != opt.rows.end(); }

void skip_row(const options& opt, const std::string& row, const std::string& why) {
    std::printf("# skip %s %s: %s\n", opt.op.c_str(), row.c_str(), why.c_str());
    std::fflush(stdout);
}

// Times one row: untimed check, the timed loop, the check again. `scale` converts a timed unit of work that holds
// several operations into per-operation time (0.5 for the add/sub and shift pairs).
template <class Work, class Check>
void time_row(const options&    opt,
              const char*       name,
              const std::size_t la,
              const std::size_t lb,
              Work&&            work,
              Check&&           check,
              const double      scale = 1.0) {
    work();
    check();
    timing t = measure(work, opt);
    check();
    t.median_ns *= scale;
    t.min_ns *= scale;
    print_row(opt, name, la, lb, t);
}

#if defined(BEMAN_BIG_INT_SWEEP_GMP)
// GMP mpz operand/destination with the limbs readable through the hand-declared struct.
struct zint {
    __mpz_struct z{};

    zint() { __gmpz_init(&z); }
    explicit zint(const std::span<const limb_t> v) : zint() {
        __gmpz_import(&z, v.size(), -1, sizeof(limb_t), 0, 0, v.data());
    }
    ~zint() { __gmpz_clear(&z); }
    zint(const zint&)            = delete;
    zint& operator=(const zint&) = delete;

    [[nodiscard]] limbs get() const {
        const std::size_t n = static_cast<std::size_t>(z._mp_size < 0 ? -z._mp_size : z._mp_size);
        return trimmed(limbs(z._mp_d, z._mp_d + n));
    }
};

std::vector<mp_limb_t> to_gmp(const std::span<const limb_t> x) { return {x.begin(), x.end()}; }
limbs                  from_gmp(const std::vector<mp_limb_t>& x, const std::size_t n) {
    return limbs(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n));
}
#endif

// ---------------------------------------------------------------------------
// mul and sqr.
// ---------------------------------------------------------------------------

void run_mul(const options& opt, const std::size_t la, const std::size_t lb) {
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    const auto      va = random_limbs(rng, la);
    const auto      vb = random_limbs(rng, lb);
    const auto      sh = shape_str(la, lb);

    for (const auto& row : opt.rows) {
        if (row == "auto") {
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
        } else if (row == "sliced" || row == "unsliced" || row == "kernel") {
            const std::string path = row == "kernel" ? "unsliced" : row;
            if (la < 2 || lb < 2) {
                if (row == "kernel") {
                    skip_row(opt, row, "the tier ladder needs both operands of at least 2 limbs");
                    continue;
                }
                usage_error("--path " + row + " needs both operands of at least 2 limbs");
            }
#if !defined(BEMAN_BIG_INT_HAS_SHAPE_DISPATCH)
            if (row == "kernel") {
                skip_row(opt, row, "needs BEMAN_BIG_INT_HAS_SHAPE_DISPATCH");
                continue;
            }
#endif
            const path_fn                                       fn = select_runtime_path(path);
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
                    fail(path + " returned size " + std::to_string(n) + " but the trimmed size is " +
                         std::to_string(trimmed_size(result)) + " at " + sh);
                }
            };
            work();
            verify(path.c_str());
            const timing t = measure(work, opt);
            verify((path + " (last timed product)").c_str());
            print_row(opt, row.c_str(), la, lb, t);
        } else if (row == "fft") {
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
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto             ga = to_gmp(va);
            const auto             gb = to_gmp(vb);
            std::vector<mp_limb_t> gr(la + lb);
            time_row(
                opt,
                "gmp",
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
                [&] { check_product("gmp mul", la, lb, va, vb, from_gmp(gr, gr.size())); });
        } else if (row == "gmpz") {
            const zint za(va);
            const zint zb(vb);
            zint       zc;
            time_row(
                opt,
                "gmpz",
                la,
                lb,
                [&] {
                    __gmpz_mul(&zc.z, &za.z, &zb.z);
                    escape(zc.z._mp_d);
                },
                [&] { check_product("gmpz mul", la, lb, va, vb, zc.get()); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

void run_sqr(const options& opt, const std::size_t n) {
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(n * 1000003 + n)));
    const auto      va = random_limbs(rng, n);

    for (const auto& row : opt.rows) {
        if (row == "auto") {
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
        } else if (row == "kernel") {
            if (n < 2) {
                skip_row(opt, row, "the squaring ladder needs at least 2 limbs");
                continue;
            }
            // dt::square_runtime directly (the mul kernel row's squaring counterpart); result zeroed inside the loop.
            std::allocator<limb_t>                              alloc;
            const dt::scratch_allocator<std::allocator<limb_t>> hooks(alloc);
            std::vector<limb_t>                                 result(2 * n);
            std::size_t                                         size = 0;
            time_row(
                opt,
                "kernel",
                n,
                n,
                [&] {
                    std::ranges::fill(result, limb_t{0});
                    size = dt::square_runtime(result, va, hooks.heap());
                    escape(&size);
                    escape(result.data());
                },
                [&] {
                    check_product("kernel sqr", n, n, va, va, result);
                    if (size != trimmed_size(result)) {
                        fail("kernel sqr returned a wrong size at " + shape_str(n, n));
                    }
                });
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto             ga = to_gmp(va);
            std::vector<mp_limb_t> gr(2 * n);
            time_row(
                opt,
                "gmp",
                n,
                n,
                [&] {
                    __gmpn_sqr(gr.data(), ga.data(), static_cast<long>(n));
                    escape(gr.data());
                },
                [&] { check_product("gmp sqr", n, n, va, va, from_gmp(gr, gr.size())); });
        } else if (row == "gmpz") {
            const zint za(va);
            zint       zc;
            time_row(
                opt,
                "gmpz",
                n,
                n,
                [&] {
                    __gmpz_mul(&zc.z, &za.z, &za.z); // same operand selects mpn_sqr
                    escape(zc.z._mp_d);
                },
                [&] { check_product("gmpz sqr", n, n, va, va, zc.get()); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// div, rem and divrem: div and rem time only their own operator (auto row only); the other half of the pair is
// computed untimed so both are always checked. The kernel and GMP rows time the combined quotient and remainder.
// ---------------------------------------------------------------------------

void run_div(const options& opt, const std::size_t la, const std::size_t lb) {
    if (la < lb) {
        usage_error(opt.op + " needs dividend limbs >= divisor limbs, got " + shape_str(la, lb));
    }
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    const auto      va = random_limbs(rng, la);
    const auto      vb = random_limbs(rng, lb);
    const auto      sh = shape_str(la, lb);

    const bb::big_int a = make_big(va);
    const bb::big_int b = make_big(vb);
    limbs             ref_q;
    limbs             ref_r;
    {
        const bb::big_int q = a / b;
        const bb::big_int r = a % b;
        check_division(opt.op.c_str(), la, lb, va, vb, q, r, b);
        ref_q = trimmed(q.representation());
        ref_r = trimmed(r.representation());
    }
    const bool is_divrem = opt.op == "divrem";

    for (const auto& row : opt.rows) {
        if (row == "auto") {
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
        } else if (row == "kernel") {
            if (!is_divrem) {
                skip_row(opt, row, "kernel rows time quotient and remainder together, use divrem");
            } else if (lb == 1) {
                limbs  q(la);
                limb_t rem = 0;
                time_row(
                    opt,
                    "kernel",
                    la,
                    lb,
                    [&] {
                        rem = dt::divide_unsigned_short(q, va, vb[0]);
                        escape(&rem);
                        escape(q.data());
                    },
                    [&] {
                        expect_same("kernel divrem quotient", sh, q, ref_q);
                        expect_same("kernel divrem remainder", sh, std::span<const limb_t>{&rem, 1}, ref_r);
                    });
            } else {
                const std::size_t                             q_len = la - lb + 1;
                const std::size_t                             r_cap = la + 1;
                std::allocator<limb_t>                        alloc;
                dt::scratch_allocator<std::allocator<limb_t>> scratch(r_cap + dt::divide_unsigned_storage_size(la, lb),
                                                                      alloc);
                const std::span<limb_t>                       rem = scratch.allocate(r_cap);
                limbs                                         q(q_len);
                time_row(
                    opt,
                    "kernel",
                    la,
                    lb,
                    [&] {
                        std::ranges::fill(q, limb_t{0});
                        dt::divide_dispatch(q, rem, va, vb, scratch, alloc);
                        escape(q.data());
                        escape(rem.data());
                    },
                    [&] {
                        expect_same("kernel divrem quotient", sh, q, ref_q);
                        expect_same("kernel divrem remainder", sh, rem, ref_r);
                    });
            }
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            if (!is_divrem) {
                skip_row(opt, row, "GMP computes quotient and remainder together, use divrem");
                continue;
            }
            const auto             ga = to_gmp(va);
            const auto             gb = to_gmp(vb);
            std::vector<mp_limb_t> gq(la - lb + 1);
            std::vector<mp_limb_t> gr(lb);
            if (lb == 1) {
                std::vector<mp_limb_t> gq1(la);
                mp_limb_t              rem = 0;
                time_row(
                    opt,
                    "gmp",
                    la,
                    lb,
                    [&] {
                        rem = __gmpn_divrem_1(gq1.data(), 0, ga.data(), static_cast<long>(la), gb[0]);
                        escape(&rem);
                        escape(gq1.data());
                    },
                    [&] {
                        expect_same("gmp divrem_1 quotient", sh, from_gmp(gq1, gq1.size()), ref_q);
                        expect_same("gmp divrem_1 remainder", sh, limbs{static_cast<limb_t>(rem)}, ref_r);
                    });
            } else {
                time_row(
                    opt,
                    "gmp",
                    la,
                    lb,
                    [&] {
                        __gmpn_tdiv_qr(gq.data(),
                                       gr.data(),
                                       0,
                                       ga.data(),
                                       static_cast<long>(la),
                                       gb.data(),
                                       static_cast<long>(lb));
                        escape(gq.data());
                        escape(gr.data());
                    },
                    [&] {
                        const bb::big_int gq_big(gq.begin(), gq.end());
                        const bb::big_int gr_big(gr.begin(), gr.end());
                        check_division("gmp divrem", la, lb, va, vb, gq_big, gr_big, b);
                        expect_same("gmp divrem quotient", sh, from_gmp(gq, gq.size()), ref_q);
                        expect_same("gmp divrem remainder", sh, from_gmp(gr, gr.size()), ref_r);
                    });
            }
        } else if (row == "gmpz") {
            if (!is_divrem) {
                skip_row(opt, row, "GMP computes quotient and remainder together, use divrem");
                continue;
            }
            const zint zn(va);
            const zint zd(vb);
            zint       zq;
            zint       zr;
            time_row(
                opt,
                "gmpz",
                la,
                lb,
                [&] {
                    __gmpz_tdiv_qr(&zq.z, &zr.z, &zn.z, &zd.z);
                    escape(zq.z._mp_d);
                    escape(zr.z._mp_d);
                },
                [&] {
                    expect_same("gmpz divrem quotient", sh, zq.get(), ref_q);
                    expect_same("gmpz divrem remainder", sh, zr.get(), ref_r);
                });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// add and sub: AxB limbs with A >= B; a > b always (equal sizes get a top bit set in a and cleared in b).
// ---------------------------------------------------------------------------

template <bool Add>
void check_addsub(const char* what, const std::string& sh, const limbs& va, const limbs& vb, const limbs& vc) {
    for (const u64 p : {prime_a, prime_b}) {
        const u64 ra   = residue(va, p);
        const u64 rb   = residue(vb, p);
        const u64 want = Add ? (ra + rb) % p : (ra + p - rb) % p;
        if (residue(vc, p) != want) {
            fail(std::string(what) + " residue mismatch mod " + std::to_string(p) + " at " + sh);
        }
    }
}

template <bool Add>
void run_addsub(const options& opt, const std::size_t la, const std::size_t lb) {
    if (la < lb) {
        usage_error(opt.op + " needs A >= B limbs, got " + shape_str(la, lb));
    }
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    auto            va = random_limbs(rng, la);
    auto            vb = random_limbs(rng, lb);
    if (la == lb) {
        constexpr limb_t top = limb_t{1} << (limb_bits - 1);
        va.back() |= top;
        vb.back() &= ~top;
        if (vb.back() == 0) {
            vb.back() = 1;
        }
    }
    const auto sh = shape_str(la, lb);

    const bb::big_int a = make_big(va);
    const bb::big_int b = make_big(vb);
    limbs             ref;
    {
        const bb::big_int c = Add ? a + b : a - b;
        check_addsub<Add>("api", sh, va, vb, trimmed(c.representation()));
        ref = trimmed(c.representation());
    }
    const limbs va_ref = trimmed(va);

    for (const auto& row : opt.rows) {
        if (row == "auto") {
            bb::big_int c;
            time_row(
                opt,
                "auto",
                la,
                lb,
                [&] {
                    if constexpr (Add) {
                        c = a + b;
                    } else {
                        c = a - b;
                    }
                    escape(&c);
                    escape(c.representation().data());
                },
                [&] { expect_same("auto", sh, c.representation(), ref); });
        } else if (row == "inplace") {
            // One timed unit is two operations (c += b; c -= b, or the reverse for sub); per-op time is half.
            {
                bb::big_int t = a;
                if constexpr (Add) {
                    t += b;
                } else {
                    t -= b;
                }
                expect_same("inplace first op", sh, t.representation(), ref);
            }
            bb::big_int c = a;
            time_row(
                opt,
                "inplace",
                la,
                lb,
                [&] {
                    if constexpr (Add) {
                        c += b;
                        c -= b;
                    } else {
                        c -= b;
                        c += b;
                    }
                    escape(&c);
                    escape(c.representation().data());
                },
                [&] { expect_same("inplace pair", sh, c.representation(), va_ref); },
                0.5);
        } else if (row == "kernel") {
            limbs       res(la);
            bool        carry = false;
            std::size_t size  = 0;
            time_row(
                opt,
                "kernel",
                la,
                lb,
                [&] {
                    if constexpr (Add) {
                        carry = dt::add_unsigned_spans(res, va, vb);
                        escape(&carry);
                    } else {
                        size = dt::subtract_unsigned_spans(res, va, vb);
                        escape(&size);
                    }
                    escape(res.data());
                },
                [&] {
                    limbs full = res;
                    if (Add && carry) {
                        full.push_back(1);
                    }
                    expect_same("kernel", sh, full, ref);
                    if (!Add && size != trimmed_size(res)) {
                        fail("kernel sub returned a wrong size at " + sh);
                    }
                });
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto             ga = to_gmp(va);
            const auto             gb = to_gmp(vb);
            std::vector<mp_limb_t> gr(la);
            mp_limb_t              carry = 0;
            time_row(
                opt,
                "gmp",
                la,
                lb,
                [&] {
                    const auto n = static_cast<long>(la);
                    const auto m = static_cast<long>(lb);
                    if (la == lb) {
                        carry = Add ? __gmpn_add_n(gr.data(), ga.data(), gb.data(), n)
                                    : __gmpn_sub_n(gr.data(), ga.data(), gb.data(), n);
                    } else {
                        carry = Add ? __gmpn_add(gr.data(), ga.data(), n, gb.data(), m)
                                    : __gmpn_sub(gr.data(), ga.data(), n, gb.data(), m);
                    }
                    escape(&carry);
                    escape(gr.data());
                },
                [&] {
                    auto full = from_gmp(gr, gr.size());
                    if (Add && carry != 0) {
                        full.push_back(1);
                    }
                    expect_same("gmp", sh, full, ref);
                });
        } else if (row == "gmpz") {
            const zint za(va);
            const zint zb(vb);
            zint       zc;
            time_row(
                opt,
                "gmpz",
                la,
                lb,
                [&] {
                    if constexpr (Add) {
                        __gmpz_add(&zc.z, &za.z, &zb.z);
                    } else {
                        __gmpz_sub(&zc.z, &za.z, &zb.z);
                    }
                    escape(zc.z._mp_d);
                },
                [&] { expect_same("gmpz", sh, zc.get(), ref); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// cmp: N limbs, operands equal except limb 0, so every limb is scanned.
// ---------------------------------------------------------------------------

void run_cmp(const options& opt, const std::size_t la, const std::size_t lb) {
    if (la != lb) {
        usage_error("cmp needs equal limb counts, got " + shape_str(la, lb));
    }
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    const auto      va = random_limbs(rng, la);
    auto            vb = va;
    vb[0]              = va[0] ^ 1;
    if (vb[0] == 0) {
        vb[0] = 3; // only reachable for a one-limb operand, whose value must stay nonzero
    }
    const int  want = va[0] < vb[0] ? -1 : 1;
    const auto sh   = shape_str(la, lb);

    const bb::big_int a     = make_big(va);
    const bb::big_int b     = make_big(vb);
    int               s     = 0;
    const auto        check = [&](const char* what) {
        if (s != want) {
            fail(std::string(what) + " cmp sign mismatch at " + sh);
        }
    };

    for (const auto& row : opt.rows) {
        if (row == "auto") {
            time_row(
                opt,
                "auto",
                la,
                lb,
                [&] {
                    escape(&a);
                    escape(&b);
                    const auto o = a <=> b;
                    s            = o < 0 ? -1 : (o > 0 ? 1 : 0);
                    escape(&s);
                },
                [&] { check("auto"); });
        } else if (row == "kernel") {
            time_row(
                opt,
                "kernel",
                la,
                lb,
                [&] {
                    escape(va.data());
                    escape(vb.data());
                    const auto o = dt::compare_unsigned_spans(va, vb);
                    s            = o < 0 ? -1 : (o > 0 ? 1 : 0);
                    escape(&s);
                },
                [&] { check("kernel"); });
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto ga = to_gmp(va);
            const auto gb = to_gmp(vb);
            time_row(
                opt,
                "gmp",
                la,
                lb,
                [&] {
                    escape(ga.data());
                    escape(gb.data());
                    const int o = __gmpn_cmp(ga.data(), gb.data(), static_cast<long>(la));
                    s           = o < 0 ? -1 : (o > 0 ? 1 : 0);
                    escape(&s);
                },
                [&] { check("gmp"); });
        } else if (row == "gmpz") {
            const zint za(va);
            const zint zb(vb);
            time_row(
                opt,
                "gmpz",
                la,
                lb,
                [&] {
                    escape(za.z._mp_d);
                    escape(zb.z._mp_d);
                    const int o = __gmpz_cmp(&za.z, &zb.z);
                    s           = o < 0 ? -1 : (o > 0 ? 1 : 0);
                    escape(&s);
                },
                [&] { check("gmpz"); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// shl and shr: la = N limbs, lb = S bits (1 <= S < 64 N). GMP's mpn shifts take 1..63 bits, so for S >= 64 the gmp row
// zero-fills (shl) or skips (shr) the whole limbs and shifts the rest.
// ---------------------------------------------------------------------------

template <bool Left>
void check_shift(const char* what, const std::string& sh, const limbs& va, const std::size_t bits, const limbs& vc) {
    const std::size_t whole   = bits / limb_bits;
    const unsigned    partial = static_cast<unsigned>(bits % limb_bits);
    limbs             low(va.begin(), va.begin() + static_cast<std::ptrdiff_t>(whole + (partial != 0 ? 1 : 0)));
    if (partial != 0) {
        low.back() &= (limb_t{1} << partial) - 1;
    }
    if (low.empty()) {
        low.push_back(0);
    }
    for (const u64 p : {prime_a, prime_b}) {
        const u64  p2 = pow2mod(bits, p);
        const u64  ra = residue(va, p);
        const u64  rc = residue(vc, p);
        const bool ok = Left ? mulmod(ra, p2, p) == rc : (mulmod(rc, p2, p) + residue(low, p)) % p == ra;
        if (!ok) {
            fail(std::string(what) + " residue mismatch mod " + std::to_string(p) + " at " + sh);
        }
    }
}

template <bool Left>
void run_shift(const options& opt, const std::size_t n, const std::size_t bits) {
    if (bits < 1 || bits >= n * limb_bits) {
        usage_error(opt.op + " needs 1 <= S < 64 N, got " + shape_str(n, bits));
    }
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(n * 1000003 + bits)));
    const auto      va = random_limbs(rng, n);
    const auto      sh = shape_str(n, bits);

    const std::size_t whole   = bits / limb_bits;
    const unsigned    partial = static_cast<unsigned>(bits % limb_bits);
    const int         s       = static_cast<int>(bits);

    const bb::big_int a = make_big(va);
    limbs             ref;
    {
        const bb::big_int c = Left ? a << s : a >> s;
        check_shift<Left>("api", sh, va, bits, trimmed(c.representation()));
        ref = trimmed(c.representation());
    }
    const limbs va_ref = trimmed(va);

    for (const auto& row : opt.rows) {
        if (row == "auto") {
            bb::big_int c;
            time_row(
                opt,
                "auto",
                n,
                bits,
                [&] {
                    if constexpr (Left) {
                        c = a << s;
                    } else {
                        c = a >> s;
                    }
                    escape(&c);
                    escape(c.representation().data());
                },
                [&] { expect_same("auto", sh, c.representation(), ref); });
        } else if (row == "inplace") {
            // One timed unit is two operations, c <<= s; c >>= s for shl and c >>= s; c <<= s for shr (the latter
            // settles on a with its low S bits cleared); per-op time is half.
            {
                bb::big_int t = a;
                if constexpr (Left) {
                    t <<= s;
                } else {
                    t >>= s;
                }
                expect_same("inplace first op", sh, t.representation(), ref);
            }
            bb::big_int c = a;
            time_row(
                opt,
                "inplace",
                n,
                bits,
                [&] {
                    if constexpr (Left) {
                        c <<= s;
                        c >>= s;
                    } else {
                        c >>= s;
                        c <<= s;
                    }
                    escape(&c);
                    escape(c.representation().data());
                },
                [&] {
                    if constexpr (Left) {
                        expect_same("inplace pair", sh, c.representation(), va_ref);
                    } else {
                        bb::big_int t = c;
                        t >>= s;
                        expect_same("inplace pair", sh, t.representation(), ref);
                    }
                },
                0.5);
        } else if (row == "kernel") {
            // The span kernels work in place, so the copy-in of the source is part of the timed loop.
            if constexpr (Left) {
                limbs       buf(n + whole + 1);
                std::size_t size = 0;
                time_row(
                    opt,
                    "kernel",
                    n,
                    bits,
                    [&] {
                        std::ranges::copy(va, buf.begin());
                        size = dt::shift_left_bits(buf, n, bits);
                        escape(&size);
                        escape(buf.data());
                    },
                    [&] { expect_same("kernel", sh, std::span<const limb_t>{buf.data(), size}, ref); });
            } else {
                // The same two steps as shift_right_bits (move down whole limbs, shift_right_n the rest), without its
                // debug assertion that every dropped bit is zero.
                const std::size_t m = n - whole;
                limbs             buf(m);
                limb_t            dropped = 0;
                time_row(
                    opt,
                    "kernel",
                    n,
                    bits,
                    [&] {
                        std::copy(va.begin() + static_cast<std::ptrdiff_t>(whole), va.end(), buf.begin());
                        if (partial != 0) {
                            dropped = dt::shift_right_n(buf, partial);
                            escape(&dropped);
                        }
                        escape(buf.data());
                    },
                    [&] { expect_same("kernel", sh, buf, ref); });
            }
        } else if (row == "kernelip") {
            if constexpr (!Left) {
                skip_row(opt, row, "identical to shl kernelip (the pair is shl then shr)");
            } else {
                // In-place shl then shr on one buffer, no copy-in; per-op time is half.
                limbs       buf(n + whole + 1);
                std::size_t size = n;
                std::ranges::copy(va, buf.begin());
                time_row(
                    opt,
                    "kernelip",
                    n,
                    bits,
                    [&] {
                        size = dt::shift_left_bits(buf, n, bits);
                        size = dt::shift_right_bits(buf, size, bits);
                        escape(&size);
                        escape(buf.data());
                    },
                    [&] { expect_same("kernelip", sh, std::span<const limb_t>{buf.data(), size}, va_ref); },
                    0.5);
            }
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto             ga = to_gmp(va);
            std::vector<mp_limb_t> gr(Left ? n + whole + 1 : n - whole);
            time_row(
                opt,
                "gmp",
                n,
                bits,
                [&] {
                    if constexpr (Left) {
                        if (whole != 0) {
                            std::memset(gr.data(), 0, whole * sizeof(mp_limb_t));
                        }
                        if (partial != 0) {
                            gr[n + whole] = __gmpn_lshift(gr.data() + whole, ga.data(), static_cast<long>(n), partial);
                        } else {
                            std::memcpy(gr.data() + whole, ga.data(), n * sizeof(mp_limb_t));
                            gr[n + whole] = 0;
                        }
                    } else {
                        const auto m = static_cast<long>(n - whole);
                        if (partial != 0) {
                            const mp_limb_t dropped = __gmpn_rshift(gr.data(), ga.data() + whole, m, partial);
                            escape(&dropped);
                        } else {
                            std::memcpy(gr.data(), ga.data() + whole, static_cast<std::size_t>(m) * sizeof(mp_limb_t));
                        }
                    }
                    escape(gr.data());
                },
                [&] { expect_same("gmp", sh, from_gmp(gr, gr.size()), ref); });
        } else if (row == "gmpz") {
            const zint za(va);
            zint       zc;
            time_row(
                opt,
                "gmpz",
                n,
                bits,
                [&] {
                    if constexpr (Left) {
                        __gmpz_mul_2exp(&zc.z, &za.z, bits);
                    } else {
                        __gmpz_tdiv_q_2exp(&zc.z, &za.z, bits);
                    }
                    escape(zc.z._mp_d);
                },
                [&] { expect_same("gmpz", sh, zc.get(), ref); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// tochars and fromchars: N limbs, base in [2, 36]. The kernel rows need a base is_fast_conversion_base accepts (no
// power of two). GMP's digit-value interfaces are paired with the ASCII-mapped output; get_str destroys its input, so
// its copy-in is timed (as for gcd). The string is lower case, as std::to_chars writes it.
// ---------------------------------------------------------------------------

char digit_char(const unsigned d) { return static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10)); }

std::string map_digits(const unsigned char* d, const std::size_t n) {
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i) {
        s[i] = digit_char(d[i]);
    }
    return s;
}

void check_base(const options& opt, const std::size_t base) {
    if (base < 2 || base > 36) {
        usage_error(opt.op + " needs a base in [2, 36], got " + std::to_string(base));
    }
}

// Digits an n-limb value can need in `base`.
std::size_t digit_capacity(const std::size_t n, const int base) {
    return n * limb_bits / static_cast<std::size_t>(std::bit_width(static_cast<unsigned>(base)) - 1) + 8;
}

void run_tochars(const options& opt, const std::size_t n, const std::size_t base_arg) {
    check_base(opt, base_arg);
    const int       base = static_cast<int>(base_arg);
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(n * 1000003 + base_arg)));
    const auto      va  = random_limbs(rng, n);
    const auto      sh  = shape_str(n, base_arg);
    const auto      cap = digit_capacity(n, base);

    const bb::big_int a = make_big(va);
    std::string       ref(cap, '\0');
    {
        const auto r = bb::to_chars(ref.data(), ref.data() + ref.size(), a, base);
        if (r.ec != std::errc{}) {
            fail("to_chars failed at " + sh);
        }
        ref.resize(static_cast<std::size_t>(r.ptr - ref.data()));
        bb::big_int back;
        const auto  fr = bb::from_chars(ref.data(), ref.data() + ref.size(), back, base);
        if (fr.ec != std::errc{}) {
            fail("from_chars failed on the to_chars output at " + sh);
        }
        expect_same("tochars round trip", sh, back.representation(), trimmed(va));
    }
    const auto same = [&](const char* what, const std::string& got) {
        if (got != ref) {
            fail(std::string(what) + " digits differ from the reference at " + sh);
        }
    };

    for (const auto& row : opt.rows) {
        if (row == "auto") {
            std::string          buf(cap, '\0');
            std::to_chars_result r{};
            time_row(
                opt,
                "auto",
                n,
                base_arg,
                [&] {
                    r = bb::to_chars(buf.data(), buf.data() + buf.size(), a, base);
                    escape(&r);
                    escape(buf.data());
                },
                [&] { same("auto", std::string(buf.data(), r.ptr)); });
        } else if (row == "kernel") {
            if (!dt::is_fast_conversion_base(base)) {
                skip_row(opt, row, "no span-level kernel for a power-of-two base");
                continue;
            }
            std::allocator<limb_t>                        alloc;
            dt::scratch_allocator<std::allocator<limb_t>> scratch(dt::limbs_to_digits_storage_size(n, base), alloc);
            std::vector<unsigned char>                    out(dt::base_conversion_digit_bound(va, base));
            std::size_t                                   count = 0;
            time_row(
                opt,
                "kernel",
                n,
                base_arg,
                [&] {
                    count = dt::limbs_to_digits(out, va, base, scratch, alloc);
                    escape(&count);
                    escape(out.data());
                },
                [&] {
                    if (scratch.m_offset != 0) {
                        fail("limbs_to_digits left scratch allocated at " + sh);
                    }
                    same("kernel", map_digits(out.data(), count));
                });
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto                 ga = to_gmp(va);
            std::vector<mp_limb_t>     tmp(n);
            std::vector<unsigned char> out(cap);
            std::size_t                count = 0;
            time_row(
                opt,
                "gmp",
                n,
                base_arg,
                [&] {
                    std::memcpy(tmp.data(), ga.data(), n * sizeof(mp_limb_t));
                    count = __gmpn_get_str(out.data(), base, tmp.data(), static_cast<long>(n));
                    escape(&count);
                    escape(out.data());
                },
                [&] { same("gmp", map_digits(out.data(), count)); });
        } else if (row == "gmpz") {
            const zint  za(va);
            std::string buf(cap + 2, '\0');
            time_row(
                opt,
                "gmpz",
                n,
                base_arg,
                [&] {
                    __gmpz_get_str(buf.data(), base, &za.z);
                    escape(buf.data());
                },
                [&] { same("gmpz", std::string(buf.c_str())); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

void run_fromchars(const options& opt, const std::size_t n, const std::size_t base_arg) {
    check_base(opt, base_arg);
    const int       base = static_cast<int>(base_arg);
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(n * 1000003 + base_arg)));
    const auto      va  = random_limbs(rng, n);
    const auto      sh  = shape_str(n, base_arg);
    const limbs     ref = trimmed(va);

    const bb::big_int a = make_big(va);
    std::string       str(digit_capacity(n, base), '\0');
    {
        const auto r = bb::to_chars(str.data(), str.data() + str.size(), a, base);
        if (r.ec != std::errc{}) {
            fail("to_chars failed at " + sh);
        }
        str.resize(static_cast<std::size_t>(r.ptr - str.data()));
    }
    std::vector<unsigned char> digits(str.size());
    for (std::size_t i = 0; i < str.size(); ++i) {
        digits[i] = static_cast<unsigned char>(dt::digit_value(str[i]));
    }

    for (const auto& row : opt.rows) {
        if (row == "auto") {
            bb::big_int            c;
            std::from_chars_result r{};
            time_row(
                opt,
                "auto",
                n,
                base_arg,
                [&] {
                    r = bb::from_chars(str.data(), str.data() + str.size(), c, base);
                    escape(&r);
                    escape(&c);
                    escape(c.representation().data());
                },
                [&] {
                    if (r.ec != std::errc{}) {
                        fail("from_chars failed at " + sh);
                    }
                    expect_same("auto", sh, c.representation(), ref);
                });
        } else if (row == "kernel") {
            if (!dt::is_fast_conversion_base(base)) {
                skip_row(opt, row, "no span-level kernel for a power-of-two base");
                continue;
            }
            std::allocator<limb_t>                        alloc;
            dt::scratch_allocator<std::allocator<limb_t>> scratch(
                dt::digits_to_limbs_storage_size(digits.size(), base), alloc);
            limbs       res(dt::base_conversion_limb_bound(digits.size(), base));
            std::size_t size = 0;
            time_row(
                opt,
                "kernel",
                n,
                base_arg,
                [&] {
                    size = dt::digits_to_limbs(
                        res, digits, base, static_cast<dt::scratch_allocator_base&>(scratch), alloc);
                    escape(&size);
                    escape(res.data());
                },
                [&] {
                    if (scratch.m_offset != 0) {
                        fail("digits_to_limbs left scratch allocated at " + sh);
                    }
                    expect_same("kernel", sh, std::span<const limb_t>{res.data(), size}, ref);
                });
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            std::vector<mp_limb_t> gr(dt::base_conversion_limb_bound(digits.size(), base) + 2);
            long                   size = 0;
            time_row(
                opt,
                "gmp",
                n,
                base_arg,
                [&] {
                    size = __gmpn_set_str(gr.data(), digits.data(), digits.size(), base);
                    escape(&size);
                    escape(gr.data());
                },
                [&] { expect_same("gmp", sh, from_gmp(gr, static_cast<std::size_t>(size)), ref); });
        } else if (row == "gmpz") {
            zint zc;
            int  rc = 0;
            time_row(
                opt,
                "gmpz",
                n,
                base_arg,
                [&] {
                    rc = __gmpz_set_str(&zc.z, str.c_str(), base);
                    escape(&rc);
                    escape(zc.z._mp_d);
                },
                [&] {
                    if (rc != 0) {
                        fail("mpz_set_str failed at " + sh);
                    }
                    expect_same("gmpz", sh, zc.get(), ref);
                });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// gcd: A x B limbs with A >= B; b is odd (GMP's mpn_gcd precondition) for every row. The kernel and gmp rows time the
// copy-in of their clobbered operands. The kernel row is gcd_unsigned_spans alone: it skips the public driver's
// Euclidean steps for lopsided operands and its single-limb shortcuts.
// ---------------------------------------------------------------------------

void run_gcd(const options& opt, const std::size_t la, const std::size_t lb) {
    if (la < lb) {
        usage_error("gcd needs A >= B limbs, got " + shape_str(la, lb));
    }
    std::mt19937_64 rng(splitmix(opt.seed ^ splitmix(la * 1000003 + lb)));
    const auto      va = random_limbs(rng, la);
    auto            vb = random_limbs(rng, lb);
    vb[0] |= 1;
    const auto sh = shape_str(la, lb);

    const bb::big_int a = make_big(va);
    const bb::big_int b = make_big(vb);
    limbs             ref;
    {
        const bb::big_int g = bb::gcd(a, b);
        if (g <= 0 || !((a % g) == 0) || !((b % g) == 0)) {
            fail("gcd result does not divide both operands at " + sh);
        }
        ref = trimmed(g.representation());
    }

    for (const auto& row : opt.rows) {
        if (row == "auto") {
            bb::big_int c;
            time_row(
                opt,
                "auto",
                la,
                lb,
                [&] {
                    c = bb::gcd(a, b);
                    escape(&c);
                    escape(c.representation().data());
                },
                [&] { expect_same("auto", sh, c.representation(), ref); });
        } else if (row == "kernel") {
            const std::size_t cap = la + 1;
            limbs             u(cap);
            limbs             v(cap);
            limbs             t(cap);
            std::size_t       size = 0;
            time_row(
                opt,
                "kernel",
                la,
                lb,
                [&] {
                    std::ranges::copy(va, u.begin());
                    std::ranges::copy(vb, v.begin());
                    size = dt::gcd_unsigned_spans(u, la, v, lb, la >= 3 ? std::span<limb_t>{t} : std::span<limb_t>{});
                    escape(&size);
                    escape(u.data());
                },
                [&] { expect_same("kernel", sh, std::span<const limb_t>{u.data(), size}, ref); });
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
        } else if (row == "gmp") {
            const auto             ga = to_gmp(va);
            const auto             gb = to_gmp(vb);
            std::vector<mp_limb_t> u(la);
            std::vector<mp_limb_t> v(lb);
            std::vector<mp_limb_t> gr(lb);
            long                   size = 0;
            time_row(
                opt,
                "gmp",
                la,
                lb,
                [&] {
                    std::memcpy(u.data(), ga.data(), la * sizeof(mp_limb_t));
                    std::memcpy(v.data(), gb.data(), lb * sizeof(mp_limb_t));
                    size = __gmpn_gcd(gr.data(), u.data(), static_cast<long>(la), v.data(), static_cast<long>(lb));
                    escape(&size);
                    escape(gr.data());
                },
                [&] { expect_same("gmp", sh, from_gmp(gr, static_cast<std::size_t>(size)), ref); });
        } else if (row == "gmpz") {
            const zint za(va);
            const zint zb(vb);
            zint       zc;
            time_row(
                opt,
                "gmpz",
                la,
                lb,
                [&] {
                    __gmpz_gcd(&zc.z, &za.z, &zb.z);
                    escape(zc.z._mp_d);
                },
                [&] { expect_same("gmpz", sh, zc.get(), ref); });
#endif
        } else {
            skip_row(opt, row, "not applicable");
        }
    }
}

// ---------------------------------------------------------------------------
// Command line.
// ---------------------------------------------------------------------------

enum class shape_kind { pair, single_or_equal };

std::pair<std::size_t, std::size_t> parse_shape(const std::string& tok, const shape_kind kind) {
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
    if (x == std::string::npos && kind == shape_kind::pair) {
        usage_error("shape '" + tok + "' needs the AxB form for this op");
    }
    if (kind == shape_kind::single_or_equal && a != b) {
        usage_error("shape '" + tok + "' must be N or NxN for this op");
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

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream       ss(s);
    std::string              part;
    while (std::getline(ss, part, ',')) {
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

const char* const all_ops[] = {
    "mul", "sqr", "div", "rem", "divrem", "add", "sub", "shl", "shr", "cmp", "tochars", "fromchars", "gcd"};
const char* const all_rows[] = {"auto", "inplace", "kernel", "kernelip", "gmp", "gmpz", "sliced", "unsliced", "fft"};

bool one_of(const std::string& s, const auto& list) { return std::ranges::find(list, s) != std::end(list); }

shape_kind kind_of(const std::string& op) {
    return (op == "sqr" || op == "cmp") ? shape_kind::single_or_equal : shape_kind::pair;
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
            opt.rows = {value()};
        } else if (arg == "--rows") {
            opt.rows = split_commas(value());
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
    if (!one_of(opt.op, all_ops)) {
        usage_error("usage: shape_sweep <mul|sqr|div|rem|divrem|add|sub|shl|shr|cmp|tochars|fromchars|gcd> "
                    "[--rows auto,inplace,kernel,kernelip,gmp,gmpz,sliced,unsliced,fft] [--path ROW] [--rounds N] "
                    "[--round-ms X] [--seed S] [--gmp] [--grid FILE] [SHAPE ...]");
    }
    if (opt.rounds == 0 || opt.round_ms <= 0.0) {
        usage_error("--rounds and --round-ms must be positive");
    }
    if (opt.gmp && !has_row(opt, "gmp")) {
        opt.rows.emplace_back("gmp");
    }
    if (opt.rows.empty()) {
        usage_error("--rows needs at least one row");
    }
    for (const auto& row : opt.rows) {
        if (!one_of(row, all_rows)) {
            usage_error("unknown row '" + row + "'");
        }
        if ((row == "sliced" || row == "unsliced" || row == "fft") && opt.op != "mul") {
            usage_error("--path " + row + " is for mul only");
        }
        if ((row == "gmp" || row == "gmpz") && limb_bits != 64) {
            usage_error("the " + row + " row needs 64-bit limbs");
        }
        if (row == "fft" && limb_bits != 64) {
            usage_error("--path fft needs 64-bit limbs");
        }
#if !defined(BEMAN_BIG_INT_SWEEP_GMP)
        if (row == "gmp" || row == "gmpz") {
            usage_error("the " + row + " row (--gmp) needs a build with -DBEMAN_BIG_INT_SWEEP_GMP");
        }
#endif
#if !defined(BEMAN_BIG_INT_HAS_SHAPE_DISPATCH)
        if (row == "sliced" || row == "unsliced") {
            usage_error("--path " + row + " needs a tree that defines BEMAN_BIG_INT_HAS_SHAPE_DISPATCH");
        }
#endif
    }
    if (tokens.empty()) {
        usage_error("no shapes given");
    }
    for (const auto& tok : tokens) {
        opt.shapes.push_back(parse_shape(tok, kind_of(opt.op)));
    }
    return opt;
}

} // namespace

int main(int argc, char** argv) {
    const options opt = parse_args(argc, argv);

    std::string rows;
    for (const auto& r : opt.rows) {
        rows += (rows.empty() ? "" : ",") + r;
    }
    std::printf("%s\n", const_line().c_str());
    std::printf("# op=%s rows=%s rounds=%u round_ms=%g seed=%llu\n",
                opt.op.c_str(),
                rows.c_str(),
                opt.rounds,
                opt.round_ms,
                static_cast<unsigned long long>(opt.seed));
#if defined(BEMAN_BIG_INT_SWEEP_GMP)
    std::printf("# gmp_version=%s\n", __gmp_version);
#endif
    std::printf("op,path,la,lb,reps,rounds,median_ns,min_ns,round_ms_actual\n");

    for (const auto& [la, lb] : opt.shapes) {
        const auto& op = opt.op;
        if (op == "mul") {
            run_mul(opt, la, lb);
        } else if (op == "sqr") {
            run_sqr(opt, la);
        } else if (op == "add") {
            run_addsub<true>(opt, la, lb);
        } else if (op == "sub") {
            run_addsub<false>(opt, la, lb);
        } else if (op == "cmp") {
            run_cmp(opt, la, lb);
        } else if (op == "shl") {
            run_shift<true>(opt, la, lb);
        } else if (op == "shr") {
            run_shift<false>(opt, la, lb);
        } else if (op == "tochars") {
            run_tochars(opt, la, lb);
        } else if (op == "fromchars") {
            run_fromchars(opt, la, lb);
        } else if (op == "gcd") {
            run_gcd(opt, la, lb);
        } else {
            run_div(opt, la, lb);
        }
    }
    return 0;
}
