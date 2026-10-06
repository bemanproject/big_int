// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Steady-state heap allocations per operation. Every case runs one warm-up call (so destinations reach their working
// capacity), then K calls, and reports the allocator traffic per call. Three environments:
//   counting  basic_big_int<64, limb, counting_allocator<limb>>  (stateful allocator, util_counting_allocator.hpp)
//   pmr       pmr::big_int with a counting memory_resource installed through std::pmr::set_default_resource, so
//             results (which take select_on_container_copy_construction of an operand allocator) land on it
//   wide256   basic_big_int<256, limb, counting_allocator<limb>>  (inline capacity of 4 64-bit limbs)
// The measured counts are printed to stdout as "[alloc_count] ...". The expectations below are the item 1 targets
// (see the front-end cost plan); cases that exceed their target at the baseline (cf1cf54) are marked pending and skipped
// while `item1_pending` is true. Drop the constant (and the baseline columns) when the library changes land.
//
// Baseline counts measured at cf1cf54 (allocations per call, K = 64 after one warm-up call; `want` is the target the
// case asserts; the cases whose baseline exceeds `want` are the pending ones):
//
//   case                                  want            counting  pmr   wide256
//   ----------------------------------------------------------------------------
//   c = a + b (16x16)                     1               1         1     1
//   c = a - b (16x16)                     1               1         1     1
//   c += b; c -= b (16x16)                0               0         0     0
//   c = a << 13 (16 limbs)                1               1         1     1
//   c = a >> 13 (16 limbs)                1               1         1     1
//   c <<= 13; c >>= 13 (16 limbs)         0               0         0     0
//   c = a * b (8x8)                       1               1         1     1
//   c = a; c *= b (8x8 -> 16)             0               1         1     1
//   c = a; c *= 7 (8 limbs)               0               1         1     1
//   c = a / b (8x4)                       1               2         2     2
//   c = a % b (8x4)                       1               2         2     2
//   div_rem_to_zero (8x4)                 <= 2            5         5     4
//   c = a / b (2x1)                       0 (>= 2 inline) 1         1     0
//   c = a; c /= b (8x4) [stretch]         0               3         3     2
//   c = a; c %= b (8x4) [stretch]         0               2         2     2
//   from_chars 2000 digits (reserved)     <= 2            2         2     2
//
// pending at the baseline: every row with a baseline above `want` (the compound `*=`, `/=` and `%=`, `a / b`, `a % b`
// and div_rem_to_zero). The add/sub/shift/mul rows already meet their target: the item 1 gain there is in the copy and
// zero-fill work, not the allocation count.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory_resource>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "util/util_counting_allocator.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::div_rem_to_zero;
using BEMAN_BIG_INT_NAMESPACE::from_chars;
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using BEMAN_BIG_INT_NAMESPACE::test_util::counting_allocator;
using BEMAN_BIG_INT_NAMESPACE::test_util::counting_state;

using limb = uint_multiprecision_t;

// Cases that exceed their target at the baseline are skipped while this is true.
constexpr bool item1_pending = true;

constexpr int calls = 64; // K

std::vector<limb> random_limbs(const std::size_t n, std::uint64_t seed) {
    std::vector<limb> v(n);
    for (auto& x : v) {
        seed ^= seed << 13U;
        seed ^= seed >> 7U;
        seed ^= seed << 17U;
        x = static_cast<limb>(seed);
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// ----- environments -----

struct counting_env {
    static constexpr const char* name  = "counting";
    static constexpr std::size_t index = 0;

    using int_t = basic_big_int<64, limb, counting_allocator<limb>>;

    counting_state           state;
    counting_allocator<limb> alloc{&state};

    [[nodiscard]] int_t       empty() { return int_t(alloc); }
    [[nodiscard]] int_t       make(const std::vector<limb>& v) { return int_t(v.begin(), v.end(), alloc); }
    [[nodiscard]] std::size_t count() const { return state.total_allocations; }
};

struct wide256_env {
    static constexpr const char* name  = "wide256";
    static constexpr std::size_t index = 2;

    using int_t = basic_big_int<256, limb, counting_allocator<limb>>;

    counting_state           state;
    counting_allocator<limb> alloc{&state};

    [[nodiscard]] int_t       empty() { return int_t(alloc); }
    [[nodiscard]] int_t       make(const std::vector<limb>& v) { return int_t(v.begin(), v.end(), alloc); }
    [[nodiscard]] std::size_t count() const { return state.total_allocations; }
};

class counting_resource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t allocations() const noexcept { return m_allocs; }

  private:
    [[nodiscard]] void* do_allocate(const std::size_t bytes, const std::size_t align) override {
        ++m_allocs;
        return std::pmr::new_delete_resource()->allocate(bytes, align);
    }
    void do_deallocate(void* p, const std::size_t bytes, const std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::size_t m_allocs{0};
};

struct pmr_env {
    static constexpr const char* name  = "pmr";
    static constexpr std::size_t index = 1;

    using int_t = BEMAN_BIG_INT_NAMESPACE::pmr::big_int;

    counting_resource          resource;
    std::pmr::memory_resource* previous;

    pmr_env() : previous(std::pmr::set_default_resource(&resource)) {}
    ~pmr_env() { std::pmr::set_default_resource(previous); }
    pmr_env(const pmr_env&)            = delete;
    pmr_env& operator=(const pmr_env&) = delete;

    [[nodiscard]] int_t       empty() { return int_t(); }
    [[nodiscard]] int_t       make(const std::vector<limb>& v) { return int_t(v.begin(), v.end()); }
    [[nodiscard]] std::size_t count() const { return resource.allocations(); }
};

// One warm-up call, then `k` calls; returns allocations per call.
template <class Env, class F>
double steady_allocs(Env& env, F&& f, const int k = calls) {
    f();
    const std::size_t before = env.count();
    for (int i = 0; i < k; ++i) {
        f();
    }
    return static_cast<double>(env.count() - before) / static_cast<double>(k);
}

// Baseline (cf1cf54) count per environment, in the order counting, pmr, wide256.
using baseline_counts = std::array<double, 3>;

} // namespace

// Prints the measurement; fails when it exceeds `want`, or skips when the case is known to exceed it at the baseline
// and the item 1 changes are still pending.
#define ALLOC_EXPECT(Env, case_name, measured, want, baseline)                                                        \
    do {                                                                                                              \
        const double alloc_measured_ = (measured);                                                                    \
        const double alloc_want_     = (want);                                                                        \
        const double alloc_base_     = (baseline)[Env::index];                                                        \
        std::printf("[alloc_count] %-8s %-34s measured=%6.3f want<=%5.2f baseline=%6.3f\n",                           \
                    Env::name,                                                                                        \
                    case_name,                                                                                        \
                    alloc_measured_,                                                                                  \
                    alloc_want_,                                                                                      \
                    alloc_base_);                                                                                     \
        if (alloc_measured_ > alloc_want_ && item1_pending && alloc_base_ > alloc_want_) {                            \
            GTEST_SKIP() << "item 1 pending: " << case_name << " measures " << alloc_measured_ << ", want <= "       \
                         << alloc_want_;                                                                              \
        }                                                                                                             \
        EXPECT_LE(alloc_measured_, alloc_want_) << case_name;                                                         \
    } while (false)

namespace {

template <class Env>
class AllocCount : public ::testing::Test {};

using envs = ::testing::Types<counting_env, pmr_env, wide256_env>;
TYPED_TEST_SUITE(AllocCount, envs);

// Baseline columns are filled from the baseline run (see the table at the top of the file).

// ----- add and subtract -----

TYPED_TEST(AllocCount, AddHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(16, 1));
    const int_t b = env.make(random_limbs(16, 2));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a + b; });
    EXPECT_TRUE(c - b == a);
    ALLOC_EXPECT(TypeParam, "c = a + b (16x16)", n, 1.0, (baseline_counts{1.0, 1.0, 1.0}));
}

TYPED_TEST(AllocCount, SubHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(16, 3));
    const int_t b = env.make(random_limbs(16, 4));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a - b; });
    EXPECT_TRUE(c + b == a);
    ALLOC_EXPECT(TypeParam, "c = a - b (16x16)", n, 1.0, (baseline_counts{1.0, 1.0, 1.0}));
}

TYPED_TEST(AllocCount, AddSubInPlaceWithCapacity) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(16, 5));
    const int_t b = env.make(random_limbs(16, 6));
    int_t       c = a;
    const double n = steady_allocs(env, [&] {
        c += b;
        c -= b;
    });
    EXPECT_TRUE(c == a);
    ALLOC_EXPECT(TypeParam, "c += b; c -= b (16x16)", n, 0.0, (baseline_counts{0.0, 0.0, 0.0}));
}

// ----- shifts -----

TYPED_TEST(AllocCount, ShiftLeftHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(16, 7));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a << 13; });
    EXPECT_TRUE((c >> 13) == a);
    ALLOC_EXPECT(TypeParam, "c = a << 13 (16 limbs)", n, 1.0, (baseline_counts{1.0, 1.0, 1.0}));
}

TYPED_TEST(AllocCount, ShiftRightHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(16, 8));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a >> 13; });
    EXPECT_TRUE((c << 13) <= a);
    ALLOC_EXPECT(TypeParam, "c = a >> 13 (16 limbs)", n, 1.0, (baseline_counts{1.0, 1.0, 1.0}));
}

TYPED_TEST(AllocCount, ShiftInPlaceWithCapacity) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(16, 9));
    int_t       c = a;
    const double n = steady_allocs(env, [&] {
        c <<= 13;
        c >>= 13;
    });
    EXPECT_TRUE(c == a);
    ALLOC_EXPECT(TypeParam, "c <<= 13; c >>= 13 (16 limbs)", n, 0.0, (baseline_counts{0.0, 0.0, 0.0}));
}

// ----- multiply -----

TYPED_TEST(AllocCount, MultiplyHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 10));
    const int_t b = env.make(random_limbs(8, 11));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a * b; });
    EXPECT_TRUE(c / b == a);
    ALLOC_EXPECT(TypeParam, "c = a * b (8x8)", n, 1.0, (baseline_counts{1.0, 1.0, 1.0}));
}

// c is restored from a inside the timed call (a copy into existing capacity), so the pair is steady.
TYPED_TEST(AllocCount, MultiplyInPlaceByBigInt) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 12));
    const int_t b = env.make(random_limbs(8, 13));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] {
        c = a;
        c *= b;
    });
    EXPECT_TRUE(c == a * b);
    ALLOC_EXPECT(TypeParam, "c = a; c *= b (8x8 -> 16)", n, 0.0, (baseline_counts{1.0, 1.0, 1.0}));
}

TYPED_TEST(AllocCount, MultiplyInPlaceBySmall) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 14));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] {
        c = a;
        c *= 7;
    });
    EXPECT_TRUE(c == a * 7);
    ALLOC_EXPECT(TypeParam, "c = a; c *= 7 (8 limbs)", n, 0.0, (baseline_counts{1.0, 1.0, 1.0}));
}

// ----- divide -----

TYPED_TEST(AllocCount, DivideHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 15));
    const int_t b = env.make(random_limbs(4, 16));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a / b; });
    EXPECT_TRUE(c * b + a % b == a);
    ALLOC_EXPECT(TypeParam, "c = a / b (8x4)", n, 1.0, (baseline_counts{2.0, 2.0, 2.0}));
}

TYPED_TEST(AllocCount, RemainderHeapResult) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 17));
    const int_t b = env.make(random_limbs(4, 18));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a % b; });
    EXPECT_TRUE(a / b * b + c == a);
    ALLOC_EXPECT(TypeParam, "c = a % b (8x4)", n, 1.0, (baseline_counts{2.0, 2.0, 2.0}));
}

TYPED_TEST(AllocCount, DivRemToZero) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 19));
    const int_t b = env.make(random_limbs(4, 20));
    bool        ok = true;
    const double n = steady_allocs(env, [&] {
        const auto qr = div_rem_to_zero(a, b);
        ok            = ok && qr.quotient * b + qr.remainder == a;
    });
    EXPECT_TRUE(ok);
    ALLOC_EXPECT(TypeParam, "div_rem_to_zero (8x4)", n, 2.0, (baseline_counts{5.0, 5.0, 4.0}));
}

// A two-limb dividend over a one-limb divisor, quotient two limbs: it fits inline only when the inline capacity is at
// least two limbs, so the case applies to wide256 only.
TYPED_TEST(AllocCount, DivideTwoByOneQuotientInline) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(std::vector<limb>{random_limbs(1, 21)[0], ~limb{0}});
    const int_t b = env.make(std::vector<limb>{limb{0x123456789ULL}});
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] { c = a / b; });
    EXPECT_TRUE(c * b + a % b == a);
    if (int_t::inplace_capacity < 2) {
        std::printf("[alloc_count] %-8s %-34s measured=%6.3f (inline capacity %zu: quotient needs the heap, n/a)\n",
                    TypeParam::name,
                    "c = a / b (2x1)",
                    n,
                    static_cast<std::size_t>(int_t::inplace_capacity));
        GTEST_SKIP() << "the 2-limb quotient only fits inline at inline capacity >= 2";
    }
    ALLOC_EXPECT(TypeParam, "c = a / b (2x1, inline q)", n, 0.0, (baseline_counts{1.0, 1.0, 0.0}));
}

// Stretch: in the schoolbook band the quotient could be built in the object's own limbs.
TYPED_TEST(AllocCount, DivideInPlace) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 22));
    const int_t b = env.make(random_limbs(4, 23));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] {
        c = a;
        c /= b;
    });
    EXPECT_TRUE(c == a / b);
    ALLOC_EXPECT(TypeParam, "c = a; c /= b (8x4) [stretch]", n, 0.0, (baseline_counts{3.0, 3.0, 2.0}));
}

TYPED_TEST(AllocCount, RemainderInPlace) {
    TypeParam    env;
    using int_t   = typename TypeParam::int_t;
    const int_t a = env.make(random_limbs(8, 24));
    const int_t b = env.make(random_limbs(4, 25));
    int_t       c = env.empty();
    const double n = steady_allocs(env, [&] {
        c = a;
        c %= b;
    });
    EXPECT_TRUE(c == a % b);
    ALLOC_EXPECT(TypeParam, "c = a; c %= b (8x4) [stretch]", n, 0.0, (baseline_counts{2.0, 2.0, 2.0}));
}

// ----- decimal from_chars -----

TYPED_TEST(AllocCount, FromCharsDecimalIntoReserved) {
    TypeParam   env;
    using int_t = typename TypeParam::int_t;
    std::string digits(2000, '0');
    for (std::size_t i = 0; i < digits.size(); ++i) {
        digits[i] = static_cast<char>('0' + (i * 7 + 3) % 10);
    }
    digits[0] = '9';
    int_t c   = env.empty();
    c.reserve(2000 * 3 + 800); // 2000 decimal digits are about 6644 bits
    std::errc    ec = std::errc{};
    const double n  = steady_allocs(
        env,
        [&] {
            const auto r = from_chars(digits.data(), digits.data() + digits.size(), c);
            ec           = r.ec;
        },
        16);
    EXPECT_EQ(ec, std::errc{});
    // The value must be 2000 decimal digits long.
    EXPECT_GT(c.size(), 6600u);
    ALLOC_EXPECT(TypeParam, "from_chars 2000 digits (reserved)", n, 2.0, (baseline_counts{2.0, 2.0, 2.0}));
}

} // namespace
