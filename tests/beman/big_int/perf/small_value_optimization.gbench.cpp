// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Google Benchmark comparison of beman::big_int against the native builtin
// integer types for the four basic arithmetic operations (add, sub, mul, div).
//
// The point of this suite is the small-value optimization: a big_int whose
// magnitude fits in its inplace limb buffer performs no heap allocation, so its
// arithmetic should land within a small constant factor of the equivalent
// builtin operation. Three tiers are measured for each operation:
//
//   * 64-bit tier  -- std::int64_t against the default big_int
//                     (basic_big_int<64>, one inplace limb). Operands are sized
//                     so the result fits in 64 bits, keeping big_int inplace.
//   * 128-bit tier -- __int128 against basic_big_int<128> (two inplace limbs),
//                     where the builtin 128-bit type is available.
//   * large tier   -- the same big_int operations on multi-limb, heap-allocated
//                     operands, to show the cost once a value outgrows the
//                     inplace buffer and the optimization no longer applies.
//
// Read the numbers relatively: each timed iteration also pays a fixed
// DoNotOptimize fence so the optimizer can neither hoist the operation out of
// the loop nor delete it. That fence is identical for every type, so the gap
// between rows reflects the arithmetic, not the harness.

#include <benchmark/benchmark.h>

#include <beman/big_int.hpp>

#include <cstdint>

namespace {

using big_int_64  = BEMAN_BIG_INT_NAMESPACE::big_int;            // basic_big_int<64>: 1 inplace limb
using big_int_128 = BEMAN_BIG_INT_NAMESPACE::basic_big_int<128>; // 2 inplace limbs

// Compose a `(hi << 64) | lo` value of the target type. Works uniformly for the
// builtin 128-bit integer and for big_int, both of which support `<<` and `|`.
template <typename T>
T compose(std::uint64_t hi, std::uint64_t lo) {
    return (T{hi} << 64) | T{lo};
}

// One operation per templated kernel so the identical loop body is timed for
// every integer type. The operands are copied in by value once, outside the
// timed loop; inside it, fencing both inputs defeats loop-invariant hoisting and
// fencing the result defeats dead-code elimination.
template <typename T>
void add(benchmark::State& state, T lhs, T rhs) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(lhs);
        benchmark::DoNotOptimize(rhs);
        T result = lhs + rhs;
        benchmark::DoNotOptimize(result);
    }
}

template <typename T>
void subtract(benchmark::State& state, T lhs, T rhs) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(lhs);
        benchmark::DoNotOptimize(rhs);
        T result = lhs - rhs;
        benchmark::DoNotOptimize(result);
    }
}

template <typename T>
void multiply(benchmark::State& state, T lhs, T rhs) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(lhs);
        benchmark::DoNotOptimize(rhs);
        T result = lhs * rhs;
        benchmark::DoNotOptimize(result);
    }
}

template <typename T>
void divide(benchmark::State& state, T lhs, T rhs) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(lhs);
        benchmark::DoNotOptimize(rhs);
        T result = lhs / rhs;
        benchmark::DoNotOptimize(result);
    }
}

// 64-bit-range operands. Every result stays within std::int64_t and within a
// single big_int limb, so the default big_int never leaves inplace storage.
constexpr std::int64_t k64_add_a = 4'100'000'000'000'000'001; // sum ~8.0e18 < INT64_MAX
constexpr std::int64_t k64_add_b = 3'900'000'000'000'000'003;
constexpr std::int64_t k64_mul_a = 1'500'000'001; // product ~2.1e18 < INT64_MAX
constexpr std::int64_t k64_mul_b = 1'400'000'009;
constexpr std::int64_t k64_div_a = 8'000'000'000'000'000'001;
constexpr std::int64_t k64_div_b = 1'000'003;

// 128-bit-range operands as (hi:lo) limb pairs.
// Magnitudes stay well under 2^126 so the signed __int128 add/sub/mul cannot overflow,
// and the two-limb big_int_128 values remain inplace.
constexpr std::uint64_t k128_a_hi  = 0x0000'000F'1234'5678ull; // ~2^100
constexpr std::uint64_t k128_a_lo  = 0x9ABC'DEF0'1234'5678ull;
constexpr std::uint64_t k128_b_hi  = 0x0000'000A'FEDC'BA98ull;
constexpr std::uint64_t k128_b_lo  = 0x7654'3210'FEDC'BA98ull;
constexpr std::uint64_t k128_mul_a = 0x0FFF'FFFF'FFFF'FFFFull; // ~2^60, product ~2^120
constexpr std::uint64_t k128_mul_b = 0x0EEE'EEEE'EEEE'EEEFull;
constexpr std::uint64_t k128_div_b = 0x0000'00AB'CDEF'0123ull; // ~2^40

// Multi-limb operands that force heap storage (~2^320 and ~2^256)
// Low bits mixed in so neither is a power of two.
big_int_64 large_a() { return (big_int_64{0xC0FFEE0123456789ull} << 256) | big_int_64{0x9E3779B97F4A7C15ull}; }
big_int_64 large_b() { return (big_int_64{0xDEADBEEFCAFEBABEull} << 192) | big_int_64{0x0123456789ABCDEFull}; }

// ----------------------------------------------------------------------------
// Addition
// ----------------------------------------------------------------------------
BENCHMARK_CAPTURE(add, int64, std::int64_t{k64_add_a}, std::int64_t{k64_add_b});
BENCHMARK_CAPTURE(add, big_int_64bit, big_int_64{k64_add_a}, big_int_64{k64_add_b});
#ifdef BEMAN_BIG_INT_HAS_INT128_EXTENSION
BENCHMARK_CAPTURE(add, int128, compose<__int128>(k128_a_hi, k128_a_lo), compose<__int128>(k128_b_hi, k128_b_lo));
BENCHMARK_CAPTURE(add,
                  big_int_128bit,
                  compose<big_int_128>(k128_a_hi, k128_a_lo),
                  compose<big_int_128>(k128_b_hi, k128_b_lo));
#endif
BENCHMARK_CAPTURE(add, big_int_large, large_a(), large_b());

// ----------------------------------------------------------------------------
// Subtraction
// ----------------------------------------------------------------------------
BENCHMARK_CAPTURE(subtract, int64, std::int64_t{k64_add_a}, std::int64_t{k64_add_b});
BENCHMARK_CAPTURE(subtract, big_int_64bit, big_int_64{k64_add_a}, big_int_64{k64_add_b});
#ifdef BEMAN_BIG_INT_HAS_INT128_EXTENSION
BENCHMARK_CAPTURE(subtract, int128, compose<__int128>(k128_a_hi, k128_a_lo), compose<__int128>(k128_b_hi, k128_b_lo));
BENCHMARK_CAPTURE(subtract,
                  big_int_128bit,
                  compose<big_int_128>(k128_a_hi, k128_a_lo),
                  compose<big_int_128>(k128_b_hi, k128_b_lo));
#endif
BENCHMARK_CAPTURE(subtract, big_int_large, large_a(), large_b());

// ----------------------------------------------------------------------------
// Multiplication
// ----------------------------------------------------------------------------
BENCHMARK_CAPTURE(multiply, int64, std::int64_t{k64_mul_a}, std::int64_t{k64_mul_b});
BENCHMARK_CAPTURE(multiply, big_int_64bit, big_int_64{k64_mul_a}, big_int_64{k64_mul_b});
#ifdef BEMAN_BIG_INT_HAS_INT128_EXTENSION
BENCHMARK_CAPTURE(multiply, int128, static_cast<__int128>(k128_mul_a), static_cast<__int128>(k128_mul_b));
BENCHMARK_CAPTURE(multiply, big_int_128bit, big_int_128{k128_mul_a}, big_int_128{k128_mul_b});
#endif
BENCHMARK_CAPTURE(multiply, big_int_large, large_a(), large_b());

// ----------------------------------------------------------------------------
// Division
// ----------------------------------------------------------------------------
BENCHMARK_CAPTURE(divide, int64, std::int64_t{k64_div_a}, std::int64_t{k64_div_b});
BENCHMARK_CAPTURE(divide, big_int_64bit, big_int_64{k64_div_a}, big_int_64{k64_div_b});
#ifdef BEMAN_BIG_INT_HAS_INT128_EXTENSION
BENCHMARK_CAPTURE(divide, int128, compose<__int128>(k128_a_hi, k128_a_lo), static_cast<__int128>(k128_div_b));
BENCHMARK_CAPTURE(divide, big_int_128bit, compose<big_int_128>(k128_a_hi, k128_a_lo), big_int_128{k128_div_b});
#endif
BENCHMARK_CAPTURE(divide, big_int_large, large_a(), large_b());

} // namespace

BENCHMARK_MAIN();
