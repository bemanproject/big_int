// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Direct tests of beman_big_int_multiply_long_runtime (the assembly kernel,
// or its portable fallback elsewhere) against multiply_long. On x86-64, also
// runs every check against the two underlying kernels directly (the generic
// baseline and the BMI2+ADX kernel, the latter skipped when the running CPU
// lacks either extension), not just the compile-time-selected forwarder.

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/multiply_long_runtime.hpp>

#include "x86_64_bmi2_adx_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <limits>
#include <ostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using limb = ::beman::big_int::uint_multiprecision_t;

constexpr limb        limb_max    = std::numeric_limits<limb>::max();
constexpr limb        poison      = limb_max / 0xFF * 0xA5; // 0xA5 in every byte
constexpr std::size_t guard_limbs = 4;
constexpr std::size_t max_len     = 40;
constexpr limb        top_bit     = limb{1} << (std::numeric_limits<limb>::digits - 1);

// Extreme limb values that stress the folded BMI2/ADX square's carried top
// bits and the all-ones overflow path (also exercised here through the
// general multiply). Each limb of an adversarial operand independently picks
// one of these, or a genuinely random value.
constexpr limb adversarial_values[] = {limb{0}, limb{1}, limb_max, top_bit, top_bit - 1, limb_max - 1};

void fill_adversarial(std::vector<limb>& v, std::mt19937_64& rng) {
    constexpr int                       choice_count = static_cast<int>(std::size(adversarial_values)) + 1;
    std::uniform_int_distribution<int>  choice(0, choice_count - 1);
    std::uniform_int_distribution<limb> dist;
    for (limb& x : v) {
        const int c = choice(rng);
        x           = (c == choice_count - 1) ? dist(rng) : adversarial_values[static_cast<std::size_t>(c)];
    }
}

// Additional (len_a, len_b) coverage: the BMI2/ADX kernel switches internal
// tiers around len_a ~48 and has per-length code below that, so every len_a
// up to past that tier boundary must be exercised, against a handful of
// representative len_b (including every len_b & 3 remainder and a couple of
// wider ones).
constexpr std::size_t wide_len_a_max      = 130;
constexpr std::size_t wide_len_b_values[] = {1, 2, 3, 4, 5, 7, 8, 9, 16, 17};

using multiply_fn = void (*)(limb*, const limb*, std::size_t, const limb*, std::size_t);

#if defined(BEMAN_BIG_INT_ARCH_X86_64)
void call_generic(limb* r, const limb* a, const std::size_t len_a, const limb* b, const std::size_t len_b) {
    ::beman_big_int_multiply_long_runtime_generic(r, a, len_a, b, len_b);
}
void call_bmi2_adx(limb* r, const limb* a, const std::size_t len_a, const limb* b, const std::size_t len_b) {
    ::beman_big_int_multiply_long_runtime_bmi2_adx(r, a, len_a, b, len_b);
}
#endif

void call_selected(limb* r, const limb* a, const std::size_t len_a, const limb* b, const std::size_t len_b) {
    ::beman_big_int_multiply_long_runtime(r, a, len_a, b, len_b);
}

// One kernel under test: a name (also the gtest instance name) and the
// function to call. The "Bmi2Adx" kernel GTEST_SKIP()s unless the running
// CPU has both BMI2 and ADX.
struct kernel {
    const char* name;
    multiply_fn fn;
};

// Lets gtest print the kernel name instead of a raw byte dump on failure.
void PrintTo(const kernel& k, std::ostream* os) { *os << k.name; }

std::vector<kernel> all_kernels() {
    std::vector<kernel> ks{
        {"Selected", &call_selected},
    };
#if defined(BEMAN_BIG_INT_ARCH_X86_64)
    ks.push_back({"Generic", &call_generic});
    ks.push_back({"Bmi2Adx", &call_bmi2_adx});
#endif
    return ks;
}

// Multiplies a * b through `fn` into a poisoned buffer framed by guard limbs,
// checking the product against multiply_long and that nothing outside
// [0, len_a + len_b) is written.
void expect_multiply_matches(multiply_fn              fn,
                             const std::vector<limb>& a,
                             const std::vector<limb>& b,
                             const char*              pattern) {
    const std::size_t len_a = a.size();
    const std::size_t len_b = b.size();

    std::vector<limb> expected(len_a + len_b);
    ::beman::big_int::detail::multiply_long(expected, a, b);

    std::vector<limb> buf(len_a + len_b + 2 * guard_limbs, poison);
    fn(buf.data() + guard_limbs, a.data(), len_a, b.data(), len_b);

    for (std::size_t k = 0; k < guard_limbs; ++k) {
        ASSERT_EQ(buf[k], poison) << pattern << " len_a=" << len_a << " len_b=" << len_b << " leading guard " << k;
        ASSERT_EQ(buf[guard_limbs + len_a + len_b + k], poison)
            << pattern << " len_a=" << len_a << " len_b=" << len_b << " trailing guard " << k;
    }
    for (std::size_t k = 0; k < len_a + len_b; ++k) {
        ASSERT_EQ(buf[guard_limbs + k], expected[k])
            << pattern << " len_a=" << len_a << " len_b=" << len_b << " limb " << k;
    }
}

class MultiplyLongRuntime : public ::testing::TestWithParam<kernel> {
  protected:
    // GTEST_SKIP() only unwinds the function it is directly called from, so
    // this check must live in SetUp() (one of the two places gtest documents
    // it as valid) rather than in a helper the test body merely calls.
    void SetUp() override {
        const std::string_view name = GetParam().name;
        if (name != "Bmi2Adx") {
            return;
        }
        if (!::beman::big_int::tests::cpu_has_bmi2_and_adx()) {
            GTEST_SKIP() << "CPU lacks BMI2 and/or ADX";
        }
        if (!::beman::big_int::tests::bmi2_adx_kernels_are_usable()) {
            GTEST_SKIP() << "CPUID claims BMI2/ADX but the instructions fault (emulator)";
        }
    }
};

// Either operand empty (0xN and Nx0) must write nothing at all: no product
// limbs and no carry-out limb, matching the portable fallback's early return.
TEST_P(MultiplyLongRuntime, EmptyOperandWritesNothing) {
    const multiply_fn fn = GetParam().fn;

    const limb a[2] = {1, 2};
    const limb b[3] = {3, 4, 5};

    {
        std::vector<limb> buf(3 + 2 * guard_limbs, poison);
        fn(buf.data() + guard_limbs, a, 0, b, 3);
        for (const limb x : buf) {
            EXPECT_EQ(x, poison);
        }
    }
    {
        std::vector<limb> buf(2 + 2 * guard_limbs, poison);
        fn(buf.data() + guard_limbs, a, 2, b, 0);
        for (const limb x : buf) {
            EXPECT_EQ(x, poison);
        }
    }
}

// Every (len_a, len_b) pair in [1, 40] x [1, 40]: both orders (len_a < len_b
// and len_a > len_b) exercise the kernel's internal operand swap, and every
// length covers every len & 3 remainder for the 4x unrolled row body.
TEST_P(MultiplyLongRuntime, AllOnes) {
    const multiply_fn fn = GetParam().fn;

    for (std::size_t len_a = 1; len_a <= max_len; ++len_a) {
        for (std::size_t len_b = 1; len_b <= max_len; ++len_b) {
            expect_multiply_matches(
                fn, std::vector<limb>(len_a, limb_max), std::vector<limb>(len_b, limb_max), "all-ones");
        }
    }
}

TEST_P(MultiplyLongRuntime, Random) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x8B1D2A57ULL};
    std::uniform_int_distribution<limb> dist;
    for (std::size_t len_a = 1; len_a <= max_len; ++len_a) {
        for (std::size_t len_b = 1; len_b <= max_len; ++len_b) {
            std::vector<limb> a(len_a);
            std::vector<limb> b(len_b);
            for (limb& x : a) {
                x = dist(rng);
            }
            for (limb& x : b) {
                x = dist(rng);
            }
            expect_multiply_matches(fn, a, b, "random");
        }
    }
}

// Sparse and structured operands: long runs of zero limbs on either side, so
// whole rows or columns of the schoolbook grid are pure carry propagation
// with no product term.
TEST_P(MultiplyLongRuntime, Sparse) {
    const multiply_fn fn = GetParam().fn;

    for (std::size_t len_a = 1; len_a <= max_len; ++len_a) {
        for (std::size_t len_b = 1; len_b <= max_len; ++len_b) {
            std::vector<limb> ends_a(len_a, limb{0});
            ends_a.front() = limb_max;
            ends_a.back()  = limb_max;

            std::vector<limb> alternating_b(len_b);
            for (std::size_t i = 0; i < len_b; ++i) {
                alternating_b[i] = (i % 2 == 0) ? top_bit : limb{0};
            }
            expect_multiply_matches(fn, ends_a, alternating_b, "sparse-ends-vs-alternating");

            std::vector<limb> every_third_a(len_a, limb{0});
            for (std::size_t i = 0; i < len_a; i += 3) {
                every_third_a[i] = limb_max;
            }

            std::vector<limb> tail_b(len_b, limb{0});
            tail_b.back() = limb_max;
            expect_multiply_matches(fn, every_third_a, tail_b, "sparse-every-third-vs-tail");
        }
    }
}

// Shapes past the 4x unrolled small grid, including one much longer than the
// other in both directions.
TEST_P(MultiplyLongRuntime, LargeShapes) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x3C9E7F41ULL};
    std::uniform_int_distribution<limb> dist;

    auto random_vec = [&](std::size_t n) {
        std::vector<limb> v(n);
        for (limb& x : v) {
            x = dist(rng);
        }
        return v;
    };

    expect_multiply_matches(fn, random_vec(97), random_vec(160), "large");
    expect_multiply_matches(fn, random_vec(160), random_vec(97), "large");
    expect_multiply_matches(fn, random_vec(200), random_vec(200), "large");
}

// Extreme aspect ratios: a single row multiplying (or multiplied by) a very
// long operand, and a few-limbs-by-many-limbs shape that drives the
// row-pair path's steady loop for a long time on one side while the other
// side is too short to ever pair.
TEST_P(MultiplyLongRuntime, UnbalancedShapes) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x1B4E5A7DULL};
    std::uniform_int_distribution<limb> dist;

    auto random_vec = [&](std::size_t n) {
        std::vector<limb> v(n);
        for (limb& x : v) {
            x = dist(rng);
        }
        return v;
    };

    expect_multiply_matches(fn, random_vec(1), random_vec(700), "unbalanced");
    expect_multiply_matches(fn, random_vec(700), random_vec(1), "unbalanced");
    expect_multiply_matches(fn, random_vec(2), random_vec(513), "unbalanced");
    expect_multiply_matches(fn, random_vec(3), random_vec(500), "unbalanced");
    expect_multiply_matches(fn, random_vec(47), random_vec(1000), "unbalanced");
    expect_multiply_matches(fn, random_vec(1000), random_vec(47), "unbalanced");
}

// Every len_a in [1, 130] against a handful of representative len_b: the
// BMI2/ADX kernel's exact-length tier only covers len_a 1..16 (LA_EXACT_MAX),
// with dedicated code per length there and 16-limb blocks plus a per-remainder
// straight-line copy above that, so every length in between must be hit too.
TEST_P(MultiplyLongRuntime, WideLenA) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x6F2C8114ULL};
    std::uniform_int_distribution<limb> dist;

    for (std::size_t len_a = 1; len_a <= wide_len_a_max; ++len_a) {
        for (const std::size_t len_b : wide_len_b_values) {
            std::vector<limb> a(len_a);
            std::vector<limb> b(len_b);
            for (limb& x : a) {
                x = dist(rng);
            }
            for (limb& x : b) {
                x = dist(rng);
            }
            expect_multiply_matches(fn, a, b, "wide-len-a");
        }
    }
}

// Same length grid as WideLenA, but every limb drawn from a mix of {0, 1,
// limb_max, top_bit, top_bit - 1, limb_max - 1} or a genuine random value,
// several seeds: stresses the folded BMI2/ADX square's carried top bits and
// its all-ones overflow path (also exercised here through the general multiply).
TEST_P(MultiplyLongRuntime, Adversarial) {
    const multiply_fn fn = GetParam().fn;

    for (unsigned seed = 0; seed < 4; ++seed) {
        std::mt19937_64 rng{0x0BADC0DEULL + seed};
        for (std::size_t len_a = 1; len_a <= wide_len_a_max; ++len_a) {
            for (const std::size_t len_b : wide_len_b_values) {
                std::vector<limb> a(len_a);
                std::vector<limb> b(len_b);
                fill_adversarial(a, rng);
                fill_adversarial(b, rng);
                expect_multiply_matches(fn, a, b, "adversarial");
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kernels,
                         MultiplyLongRuntime,
                         ::testing::ValuesIn(all_kernels()),
                         [](const ::testing::TestParamInfo<kernel>& tpi) { return std::string(tpi.param.name); });

} // namespace
