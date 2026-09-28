// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Direct tests of beman_big_int_square_long_runtime (the assembly kernel
// (x86_64/AArch64), or its portable fallback elsewhere) against
// multiply_long(a, a). On x86-64, also runs every check against the two
// underlying kernels directly (the generic baseline and the BMI2+ADX kernel,
// the latter skipped when the running CPU lacks either extension), not just
// the compile-time-selected forwarder.

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/square_long_runtime.hpp>

#include "x86_64_bmi2_adx_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <limits>
#include <ostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using limb = ::beman::big_int::uint_multiprecision_t;

constexpr limb        limb_max    = std::numeric_limits<limb>::max();
constexpr limb        poison      = limb_max / 0xFF * 0xA5; // 0xA5 in every byte
constexpr std::size_t guard_limbs = 4;
constexpr std::size_t max_limbs   = 260;
constexpr limb        top_bit     = limb{1} << (std::numeric_limits<limb>::digits - 1);

// Extreme limb values that stress the folded BMI2/ADX square's carried top
// bits and the all-ones overflow path. Each limb of an adversarial operand
// independently picks one of these, or a genuinely random value.
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

using square_fn = void (*)(limb*, const limb*, std::size_t);

#if defined(BEMAN_BIG_INT_ARCH_X86_64)
void call_generic(limb* r, const limb* a, const std::size_t n) {
    ::beman_big_int_square_long_runtime_generic(r, a, n);
}
void call_bmi2_adx(limb* r, const limb* a, const std::size_t n) {
    ::beman_big_int_square_long_runtime_bmi2_adx(r, a, n);
}
#endif

void call_selected(limb* r, const limb* a, const std::size_t n) { ::beman_big_int_square_long_runtime(r, a, n); }

// One kernel under test: a name (also the gtest instance name) and the
// function to call. The "Bmi2Adx" kernel GTEST_SKIP()s unless the running
// CPU has both BMI2 and ADX.
struct kernel {
    const char* name;
    square_fn   fn;
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

// Squares `a` through `fn` into a poisoned buffer framed by guard limbs,
// checking the product against multiply_long and that nothing outside
// [0, 2n) is written.
void expect_square_matches(square_fn fn, const std::vector<limb>& a, const char* pattern) {
    const std::size_t n = a.size();

    std::vector<limb> expected(2 * n);
    ::beman::big_int::detail::multiply_long(expected, a, a);

    std::vector<limb> buf(2 * n + 2 * guard_limbs, poison);
    fn(buf.data() + guard_limbs, a.data(), n);

    for (std::size_t k = 0; k < guard_limbs; ++k) {
        ASSERT_EQ(buf[k], poison) << pattern << " n=" << n << " leading guard " << k;
        ASSERT_EQ(buf[guard_limbs + 2 * n + k], poison) << pattern << " n=" << n << " trailing guard " << k;
    }
    for (std::size_t k = 0; k < 2 * n; ++k) {
        ASSERT_EQ(buf[guard_limbs + k], expected[k]) << pattern << " n=" << n << " limb " << k;
    }
}

class SquareLongRuntime : public ::testing::TestWithParam<kernel> {
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

TEST_P(SquareLongRuntime, EmptyOperandWritesNothing) {
    const square_fn fn = GetParam().fn;

    std::vector<limb> buf(2 * guard_limbs, poison);
    const limb        a = 1;
    fn(buf.data() + guard_limbs, &a, 0);
    for (const limb x : buf) {
        EXPECT_EQ(x, poison);
    }
}

// All-ones limbs drive every carry to its maximum: the triangle rows carry out
// every limb and the diagonal pass sees the largest `extra`.
TEST_P(SquareLongRuntime, AllOnes) {
    const square_fn fn = GetParam().fn;

    for (std::size_t n = 1; n <= max_limbs; ++n) {
        expect_square_matches(fn, std::vector<limb>(n, limb_max), "all-ones");
    }
}

TEST_P(SquareLongRuntime, Random) {
    const square_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x5157A12EULL};
    std::uniform_int_distribution<limb> dist;
    for (unsigned trial = 0; trial < 4; ++trial) {
        for (std::size_t n = 1; n <= max_limbs; ++n) {
            std::vector<limb> a(n);
            for (limb& x : a) {
                x = dist(rng);
            }
            expect_square_matches(fn, a, "random");
        }
    }
}

// Sparse and structured operands: zero limbs inside the triangle, lone high
// bits that make the doubling shift carry between limb pairs, and zero
// multipliers that leave whole rows as pure carry propagation.
TEST_P(SquareLongRuntime, Structured) {
    const square_fn fn = GetParam().fn;

    for (std::size_t n = 1; n <= max_limbs; ++n) {
        std::vector<limb> a(n, top_bit);
        expect_square_matches(fn, a, "top-bit");

        std::vector<limb> alternating(n);
        for (std::size_t i = 0; i < n; ++i) {
            alternating[i] = (i % 2 == 0) ? limb_max : limb{0};
        }
        expect_square_matches(fn, alternating, "alternating");

        std::vector<limb> ends(n, limb{0});
        ends.front() = limb_max;
        ends.back()  = limb_max;
        expect_square_matches(fn, ends, "ends");

        std::vector<limb> one_less(n, limb_max);
        one_less.front() = limb_max - 1;
        expect_square_matches(fn, one_less, "all-ones-minus-one");
    }
}

// Every limb drawn from a mix of {0, 1, limb_max, top_bit, top_bit - 1,
// limb_max - 1} or a genuine random value, several seeds, every n: stresses
// the folded BMI2/ADX square's carried top bits and its all-ones overflow path.
TEST_P(SquareLongRuntime, Adversarial) {
    const square_fn fn = GetParam().fn;

    for (unsigned seed = 0; seed < 4; ++seed) {
        std::mt19937_64 rng{0x0BADC0DEULL + seed};
        for (std::size_t n = 1; n <= max_limbs; ++n) {
            std::vector<limb> a(n);
            fill_adversarial(a, rng);
            expect_square_matches(fn, a, "adversarial");
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kernels,
                         SquareLongRuntime,
                         ::testing::ValuesIn(all_kernels()),
                         [](const ::testing::TestParamInfo<kernel>& tpi) { return std::string(tpi.param.name); });

} // namespace
