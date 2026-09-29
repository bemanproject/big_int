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

#include "x86_64_avx512_ifma_support.hpp"
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

#if defined(__unix__) || defined(__APPLE__)
    #include <sys/mman.h>
    #include <unistd.h>
    #define BEMAN_BIG_INT_TESTS_HAVE_MMAN 1
#endif

namespace {

using limb = ::beman::big_int::uint_multiprecision_t;

constexpr limb        limb_max    = std::numeric_limits<limb>::max();
constexpr limb        poison      = limb_max / 0xFF * 0xA5; // 0xA5 in every byte
constexpr std::size_t guard_limbs = 4;
// n = 1..300 exhaustive: covers the AVX-512 IFMA square kernel's native range
// (up to ifma_square_native_max_limbs = 256) and a margin past it, where it
// tail-calls the BMI2/ADX kernel.
constexpr std::size_t max_limbs = 300;
constexpr limb        top_bit   = limb{1} << (std::numeric_limits<limb>::digits - 1);

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
void call_avx512_ifma(limb* r, const limb* a, const std::size_t n) {
    ::beman_big_int_square_long_runtime_avx512_ifma(r, a, n);
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
    ks.push_back({"Avx512Ifma", &call_avx512_ifma});
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
        if (name == "Bmi2Adx") {
            if (!::beman::big_int::tests::cpu_has_bmi2_and_adx()) {
                GTEST_SKIP() << "CPU lacks BMI2 and/or ADX";
            }
            if (!::beman::big_int::tests::bmi2_adx_kernels_are_usable()) {
                GTEST_SKIP() << "CPUID claims BMI2/ADX but the instructions fault (emulator)";
            }
        } else if (name == "Avx512Ifma") {
            if (!::beman::big_int::tests::avx512_ifma_kernels_are_usable()) {
                GTEST_SKIP() << "CPU lacks AVX-512 IFMA (or a required companion feature), or the "
                             << "instructions fault (emulator)";
            }
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

// Just below/at/above ifma_square_native_max_limbs (256), where the IFMA
// square kernel switches from its native path to tail-calling BMI2/ADX, plus
// a few larger sizes on the tail-call side.
TEST_P(SquareLongRuntime, NearIfmaNativeMax) {
    const square_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x2565A1A1ULL};
    std::uniform_int_distribution<limb> dist;

    constexpr std::size_t n_values[] = {254, 255, 256, 257, 258, 300, 400, 512, 1000};
    for (const std::size_t n : n_values) {
        std::vector<limb> a(n);
        for (limb& x : a) {
            x = dist(rng);
        }
        expect_square_matches(fn, a, "near-ifma-native-max");
        expect_square_matches(fn, std::vector<limb>(n, limb_max), "near-ifma-native-max-all-ones");
    }
}

#if defined(BEMAN_BIG_INT_TESTS_HAVE_MMAN)

// A buffer whose data is placed flush against one PROT_NONE guard page, so
// any read or write straying outside it faults immediately instead of
// silently landing in padding.
class guard_page_buffer {
  public:
    enum class side { leading, trailing };

    guard_page_buffer(const std::size_t byte_count, const side s) {
        page_size_                   = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        const std::size_t data_pages = (byte_count + page_size_ - 1) / page_size_;
        total_bytes_                 = (data_pages + 1) * page_size_;
        base_ = mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base_ == MAP_FAILED) {
            std::abort();
        }
        auto* const bytes = static_cast<std::byte*>(base_);
        if (s == side::trailing) {
            guard_ = bytes + data_pages * page_size_;
            data_  = static_cast<std::byte*>(guard_) - byte_count;
        } else {
            guard_ = bytes;
            data_  = bytes + page_size_;
        }
        if (mprotect(guard_, page_size_, PROT_NONE) != 0) {
            std::abort();
        }
    }
    ~guard_page_buffer() { munmap(base_, total_bytes_); }
    guard_page_buffer(const guard_page_buffer&)            = delete;
    guard_page_buffer& operator=(const guard_page_buffer&) = delete;

    [[nodiscard]] void* data() const { return data_; }

  private:
    void*       base_;
    void*       guard_{};
    void*       data_{};
    std::size_t page_size_;
    std::size_t total_bytes_;
};

void expect_square_matches_guarded(square_fn fn, const std::size_t n, guard_page_buffer::side side) {
    std::mt19937_64                     rng{0xD00D5EEDULL + n * 0x9E3779B9ULL};
    std::uniform_int_distribution<limb> dist;

    std::vector<limb> a_src(n);
    for (limb& x : a_src) {
        x = dist(rng);
    }

    std::vector<limb> expected(2 * n);
    ::beman::big_int::detail::multiply_long(expected, a_src, a_src);

    guard_page_buffer a_buf(n * sizeof(limb), side);
    guard_page_buffer r_buf(2 * n * sizeof(limb), side);

    auto* const a = static_cast<limb*>(a_buf.data());
    auto* const r = static_cast<limb*>(r_buf.data());
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = a_src[i];
    }

    fn(r, a, n);

    for (std::size_t k = 0; k < 2 * n; ++k) {
        ASSERT_EQ(r[k], expected[k]) << "guard-page n=" << n << " limb " << k;
    }
}

TEST_P(SquareLongRuntime, GuardPages) {
    const square_fn fn = GetParam().fn;

    constexpr guard_page_buffer::side sides[] = {guard_page_buffer::side::trailing, guard_page_buffer::side::leading};
    constexpr std::size_t             extra_n[] = {105, 157};

    for (const auto side : sides) {
        for (std::size_t n = 1; n <= 64; ++n) {
            expect_square_matches_guarded(fn, n, side);
        }
        for (const std::size_t n : extra_n) {
            expect_square_matches_guarded(fn, n, side);
        }
    }
}

#endif // defined(BEMAN_BIG_INT_TESTS_HAVE_MMAN)

INSTANTIATE_TEST_SUITE_P(Kernels,
                         SquareLongRuntime,
                         ::testing::ValuesIn(all_kernels()),
                         [](const ::testing::TestParamInfo<kernel>& tpi) { return std::string(tpi.param.name); });

} // namespace
