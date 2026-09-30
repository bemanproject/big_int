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

#include "x86_64_avx512_ifma_support.hpp"
#include "x86_64_bmi2_adx_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <new>
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
void call_avx512_ifma(limb* r, const limb* a, const std::size_t len_a, const limb* b, const std::size_t len_b) {
    ::beman_big_int_multiply_long_runtime_avx512_ifma(r, a, len_a, b, len_b);
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
    ks.push_back({"Avx512Ifma", &call_avx512_ifma});
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

// Returns a value whose low `nbits` bits are 1 and the rest are 0, packed
// into ceil(nbits / 64) limbs: the pattern of an AVX-512 IFMA accumulator
// whose low k 52-bit digits are all-ones (nbits = 52 * k).
std::vector<limb> low_bits_ones(std::size_t nbits) {
    std::vector<limb> v((nbits + 63) / 64, limb{0});
    for (limb& x : v) {
        const std::size_t take = nbits < 64 ? nbits : 64;
        x                      = (take == 64) ? limb_max : ((limb{1} << take) - 1);
        nbits -= take;
    }
    return v;
}

// Boundary shapes around the IFMA kernel's internal tiering: multiples of 8
// limbs (the AVX-512 register packs 8 limbs), the neighborhood of the
// IFMA dispatch gate (both sides of each tier of ifma_multiply_worthwhile), and very
// unbalanced huge-by-small shapes.
TEST_P(MultiplyLongRuntime, Avx512IfmaBoundaryShapes) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x1FA57000ULL};
    std::uniform_int_distribution<limb> dist;
    auto                                random_vec = [&](std::size_t n) {
        std::vector<limb> v(n);
        for (limb& x : v) {
            x = dist(rng);
        }
        return v;
    };

    constexpr std::size_t lb_values[] = {103, 104, 105, 207, 208, 209, 313};
    for (const std::size_t lb : lb_values) {
        for (std::size_t la = lb; la <= lb + 12; ++la) {
            expect_multiply_matches(fn, random_vec(la), random_vec(lb), "ifma-boundary-la-near-lb");
        }
        expect_multiply_matches(fn, random_vec(1000), random_vec(lb), "ifma-boundary-la-1000");
        expect_multiply_matches(fn, random_vec(4099), random_vec(lb), "ifma-boundary-la-4099");
    }

#if defined(BEMAN_BIG_INT_ARCH_X86_64)
    // Both sides of the IFMA dispatch gate: for each short length around its tiers, the last la the gate refuses
    // and the first it accepts (found through the gate itself), in both operand orders.
    namespace d = ::beman::big_int::detail;
    for (std::size_t lo = d::ifma_multiply_min_limbs - 1; lo <= d::ifma_multiply_always_limbs + 1; ++lo) {
        if (lo == 0) {
            continue;
        }
        for (std::size_t la = lo; la <= 2000; ++la) {
            if (d::ifma_multiply_worthwhile(la, lo)) {
                for (const std::size_t n : {la - 1, la}) {
                    if (n >= lo) {
                        expect_multiply_matches(fn, random_vec(n), random_vec(lo), "ifma-gate-boundary");
                        expect_multiply_matches(fn, random_vec(lo), random_vec(n), "ifma-gate-boundary-swapped");
                    }
                }
                break;
            }
        }
    }
#endif

    for (std::size_t t = 0; t <= 12; ++t) {
        for (std::size_t lb = 1; lb <= 20; ++lb) {
            expect_multiply_matches(fn, random_vec(1000 + t), random_vec(lb), "ifma-boundary-la-1000-plus-t");
        }
    }

    constexpr std::size_t la_values[]  = {10007, 100003};
    constexpr std::size_t lb2_values[] = {1, 2, 3, 5, 8, 13, 21, 46, 47, 104, 105};
    for (const std::size_t la : la_values) {
        for (const std::size_t lb : lb2_values) {
            expect_multiply_matches(fn, random_vec(la), random_vec(lb), "ifma-boundary-huge-la");
        }
    }
}

// All-ones squares up to and past the IFMA square kernel's native range, plus
// a few unequal chunked shapes.
TEST_P(MultiplyLongRuntime, AllOnesUpTo300) {
    const multiply_fn fn = GetParam().fn;

    for (std::size_t len = 1; len <= 300; ++len) {
        expect_multiply_matches(
            fn, std::vector<limb>(len, limb_max), std::vector<limb>(len, limb_max), "all-ones-300");
    }

    constexpr std::size_t chunked_la[] = {21, 300, 104, 209, 46};
    constexpr std::size_t chunked_lb[] = {300, 21, 209, 104, 300};
    for (std::size_t i = 0; i < std::size(chunked_la); ++i) {
        expect_multiply_matches(fn,
                                std::vector<limb>(chunked_la[i], limb_max),
                                std::vector<limb>(chunked_lb[i], limb_max),
                                "all-ones-chunked");
    }
}

// (2^(52k) - 1) against all-ones: the pattern whose low 52k bits are ones,
// the shape an IFMA accumulator sees when every one of its radix-2^52 digits
// is saturated.
TEST_P(MultiplyLongRuntime, Radix52Pattern) {
    const multiply_fn fn = GetParam().fn;

    constexpr std::size_t k_values[]  = {1, 2, 3, 4, 6, 8, 13, 16, 25, 50};
    constexpr std::size_t ones_lens[] = {1, 5, 21, 46, 105};
    for (const std::size_t k : k_values) {
        const std::vector<limb> pattern = low_bits_ones(52 * k);
        for (const std::size_t ones_len : ones_lens) {
            expect_multiply_matches(fn, pattern, std::vector<limb>(ones_len, limb_max), "radix52-vs-ones");
            expect_multiply_matches(fn, std::vector<limb>(ones_len, limb_max), pattern, "ones-vs-radix52");
        }
    }
}

// (2^(64n) - 1) * (2^64 + 1): an all-ones operand against the two-limb {1, 1}
// pattern, exercising the "replicate and shift-add by one limb" carry chain.
TEST_P(MultiplyLongRuntime, AllOnesTimesTwoPow64PlusOne) {
    const multiply_fn fn = GetParam().fn;

    constexpr std::size_t   n_values[] = {1, 2, 5, 10, 21, 46, 105, 300};
    const std::vector<limb> b          = {limb{1}, limb{1}}; // 2^64 + 1

    for (const std::size_t n : n_values) {
        expect_multiply_matches(fn, std::vector<limb>(n, limb_max), b, "all-ones-times-2p64p1");
        expect_multiply_matches(fn, b, std::vector<limb>(n, limb_max), "2p64p1-times-all-ones");
    }
}

// Multiplies `v.first(len_a)` by `v.first(len_b)` with BOTH operands
// pointing into the same backing array (p_a == p_b), covering equal-length
// squaring-through-the-multiply-entry-point and prefix-length aliasing.
void expect_multiply_matches_aliased(multiply_fn              fn,
                                     const std::vector<limb>& v,
                                     const std::size_t        len_a,
                                     const std::size_t        len_b,
                                     const char*              pattern) {
    std::vector<limb> expected(len_a + len_b);
    ::beman::big_int::detail::multiply_long(
        expected, std::span<const limb>(v).first(len_a), std::span<const limb>(v).first(len_b));

    std::vector<limb> buf(len_a + len_b + 2 * guard_limbs, poison);
    fn(buf.data() + guard_limbs, v.data(), len_a, v.data(), len_b);

    for (std::size_t k = 0; k < guard_limbs; ++k) {
        ASSERT_EQ(buf[k], poison) << pattern << " leading guard " << k;
        ASSERT_EQ(buf[guard_limbs + len_a + len_b + k], poison) << pattern << " trailing guard " << k;
    }
    for (std::size_t k = 0; k < len_a + len_b; ++k) {
        ASSERT_EQ(buf[guard_limbs + k], expected[k]) << pattern << " limb " << k;
    }
}

TEST_P(MultiplyLongRuntime, AliasedOperands) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0xA11A5EDULL};
    std::uniform_int_distribution<limb> dist;

    constexpr std::size_t lens[] = {1, 2, 3, 21, 46, 105, 209, 300};
    for (const std::size_t len : lens) {
        std::vector<limb> v(len);
        for (limb& x : v) {
            x = dist(rng);
        }
        expect_multiply_matches_aliased(fn, v, len, len, "aliased-equal-length");
    }

    constexpr std::size_t la_values[] = {2, 46, 105, 209, 300};
    for (const std::size_t la : la_values) {
        std::vector<limb> v(la);
        for (limb& x : v) {
            x = dist(rng);
        }
        for (std::size_t lb = 1; lb < la; lb += (la / 4 == 0 ? 1 : la / 4)) {
            expect_multiply_matches_aliased(fn, v, la, lb, "aliased-prefix");
        }
    }
}

// A 64-byte-aligned allocation with room to slide the returned pointer by
// 0..7 limbs (0..56 bytes), so a caller can place a buffer at any 8-byte
// alignment mod 64 -- the width an AVX-512 (ZMM) load can straddle.
class aligned_storage_64 {
  public:
    explicit aligned_storage_64(const std::size_t limb_count)
        : bytes_((limb_count + 7) * sizeof(limb)),
          ptr_(static_cast<limb*>(::operator new(bytes_, std::align_val_t{64}))) {}
    ~aligned_storage_64() { ::operator delete(ptr_, bytes_, std::align_val_t{64}); }
    aligned_storage_64(const aligned_storage_64&)            = delete;
    aligned_storage_64& operator=(const aligned_storage_64&) = delete;

    [[nodiscard]] limb* at_offset(const std::size_t limb_offset) const { return ptr_ + limb_offset; }

  private:
    std::size_t bytes_;
    limb*       ptr_;
};

// Places a, b, and r (one at a time, the other two fixed at offset 0) at
// every 0..7-limb offset mod 64 bytes.
TEST_P(MultiplyLongRuntime, OperandAlignmentOffsets) {
    const multiply_fn fn = GetParam().fn;

    constexpr std::size_t len_a = 37;
    constexpr std::size_t len_b = 29;

    std::vector<limb>                   a_src(len_a);
    std::vector<limb>                   b_src(len_b);
    std::mt19937_64                     rng{0x0FF5E7ULL};
    std::uniform_int_distribution<limb> dist;
    for (limb& x : a_src) {
        x = dist(rng);
    }
    for (limb& x : b_src) {
        x = dist(rng);
    }

    std::vector<limb> expected(len_a + len_b);
    ::beman::big_int::detail::multiply_long(expected, a_src, b_src);

    for (std::size_t off = 0; off < 8; ++off) {
        for (int which = 0; which < 3; ++which) {
            aligned_storage_64 a_store(len_a);
            aligned_storage_64 b_store(len_b);
            aligned_storage_64 r_store(len_a + len_b);

            limb* a = a_store.at_offset(which == 0 ? off : 0);
            limb* b = b_store.at_offset(which == 1 ? off : 0);
            limb* r = r_store.at_offset(which == 2 ? off : 0);
            for (std::size_t i = 0; i < len_a; ++i) {
                a[i] = a_src[i];
            }
            for (std::size_t i = 0; i < len_b; ++i) {
                b[i] = b_src[i];
            }

            fn(r, a, len_a, b, len_b);

            for (std::size_t k = 0; k < len_a + len_b; ++k) {
                ASSERT_EQ(r[k], expected[k]) << "which=" << which << " offset=" << off << " limb " << k;
            }
        }
    }
}

// Fixed-seed differential fuzz against detail::multiply_long over a broad,
// randomly sampled shape range.
TEST_P(MultiplyLongRuntime, DifferentialFuzz) {
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                            rng{0xF0231D5EULL};
    std::uniform_int_distribution<std::size_t> len_a_dist(1, 5000);
    std::uniform_int_distribution<std::size_t> len_b_dist(1, 400);
    std::uniform_int_distribution<limb>        dist;

    constexpr int cases = 200;
    for (int i = 0; i < cases; ++i) {
        const std::size_t len_a = len_a_dist(rng);
        const std::size_t len_b = len_b_dist(rng);
        std::vector<limb> a(len_a);
        std::vector<limb> b(len_b);
        for (limb& x : a) {
            x = dist(rng);
        }
        for (limb& x : b) {
            x = dist(rng);
        }
        expect_multiply_matches(fn, a, b, "differential-fuzz");
    }
}

// 46 x 1,000,000, both orders: only run for the AVX-512 IFMA kernel (well
// past its dispatch gate) and the compile-time-selected forwarder. The
// generic and BMI2/ADX kernels are already covered at every other shape in
// this file; repeating a million-limb operand against them here would just
// add runtime without adding coverage.
TEST_P(MultiplyLongRuntime, WideByHuge) {
    const std::string_view name = GetParam().name;
    if (name != "Avx512Ifma" && name != "Selected") {
        GTEST_SKIP() << "46 x 1,000,000 only runs for Avx512Ifma and Selected";
    }
    const multiply_fn fn = GetParam().fn;

    std::mt19937_64                     rng{0x8000000ULL};
    std::uniform_int_distribution<limb> dist;
    auto                                random_vec = [&](std::size_t n) {
        std::vector<limb> v(n);
        for (limb& x : v) {
            x = dist(rng);
        }
        return v;
    };

    // Not named "small": that identifier is a macro (expanding to "char") on
    // MSVC, pulled in transitively by <windows.h> in the AVX-512 IFMA/BMI2+ADX
    // support headers.
    const std::vector<limb> narrow = random_vec(46);
    const std::vector<limb> huge   = random_vec(1'000'000);
    expect_multiply_matches(fn, narrow, huge, "wide-by-huge");
    expect_multiply_matches(fn, huge, narrow, "huge-by-wide");
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

void expect_multiply_matches_guarded(multiply_fn             fn,
                                     const std::size_t       len_a,
                                     const std::size_t       len_b,
                                     guard_page_buffer::side side) {
    std::mt19937_64                     rng{0xD00D5EEDULL + len_a * 0x9E3779B9ULL + len_b};
    std::uniform_int_distribution<limb> dist;

    std::vector<limb> a_src(len_a);
    std::vector<limb> b_src(len_b);
    for (limb& x : a_src) {
        x = dist(rng);
    }
    for (limb& x : b_src) {
        x = dist(rng);
    }

    std::vector<limb> expected(len_a + len_b);
    ::beman::big_int::detail::multiply_long(expected, a_src, b_src);

    guard_page_buffer a_buf(len_a * sizeof(limb), side);
    guard_page_buffer b_buf(len_b * sizeof(limb), side);
    guard_page_buffer r_buf((len_a + len_b) * sizeof(limb), side);

    auto* const a = static_cast<limb*>(a_buf.data());
    auto* const b = static_cast<limb*>(b_buf.data());
    auto* const r = static_cast<limb*>(r_buf.data());
    for (std::size_t i = 0; i < len_a; ++i) {
        a[i] = a_src[i];
    }
    for (std::size_t i = 0; i < len_b; ++i) {
        b[i] = b_src[i];
    }

    fn(r, a, len_a, b, len_b);

    for (std::size_t k = 0; k < len_a + len_b; ++k) {
        ASSERT_EQ(r[k], expected[k]) << "guard-page len_a=" << len_a << " len_b=" << len_b << " limb " << k;
    }
}

TEST_P(MultiplyLongRuntime, GuardPages) {
    const multiply_fn fn = GetParam().fn;

    constexpr guard_page_buffer::side sides[] = {guard_page_buffer::side::trailing, guard_page_buffer::side::leading};

    for (const auto side : sides) {
        for (std::size_t len = 1; len <= 64; ++len) {
            expect_multiply_matches_guarded(fn, len, len, side);
            expect_multiply_matches_guarded(fn, len, 3, side);
            expect_multiply_matches_guarded(fn, 3, len, side);
        }
        expect_multiply_matches_guarded(fn, 5, 3000, side);
        expect_multiply_matches_guarded(fn, 3000, 5, side);
        expect_multiply_matches_guarded(fn, 105, 105, side);
        expect_multiply_matches_guarded(fn, 157, 157, side);
    }
}

#endif // defined(BEMAN_BIG_INT_TESTS_HAVE_MMAN)

INSTANTIATE_TEST_SUITE_P(Kernels,
                         MultiplyLongRuntime,
                         ::testing::ValuesIn(all_kernels()),
                         [](const ::testing::TestParamInfo<kernel>& tpi) { return std::string(tpi.param.name); });

} // namespace
