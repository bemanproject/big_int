// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

namespace {

using BEMAN_BIG_INT_NAMESPACE::basic_big_int;
using BEMAN_BIG_INT_NAMESPACE::big_int;
using namespace BEMAN_BIG_INT_NAMESPACE::literals;

// ----- An independent SipHash-2-4, taking bytes rather than limbs -----

// SipHash-2-4 over `in`, under the same key and with the same negative-sign fold
// as detail::siphash. Deliberately a second implementation, and one that is given
// bytes, so that agreement with it pins the digest to the value rather than to the
// limbs the value happens to be stored in.
[[nodiscard]] std::uint64_t reference_siphash24(const std::span<const std::uint8_t> in, const bool negative) {
    constexpr std::uint64_t k0 = 0x0706050403020100ULL;
    constexpr std::uint64_t k1 = 0x0f0e0d0c0b0a0908ULL;

    std::uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    std::uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    std::uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    std::uint64_t v3 = 0x7465646279746573ULL ^ k1;

    const auto sip_round = [&v0, &v1, &v2, &v3]() {
        v0 += v1;
        v1 = std::rotl(v1, 13);
        v1 ^= v0;
        v0 = std::rotl(v0, 32);
        v2 += v3;
        v3 = std::rotl(v3, 16);
        v3 ^= v2;
        v0 += v3;
        v3 = std::rotl(v3, 21);
        v3 ^= v0;
        v2 += v1;
        v1 = std::rotl(v1, 17);
        v1 ^= v2;
        v2 = std::rotl(v2, 32);
    };

    const auto absorb = [&v0, &v3, &sip_round](const std::uint64_t m) {
        v3 ^= m;
        sip_round();
        sip_round();
        v0 ^= m;
    };

    if (negative) {
        v3 ^= std::numeric_limits<std::uint64_t>::max();
    }

    const std::size_t n = in.size();

    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t m = 0;
        for (std::size_t j = 0; j < 8; ++j) {
            m |= std::uint64_t{in[i + j]} << (j * 8);
        }
        absorb(m);
    }

    std::uint64_t b = (static_cast<std::uint64_t>(n) & 0xFFULL) << 56U;
    for (std::size_t j = 0; i + j < n; ++j) {
        b |= std::uint64_t{in[i + j]} << (j * 8);
    }
    absorb(b);

    v2 ^= 0xFFULL;
    sip_round();
    sip_round();
    sip_round();
    sip_round();

    return v0 ^ v1 ^ v2 ^ v3;
}

// The little-endian byte string of the magnitude of `x`, carrying no most
// significant zero byte. Built from value-level operations only, so it never
// observes a limb, and zero yields the empty string.
[[nodiscard]] std::vector<std::uint8_t> magnitude_bytes(const big_int& x) {
    big_int                   magnitude = BEMAN_BIG_INT_NAMESPACE::abs(x);
    std::vector<std::uint8_t> bytes;
    while (magnitude != 0) {
        bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(magnitude & big_int{0xFF})));
        magnitude >>= 8;
    }
    return bytes;
}

// detail::siphash folds its 64-bit digest down where size_t is narrower.
[[nodiscard]] constexpr std::size_t fold_to_size_t(const std::uint64_t h) {
    if constexpr (sizeof(std::size_t) == sizeof(std::uint64_t)) {
        return static_cast<std::size_t>(h);
    } else {
        const std::uint64_t folded = h ^ (h >> 32U);
        return static_cast<std::size_t>(folded);
    }
}

// ----- The ladder of signed `_BitInt` types std::hash defers to -----

namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;

// The rungs as the implementation generates them, narrowest first.
template <std::size_t i>
using rung = detail::hash_rung_t<i>;

using rung_indices = detail::hash_rung_indices;

// A signed `_BitInt` of an arbitrary width, for the widths swept between the rungs.
// `bit_int` is only declared where the target has `_BitInt` at all, so the alias stands in
// for the widths of a target that has none.
#if BEMAN_BIG_INT_BITINT_MAXWIDTH > 0
template <std::size_t bits>
using bit_int_of = ::bit_int<static_cast<int>(bits)>;
#else
template <std::size_t bits>
using bit_int_of = detail::absent_hash_rung;
#endif

// The width of a rung, or zero where the target lacks the type or the implementation does
// not hash it. `detail::absent_hash_rung` stands in for a type the target lacks.
template <class T>
[[nodiscard]] constexpr std::size_t rung_bits() {
    if constexpr (detail::std_hashable<T>) {
        return detail::width_v<T>;
    } else {
        return 0;
    }
}

constexpr std::size_t widest_rung_bits = detail::widest_hash_rung_bits;

// Whether `T` can represent `x`, from value-level bounds rather than the round trip
// through `T` that the implementation uses.
template <class T>
[[nodiscard]] bool representable(const big_int& x) {
    if constexpr (!detail::std_hashable<T>) {
        return false;
    } else {
        const big_int bound = big_int{1} << (detail::width_v<T> - 1);
        return std::is_gteq(x <=> -bound) && std::is_lt(x <=> bound);
    }
}

template <class T>
[[nodiscard]] bool rung_digest(const big_int& x, std::size_t& digest) {
    if constexpr (detail::std_hashable<T>) {
        if (representable<T>(x)) {
            digest = std::hash<T>{}(static_cast<T>(x));
            return true;
        }
    }
    return false;
}

// The rungs are nested ranges, so the narrowest one that can represent `x` gives its digest.
template <std::size_t... i>
[[nodiscard]] bool expected_builtin_digest(const big_int& x, std::size_t& digest, std::index_sequence<i...>) {
    return (rung_digest<rung<i>>(x, digest) || ...);
}

// The widest rung decides which path a value takes. Where the implementation hashes no
// `_BitInt` at all, every value takes SipHash.
[[nodiscard]] bool takes_builtin_path(const big_int& x) {
    if constexpr (widest_rung_bits == 0) {
        return false;
    } else {
        const big_int bound = big_int{1} << (widest_rung_bits - 1);
        return std::is_gteq(x <=> -bound) && std::is_lt(x <=> bound);
    }
}

// A value takes the digest of the narrowest signed `_BitInt` that can represent it, where
// the implementation hashes that type; everything else takes SipHash-2-4 over the trimmed
// bytes of the magnitude.
[[nodiscard]] std::size_t expected_hash(const big_int& x) {
    std::size_t digest{};
    if (expected_builtin_digest(x, digest, rung_indices{})) {
        return digest;
    }
    return fold_to_size_t(reference_siphash24(magnitude_bytes(x), std::is_lt(x <=> 0)));
}

// ----- Type-level checks for the std::hash specialization -----

static_assert(std::is_default_constructible_v<std::hash<big_int>>);
static_assert(std::is_copy_constructible_v<std::hash<big_int>>);
static_assert(std::is_copy_assignable_v<std::hash<big_int>>);
static_assert(std::is_move_constructible_v<std::hash<big_int>>);
static_assert(std::is_move_assignable_v<std::hash<big_int>>);
static_assert(std::is_destructible_v<std::hash<big_int>>);
static_assert(std::is_swappable_v<std::hash<big_int>>);

static_assert(std::is_invocable_r_v<std::size_t, std::hash<big_int>, const big_int&>);
static_assert(std::is_nothrow_invocable_r_v<std::size_t, std::hash<big_int>, const big_int&>);

// The specialization must work for any inplace_bits / allocator parameterization,
// not just the convenience alias.
static_assert(std::is_default_constructible_v<std::hash<basic_big_int<32>>>);
static_assert(std::is_default_constructible_v<std::hash<basic_big_int<256>>>);
static_assert(std::is_default_constructible_v<std::hash<basic_big_int<1024>>>);
static_assert(std::is_nothrow_invocable_r_v<std::size_t, std::hash<basic_big_int<256>>, const basic_big_int<256>&>);

// ----- The digest is available during constant evaluation -----

// std::hash::operator() is not constexpr, so nothing else in the suite evaluates the
// digest at compile time. A zero magnitude is the empty input to SipHash-2-4, whose
// published digest for that input is 726fdb47dd0e0e31.
constexpr BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t zero_magnitude[]{0};
static_assert(BEMAN_BIG_INT_NAMESPACE::detail::siphash(zero_magnitude, false) ==
              fold_to_size_t(0x726fdb47dd0e0e31ULL));
static_assert(BEMAN_BIG_INT_NAMESPACE::detail::siphash(zero_magnitude, false) !=
              BEMAN_BIG_INT_NAMESPACE::detail::siphash(zero_magnitude, true));

// ----- Determinism: hashing the same value twice yields the same hash -----

TEST(Hash, DeterminismSmall) {
    const std::hash<big_int> h{};
    const big_int            x{42};
    EXPECT_EQ(h(x), h(x));
}

TEST(Hash, DeterminismZero) {
    const std::hash<big_int> h{};
    const big_int            x{};
    EXPECT_EQ(h(x), h(x));
}

TEST(Hash, DeterminismMultiLimb) {
    const std::hash<big_int> h{};
    const big_int            x = 12345678901234567890123456789012345678901234567890_n;
    EXPECT_EQ(h(x), h(x));
}

TEST(Hash, DeterminismAcrossInstances) {
    // A freshly-constructed std::hash instance must agree with another instance.
    const std::hash<big_int> h1{};
    const std::hash<big_int> h2{};
    const big_int            x = 0xCAFE'BABE'DEAD'BEEF_n;
    EXPECT_EQ(h1(x), h2(x));
}

// ----- Equal values produce equal hashes (the only contract requirement) -----

TEST(Hash, EqualValuesHashEqually) {
    const std::hash<big_int> h{};
    const big_int            a{12345};
    const big_int            b{12345};
    ASSERT_EQ(a, b);
    EXPECT_EQ(h(a), h(b));
}

TEST(Hash, EqualValuesFromCopy) {
    const std::hash<big_int> h{};
    const big_int            a = 1'000'000'000'000'000'000_n;
    const big_int            b{a};
    ASSERT_EQ(a, b);
    EXPECT_EQ(h(a), h(b));
}

TEST(Hash, EqualValuesAfterMove) {
    const std::hash<big_int> h{};
    big_int                  a        = 0xDEAD'BEEF'CAFE'BABE'1234'5678'90AB'CDEF_n;
    const std::size_t        expected = h(a);
    const big_int            b{std::move(a)};
    EXPECT_EQ(h(b), expected);
}

TEST(Hash, EqualValuesAfterArithmetic) {
    const std::hash<big_int> h{};
    const big_int            a{1000};
    const big_int            b = big_int{700} + big_int{300};
    const big_int            c = big_int{2000} - big_int{1000};
    const big_int            d = big_int{500} * big_int{2};
    ASSERT_EQ(a, b);
    ASSERT_EQ(a, c);
    ASSERT_EQ(a, d);
    EXPECT_EQ(h(a), h(b));
    EXPECT_EQ(h(a), h(c));
    EXPECT_EQ(h(a), h(d));
}

TEST(Hash, EqualMultiLimbAfterArithmetic) {
    const std::hash<big_int> h{};
    const big_int            big        = 1_n << 200;
    const big_int            also_big_a = big + big_int{0};
    const big_int            also_big_b = (big_int{1} << 199) * big_int{2};
    ASSERT_EQ(big, also_big_a);
    ASSERT_EQ(big, also_big_b);
    EXPECT_EQ(h(big), h(also_big_a));
    EXPECT_EQ(h(big), h(also_big_b));
}

// ----- Zero is well-defined -----

TEST(Hash, ZeroIsConsistent) {
    const std::hash<big_int> h{};
    const big_int            z_default{};
    const big_int            z_from_int{0};
    const big_int            z_from_uint{0U};
    const big_int            z_from_subtraction = big_int{7} - big_int{7};

    ASSERT_TRUE(z_default == 0);
    ASSERT_EQ(z_default, z_from_int);
    ASSERT_EQ(z_default, z_from_uint);
    ASSERT_EQ(z_default, z_from_subtraction);
    EXPECT_EQ(h(z_default), h(z_from_int));
    EXPECT_EQ(h(z_default), h(z_from_uint));
    EXPECT_EQ(h(z_default), h(z_from_subtraction));
}

// ----- Different values produce different hashes (statistical / collision tests) -----

TEST(Hash, DistinctSmallValues) {
    const std::hash<big_int>        h{};
    std::unordered_set<std::size_t> hashes;
    for (int i = 0; i < 8; ++i) {
        hashes.insert(h(big_int{i}));
    }
    // For a good keyed hash and only 8 distinct inputs, the chance of any
    // collision is astronomically small. Require all eight to be distinct.
    EXPECT_EQ(hashes.size(), 8U);
}

TEST(Hash, DistinctValuesFromOneToTwoFiftySix) {
    const std::hash<big_int>        h{};
    std::unordered_set<std::size_t> hashes;
    constexpr int                   N = 256;
    for (int i = 0; i < N; ++i) {
        hashes.insert(h(big_int{i}));
    }
    // We allow a small slack so this is not flaky on 32-bit `size_t` platforms,
    // where the hash is folded down to 32 bits and the birthday bound predicts
    // a non-zero (but still small) collision probability over 256 inputs.
    EXPECT_GE(hashes.size(), static_cast<std::size_t>(N - 4));
}

TEST(Hash, DistinctMultiLimbValues) {
    const std::hash<big_int> h{};
    const big_int            base = big_int{1} << 200;
    const big_int            a    = base + big_int{1};
    const big_int            b    = base + big_int{2};
    const big_int            c    = (big_int{1} << 201) + big_int{1};
    EXPECT_NE(h(a), h(b));
    EXPECT_NE(h(a), h(c));
    EXPECT_NE(h(b), h(c));
}

TEST(Hash, DistinctPowersOfTwo) {
    const std::hash<big_int> h{};
    const big_int            a = big_int{1} << 100;
    const big_int            b = big_int{1} << 200;
    const big_int            c = big_int{1} << 300;
    EXPECT_NE(h(a), h(b));
    EXPECT_NE(h(b), h(c));
    EXPECT_NE(h(a), h(c));
}

TEST(Hash, SingleVsMultiLimbDistinct) {
    // Make sure a value that fits in one limb and a multi-limb value differ.
    const std::hash<big_int> h{};
    const big_int            small{1};
    const big_int            wide = big_int{1} << 128;
    EXPECT_NE(h(small), h(wide));
}

// ----- Stability across different inplace_bits parameterizations -----

TEST(Hash, CrossInplaceBitsForSmallValue) {
    // The hash is computed over the significant bytes of the magnitude, which do
    // not depend on `inplace_bits`.
    // The hash must therefore agree across all parameterizations.
    const basic_big_int<32>   a{42};
    const basic_big_int<64>   b{42};
    const basic_big_int<256>  c{42};
    const basic_big_int<1024> d{42};

    const auto ha = std::hash<basic_big_int<32>>{}(a);
    const auto hb = std::hash<basic_big_int<64>>{}(b);
    const auto hc = std::hash<basic_big_int<256>>{}(c);
    const auto hd = std::hash<basic_big_int<1024>>{}(d);

    EXPECT_EQ(ha, hb);
    EXPECT_EQ(ha, hc);
    EXPECT_EQ(ha, hd);
}

TEST(Hash, CrossInplaceBitsForMultiLimb) {
    const basic_big_int<64>   a = 1'000'000'000'000'000'000'000'000'000_n;
    const basic_big_int<2048> b{a};

    EXPECT_EQ(std::hash<basic_big_int<64>>{}(a), std::hash<basic_big_int<2048>>{}(b));
}

TEST(Hash, CrossInplaceBitsForZero) {
    const basic_big_int<32>   a{};
    const basic_big_int<2048> b{};

    EXPECT_EQ(std::hash<basic_big_int<32>>{}(a), std::hash<basic_big_int<2048>>{}(b));
}

// ----- The digest is a function of the value, not of the limb decomposition -----

TEST(Hash, MatchesSipHash24OverTrimmedValueBytes) {
    // [big.int.hash] requires the hash to depend only on the integer value, which
    // includes not depending on the limb width. A single build cannot instantiate two
    // limb widths, but it can check the properties that make them agree: a value in the
    // range of `std::int64_t` is hashed as that `std::int64_t`, and everything else by
    // SipHash-2-4 over the trimmed little-endian bytes of the magnitude. The reference
    // is given the value and those bytes, so it cannot observe the limb width either.
    const std::hash<big_int> h{};

    // Every magnitude width up to 520 bits, so every limb and block boundary of both
    // limb widths is crossed, along with every rung of the builtin ladder and the widths
    // past its end, together with each width's neighbours and its negation.
    for (unsigned width = 0; width <= 520; ++width) {
        const big_int power = width == 0 ? big_int{0} : (big_int{1} << (width - 1));
        for (const big_int& x : {power, power + big_int{1}, power - big_int{1}, -power}) {
            EXPECT_EQ(h(x), expected_hash(x)) << "width " << width;
        }
    }

    for (int i = -300; i <= 300; ++i) {
        const big_int x{i};
        EXPECT_EQ(h(x), expected_hash(x)) << "value " << i;
    }

    // All-ones magnitudes fill every byte; the sparse ones leave every interior byte,
    // and every interior limb, zero. Both sides of the trimming rule are exercised.
    for (unsigned bytes = 1; bytes <= 40; ++bytes) {
        const big_int ones   = (big_int{1} << (8 * bytes)) - big_int{1};
        const big_int sparse = (big_int{1} << (8 * bytes)) + big_int{1};
        EXPECT_EQ(h(ones), expected_hash(ones)) << "bytes " << bytes;
        EXPECT_EQ(h(sparse), expected_hash(sparse)) << "bytes " << bytes;
    }
}

TEST(Hash, MatchesPublishedSipHash24Vectors) {
    // The integer whose trimmed little-endian byte string is {0x00, 0x01, ..., k-1}
    // is exactly the k-byte input of the published SipHash-2-4 test vectors, under the
    // key detail::siphash uses. These digests are fixed by a reference outside this
    // library, and so cannot follow the limb width. Lengths 33 through 40 are the
    // shortest that exceed the widest rung of the builtin ladder, 256 bits, so they take
    // the SipHash path on every target, and between them they cover every block and tail
    // length.
    const std::hash<big_int> h{};

    static constexpr std::uint64_t digests[] = {
        0xa7f32346f95978e3ULL, // {0x00, 0x01, ..., 0x20}
        0x12e0b01abb051238ULL, // {0x00, 0x01, ..., 0x21}
        0x15e034d40fa197aeULL, // and so on up to
        0x314dffbe0815a3b4ULL,
        0x027990f029623981ULL,
        0xcadcd4e59ef40c4dULL,
        0x9abfd8766a33735cULL,
        0x0e3ea96b5304a7d0ULL, // {0x00, 0x01, ..., 0x27}
    };

    for (std::size_t k = 33; k <= 40; ++k) {
        big_int x{0};
        for (std::size_t i = k; i-- > 0;) {
            x = (x << 8) + big_int{i};
        }
        ASSERT_FALSE(takes_builtin_path(x)) << "length " << k;
        EXPECT_EQ(h(x), fold_to_size_t(digests[k - 33])) << "length " << k;
    }

    // The digest of the magnitude alone is still the published vector for the shorter
    // inputs, even where std::hash now answers from the builtin instead. Zero takes the
    // length-0 vector; there is no length-1 case, because {0x00} is not trimmed.
    static constexpr std::uint64_t short_digests[] = {
        0x726fdb47dd0e0e31ULL, // {}
        0x0d6c8009d9a94f5aULL, // {0x00, 0x01}
        0x85676696d7fb7e2dULL, // {0x00, 0x01, 0x02}
        0xcf2794e0277187b7ULL, // and so on up to
        0x18765564cd99a68dULL,
        0xcbc9466e58fee3ceULL,
        0xab0200f58b01d137ULL,
        0x93f5f5799a932462ULL, // {0x00, 0x01, ..., 0x07}
    };

    EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::detail::siphash(big_int{0}.representation(), false),
              fold_to_size_t(short_digests[0]));

    for (std::size_t k = 2; k <= 8; ++k) {
        big_int x{0};
        for (std::size_t i = k; i-- > 0;) {
            x = (x << 8) + big_int{i};
        }
        EXPECT_EQ(BEMAN_BIG_INT_NAMESPACE::detail::siphash(x.representation(), false),
                  fold_to_size_t(short_digests[k - 1]))
            << "length " << k;
    }
}

// ----- Values in the range of std::int64_t hash as the builtin does -----

// The values used to sweep the narrowest rung. A typed array rather than a braced list:
// `std::int64_t` is `long` on some targets and `long long` on others, so a list of
// literals has no one deduced type.
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();

static constexpr std::int64_t sweep_values[]{int64_min,
                                             int64_min + 1,
                                             int64_min / 2,
                                             -4294967296,
                                             -1,
                                             0,
                                             1,
                                             4294967295,
                                             4294967296,
                                             int64_max / 2,
                                             int64_max - 1,
                                             int64_max};

// The bodies below are templates so that `if constexpr` really does discard the rung half
// where the implementation hashes no `_BitInt`. In a non-template function both halves are
// still checked, and naming `std::hash<narrowest>` there is an error rather than dead code.
template <class narrowest>
void check_narrowest_rung() {
    if constexpr (!detail::std_hashable<narrowest>) {
        GTEST_SKIP() << "this implementation hashes no _BitInt";
    } else {
        // A `big_int` and a `_BitInt` of the narrowest rung's width, holding the same value,
        // must agree, so that a program can hash the two interchangeably.
        const std::hash<big_int>   h{};
        const std::hash<narrowest> builtin{};

        // A value the rung cannot represent is hashed further up the ladder, so it says
        // nothing about this rung.
        for (const std::int64_t v : sweep_values) {
            if (!representable<narrowest>(big_int{v})) {
                continue;
            }
            EXPECT_EQ(h(big_int{v}), builtin(static_cast<narrowest>(v))) << "value " << v;
        }

        for (std::int64_t v = -300; v <= 300; ++v) {
            EXPECT_EQ(h(big_int{v}), builtin(static_cast<narrowest>(v))) << "value " << v;
        }

        // Every power of two in range, and its neighbours, so both sides of each limb and
        // byte boundary of the rung are covered.
        const int top = static_cast<int>(std::min<std::size_t>(detail::width_v<narrowest>, 64U)) - 1;
        for (int bit = 0; bit < top; ++bit) {
            const std::int64_t power = std::int64_t{1} << bit;
            for (const std::int64_t v : {power, power - 1, -power, -power + 1}) {
                EXPECT_EQ(h(big_int{v}), builtin(static_cast<narrowest>(v))) << "value " << v;
            }
        }
    }
}

TEST(Hash, MatchesBitPreciseHashInTheNarrowestRung) { check_narrowest_rung<rung<0>>(); }

// Where the narrowest rung is as wide as `std::int64_t` the two hold the same values, so
// wherever the implementation gives them the same digest, which libc++ does since it hashes
// a scalar by its object representation, a `big_int` agrees with `std::int64_t` as well.
// Nothing to check where the implementation makes them differ, or hashes no `_BitInt`.
template <class narrowest>
void check_int64_agreement() {
    if constexpr (!detail::std_hashable<narrowest>) {
        GTEST_SKIP() << "this implementation hashes no _BitInt";
    } else if constexpr (detail::width_v<narrowest> < detail::width_v<std::int64_t>) {
        GTEST_SKIP() << "the narrowest rung is narrower than std::int64_t";
    } else {
        if (std::hash<narrowest>{}(narrowest{42}) != std::hash<std::int64_t>{}(42)) {
            GTEST_SKIP() << "this implementation hashes the narrowest rung and std::int64_t differently";
        }

        const std::hash<big_int> h{};
        for (const std::int64_t v : sweep_values) {
            EXPECT_EQ(h(big_int{v}), std::hash<std::int64_t>{}(v)) << "value " << v;
        }
    }
}

TEST(Hash, AgreesWithInt64WhereTheTwoBuiltinDigestsAgree) { check_int64_agreement<rung<0>>(); }

// The rung is chosen on the value, not on where the value is stored, so a value that is in
// place for one parameterization and heap-allocated for another still lands on the same
// digest.
template <class narrowest>
void check_rung_across_inplace_bits() {
    if constexpr (!detail::std_hashable<narrowest>) {
        GTEST_SKIP() << "this implementation hashes no _BitInt";
    } else if constexpr (detail::width_v<narrowest> < detail::width_v<std::int64_t>) {
        GTEST_SKIP() << "the narrowest rung is narrower than std::int64_t";
    } else {
        static constexpr std::int64_t values[]{int64_min, -1, 0, 42, int64_max};

        for (const std::int64_t v : values) {
            const std::size_t expected = std::hash<narrowest>{}(static_cast<narrowest>(v));
            EXPECT_EQ(std::hash<basic_big_int<32>>{}(basic_big_int<32>{v}), expected) << "value " << v;
            EXPECT_EQ(std::hash<basic_big_int<64>>{}(basic_big_int<64>{v}), expected) << "value " << v;
            EXPECT_EQ(std::hash<basic_big_int<256>>{}(basic_big_int<256>{v}), expected) << "value " << v;
            EXPECT_EQ(std::hash<basic_big_int<1024>>{}(basic_big_int<1024>{v}), expected) << "value " << v;
        }
    }
}

TEST(Hash, MatchesBitPreciseHashAcrossInplaceBits) { check_rung_across_inplace_bits<rung<0>>(); }

TEST(Hash, BitPrecisePathEndsAtTheWidestRung) {
    // The value just past each end of the widest rung is hashed the other way, and must
    // not collide with the boundary value it neighbours.
    if constexpr (widest_rung_bits == 0) {
        GTEST_SKIP() << "this implementation hashes no _BitInt, so every value takes SipHash";
    } else {
        const std::hash<big_int> h{};

        const big_int bound    = big_int{1} << (widest_rung_bits - 1);
        const big_int max      = bound - big_int{1};
        const big_int min      = -bound;
        const big_int past_max = max + big_int{1};
        const big_int past_min = min - big_int{1};

        ASSERT_TRUE(takes_builtin_path(max));
        ASSERT_TRUE(takes_builtin_path(min));
        ASSERT_FALSE(takes_builtin_path(past_max));
        ASSERT_FALSE(takes_builtin_path(past_min));

        EXPECT_EQ(h(max), expected_hash(max));
        EXPECT_EQ(h(min), expected_hash(min));
        EXPECT_EQ(h(past_max), expected_hash(past_max));
        EXPECT_EQ(h(past_min), expected_hash(past_min));

        EXPECT_NE(h(max), h(past_max));
        EXPECT_NE(h(min), h(past_min));
        EXPECT_NE(h(past_max), h(past_min));
    }
}

// Checks the values that only rung `T` can represent, `T` sitting above a rung of
// `narrower_bits` bits, or above nothing where that is zero. Does nothing where the
// implementation does not hash `T`.
template <class T>
[[nodiscard]] std::size_t check_rung(const std::size_t narrower_bits) {
    std::size_t checked = 0;
    if constexpr (detail::std_hashable<T>) {
        constexpr std::size_t bits = detail::width_v<T>;
        if (bits <= narrower_bits) {
            return checked;
        }

        const std::hash<big_int> h{};

        // The narrower rung stops at a magnitude of 2^(narrower_bits - 1), so these are
        // the first values past it, and the last this rung reaches. With no narrower rung
        // the sweep starts at one.
        const big_int first_beyond = narrower_bits == 0 ? big_int{1} : big_int{1} << (narrower_bits - 1);
        const big_int largest      = (big_int{1} << (bits - 1)) - big_int{1};

        for (const big_int& x :
             {first_beyond, first_beyond + big_int{1}, largest, -largest - big_int{1}, -first_beyond - big_int{1}}) {
            EXPECT_TRUE(takes_builtin_path(x)) << "bits " << bits;
            EXPECT_EQ(h(x), std::hash<T>{}(static_cast<T>(x))) << "bits " << bits;
            ++checked;
        }
    }
    return checked;
}

// Walks the ladder narrowest first, carrying the width of the widest rung below the one
// being checked, and reports how many rungs contributed a check.
template <std::size_t... i>
[[nodiscard]] std::size_t check_every_rung(std::index_sequence<i...>) {
    std::size_t narrower = 0;
    std::size_t checked  = 0;
    ((checked += check_rung<rung<i>>(narrower) != 0 ? 1U : 0U, narrower = std::max(narrower, rung_bits<rung<i>>())),
     ...);
    return checked;
}

// An object size the implementation hashes is one every narrower size is hashed too, so the
// widest rung tells how many rungs there are to check.
constexpr std::size_t hashable_rungs = widest_rung_bits / detail::hash_rung_step;

TEST(Hash, MatchesBitPreciseHashOnEveryRung) {
    if constexpr (widest_rung_bits == 0) {
        GTEST_SKIP() << "this implementation hashes no _BitInt, so every value takes SipHash";
    } else {
        // Every rung must have been checked, not merely some of them.
        EXPECT_EQ(check_every_rung(rung_indices{}), hashable_rungs) << "widest rung " << widest_rung_bits << " bits";
    }
}

// ----- Parity with `_BitInt(N)` at every width, not only at the rungs -----

// A signed `_BitInt` is at least two bits wide, so a sweep of widths starts there.
constexpr std::size_t narrowest_bit_int_bits = 2;

// The rung a `_BitInt(bits)` shares an object size with: the ladder steps one object size at
// a time, so it is `bits` rounded up to a whole number of steps.
[[nodiscard]] constexpr std::size_t rung_covering(const std::size_t bits) {
    return detail::div_to_pos_inf(bits, detail::hash_rung_step) * detail::hash_rung_step;
}

// The width of the rung below the one covering `bits`, or zero where there is none.
[[nodiscard]] constexpr std::size_t rung_below(const std::size_t bits) {
    return rung_covering(bits) - detail::hash_rung_step;
}

// `std::hash<big_int>` gives a value the digest of the narrowest rung that can represent it,
// and an implementation that hashes a scalar by its object representation gives every
// `_BitInt` of one object size a single digest. A `_BitInt(N)` key therefore hashes as a
// `big_int` key of the same value exactly where that value needs `N`'s object size: one small
// enough for a narrower rung takes the narrower rung's digest, which past the first step is a
// digest over fewer bytes. Below the first step there is no narrower rung, so the whole range
// of `_BitInt(N)` qualifies. Where the standard library hashes no `_BitInt` the sweep below
// has no width to check, so this helper goes unused there.
[[nodiscard, maybe_unused]] bool needs_own_object_size(const big_int& x, const std::size_t bits) {
    const std::size_t narrower = rung_below(bits);
    if (narrower == 0) {
        return true;
    }
    const big_int bound = big_int{1} << (narrower - 1);
    return std::is_lt(x <=> -bound) || std::is_gt(x <=> bound - big_int{1});
}

// Checks a `big_int` against a `_BitInt(bits)` holding the same value, over both ends of that
// type's range, both ends of the band of values it is the narrowest of its object size for,
// and a spread in between. Reports how many values were checked, which is none where the
// implementation does not hash the type. `T` is a template parameter rather than a local
// alias so that `std::hash<T>` stays dependent: on a target without `_BitInt` every width
// names the same placeholder, and a compiler is free to reject a non-dependent use of it
// even in the branch discarded below.
template <std::size_t bits, class T = bit_int_of<bits>>
[[nodiscard]] std::size_t check_width() {
    if constexpr (!detail::std_hashable<T>) {
        return 0;
    } else {
        const std::hash<big_int> h{};

        const big_int max    = (big_int{1} << (bits - 1)) - big_int{1};
        const big_int min    = -max - big_int{1};
        const big_int first  = rung_below(bits) == 0 ? big_int{0} : big_int{1} << (rung_below(bits) - 1);
        const big_int middle = first + ((max - first) >> 1);
        const big_int third  = max / big_int{3};

        std::size_t checked = 0;
        for (const big_int& x : {min,
                                 min + big_int{1},
                                 max,
                                 max - big_int{1},
                                 first,
                                 first + big_int{1},
                                 -first - big_int{1},
                                 middle,
                                 -middle - big_int{1},
                                 third,
                                 -third - big_int{1}}) {
            // A candidate outside this width's range, or one a narrower rung already covers,
            // says nothing about this width.
            if (std::is_lt(x <=> min) || std::is_gt(x <=> max) || !needs_own_object_size(x, bits)) {
                continue;
            }
            EXPECT_EQ(h(x), std::hash<T>{}(static_cast<T>(x))) << "width " << bits << " value " << x;
            ++checked;
        }
        return checked;
    }
}

// Every width from the narrowest signed `_BitInt` up to the widest rung, so that parity is
// pinned across the board rather than only at the widths the ladder names.
constexpr std::size_t swept_widths =
    widest_rung_bits >= narrowest_bit_int_bits ? widest_rung_bits - narrowest_bit_int_bits + 1 : 0;

// Reports how many of the swept widths contributed a check.
template <std::size_t... i>
[[nodiscard]] std::size_t check_every_width(std::index_sequence<i...>) {
    std::size_t checked = 0;
    ((checked += check_width<i + narrowest_bit_int_bits>() != 0 ? 1U : 0U), ...);
    return checked;
}

TEST(Hash, MatchesBitPreciseHashAtEveryWidth) {
    if constexpr (swept_widths == 0) {
        GTEST_SKIP() << "this implementation hashes no _BitInt, so every value takes SipHash";
    } else {
        // Every width up to the widest rung must have been checked, not merely the rungs.
        EXPECT_EQ(check_every_width(std::make_index_sequence<swept_widths>{}), swept_widths)
            << "widest rung " << widest_rung_bits << " bits";
    }
}

// ----- Usability in standard unordered associative containers -----

TEST(Hash, UnorderedSetSmallKeys) {
    std::unordered_set<big_int> s;
    s.insert(big_int{1});
    s.insert(big_int{2});
    s.insert(big_int{3});
    s.insert(big_int{2}); // duplicate
    s.insert(big_int{1}); // duplicate
    EXPECT_EQ(s.size(), 3U);
    EXPECT_TRUE(s.contains(big_int{1}));
    EXPECT_TRUE(s.contains(big_int{2}));
    EXPECT_TRUE(s.contains(big_int{3}));
    EXPECT_FALSE(s.contains(big_int{0}));
    EXPECT_FALSE(s.contains(big_int{4}));
}

TEST(Hash, UnorderedSetMultiLimbKeys) {
    std::unordered_set<big_int> s;
    const big_int               k1 = 100'000'000'000'000'000'000'000_n;
    const big_int               k2 = 200'000'000'000'000'000'000'000_n;
    const big_int               k3 = (big_int{1} << 256) + big_int{1};
    s.insert(k1);
    s.insert(k2);
    s.insert(k3);
    s.insert(k1); // duplicate
    EXPECT_EQ(s.size(), 3U);
    EXPECT_TRUE(s.contains(k1));
    EXPECT_TRUE(s.contains(k2));
    EXPECT_TRUE(s.contains(k3));
    EXPECT_FALSE(s.contains(big_int{0}));
}

TEST(Hash, UnorderedSetWithBothSignsAreDistinctElements) {
    // Both the hash and operator== must distinguish +x from -x.
    std::unordered_set<big_int> s;
    s.insert(big_int{5});
    s.insert(big_int{-5});
    s.insert(big_int{5}); // duplicate
    EXPECT_EQ(s.size(), 2U);
    EXPECT_TRUE(s.contains(big_int{5}));
    EXPECT_TRUE(s.contains(big_int{-5}));
}

TEST(Hash, SignDistinguishesSmallValues) {
    const std::hash<big_int> h{};
    for (int i = 1; i <= 32; ++i) {
        const big_int pos{i};
        const big_int neg{-i};
        EXPECT_NE(h(pos), h(neg)) << "collision at i=" << i;
    }
}

TEST(Hash, SignDistinguishesMultiLimbValues) {
    const std::hash<big_int> h{};
    const big_int            magnitude = (big_int{1} << 200) + big_int{12345};
    const big_int            negated   = -magnitude;
    ASSERT_NE(magnitude, negated);
    EXPECT_NE(h(magnitude), h(negated));
}

TEST(Hash, SignDistinguishesAcrossInplaceBits) {
    // The sign-aware hash must agree across inplace_bits parameterizations,
    // both for positive and for negative values.
    const basic_big_int<64>  pos_64{42};
    const basic_big_int<256> pos_256{42};
    const basic_big_int<64>  neg_64  = -pos_64;
    const basic_big_int<256> neg_256 = -pos_256;

    EXPECT_EQ(std::hash<basic_big_int<64>>{}(pos_64), std::hash<basic_big_int<256>>{}(pos_256));
    EXPECT_EQ(std::hash<basic_big_int<64>>{}(neg_64), std::hash<basic_big_int<256>>{}(neg_256));
    EXPECT_NE(std::hash<basic_big_int<64>>{}(pos_64), std::hash<basic_big_int<64>>{}(neg_64));
}

TEST(Hash, NegatingTwiceRestoresHash) {
    const std::hash<big_int> h{};
    const big_int            x        = (big_int{1} << 100) + big_int{7};
    const std::size_t        original = h(x);
    const big_int            negated  = -x;
    EXPECT_NE(original, h(negated));
    EXPECT_EQ(original, h(-negated));
}

TEST(Hash, UnorderedMapBigIntKey) {
    std::unordered_map<big_int, int> m;
    const big_int                    k1 = 10'000'000'000'000'000'000'000_n;
    const big_int                    k2 = 20'000'000'000'000'000'000'000_n;
    m[k1]                               = 1;
    m[k2]                               = 2;
    m[k1]                               = 11; // overwrite
    EXPECT_EQ(m.size(), 2U);
    EXPECT_EQ(m.at(k1), 11);
    EXPECT_EQ(m.at(k2), 2);
    EXPECT_EQ(m.count(big_int{0}), 0U);
}

TEST(Hash, UnorderedMapManyEntries) {
    std::unordered_map<big_int, int> m;
    constexpr int                    N = 100;
    for (int i = 0; i < N; ++i) {
        m.emplace(big_int{i} * big_int{1'000'000'000'000'000'000} + big_int{i}, i);
    }
    EXPECT_EQ(m.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        const big_int k = big_int{i} * big_int{1'000'000'000'000'000'000} + big_int{i};
        EXPECT_EQ(m.at(k), i);
    }
}

// ----- Boundary values -----

TEST(Hash, BoundaryUint64Values) {
    const std::hash<big_int> h{};
    const big_int            u64_max{std::numeric_limits<std::uint64_t>::max()};
    const big_int            u64_max_minus_one{std::numeric_limits<std::uint64_t>::max() - 1U};
    const big_int            u64_max_plus_one = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};

    EXPECT_NE(h(u64_max), h(u64_max_minus_one));
    EXPECT_NE(h(u64_max), h(u64_max_plus_one));
    EXPECT_NE(h(u64_max_minus_one), h(u64_max_plus_one));
}

TEST(Hash, BoundarySignedInt64Min) {
    const std::hash<big_int> h{};
    const big_int            i64_min{std::numeric_limits<std::int64_t>::min()};
    const big_int            i64_min_copy{i64_min};
    EXPECT_EQ(h(i64_min), h(i64_min_copy));
}

TEST(Hash, DecrementAcrossLimbBoundaryPreservesHash) {
    const std::hash<big_int> hasher;
    for (const unsigned shift : {64U, 128U, 192U}) {
        big_int x = big_int{1} << shift;
        --x;

        const big_int expected = (big_int{1} << shift) - big_int{1};
        ASSERT_EQ(x, expected) << "shift " << shift;

        EXPECT_TRUE(is_normalized(x)) << "shift " << shift;
        EXPECT_EQ(hasher(x), hasher(expected)) << "shift " << shift;
    }
}

} // namespace
