// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Tests for the front-end span primitives add_n_tail, sub_n_tail, lshift_copy and rshift_copy.

#include <array>
#include <cstddef>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

namespace bb = BEMAN_BIG_INT_NAMESPACE;

namespace {

using limb                    = bb::uint_multiprecision_t;
constexpr unsigned limb_width = static_cast<unsigned>(std::numeric_limits<limb>::digits);
constexpr limb     max_limb   = std::numeric_limits<limb>::max();

using bits_t = std::vector<bool>; // little-endian bit string, the reference representation

bits_t to_bits(const std::vector<limb>& v) {
    bits_t out(v.size() * limb_width);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = ((v[i / limb_width] >> (i % limb_width)) & 1U) != 0;
    }
    return out;
}

std::vector<limb> from_bits(const bits_t& b) {
    std::vector<limb> out(b.size() / limb_width, limb{0});
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (b[i]) {
            out[i / limb_width] |= static_cast<limb>(limb{1} << (i % limb_width));
        }
    }
    return out;
}

// Reference a +/- b over a.size() limbs, returning the carry/borrow out.
bool ref_addsub(std::vector<limb>& out, const std::vector<limb>& a, const std::vector<limb>& b, const bool sub) {
    const bits_t abits = to_bits(a);
    bits_t       bbits(abits.size(), false);
    const bits_t braw = to_bits(b);
    for (std::size_t i = 0; i < braw.size(); ++i) {
        bbits[i] = braw[i];
    }
    bits_t res(abits.size());
    bool   c = false;
    for (std::size_t i = 0; i < abits.size(); ++i) {
        if (!sub) {
            res[i] = (abits[i] != bbits[i]) != c;
            c      = (abits[i] && bbits[i]) || (c && (abits[i] != bbits[i]));
        } else {
            res[i] = (abits[i] != bbits[i]) != c;
            c      = (!abits[i] && bbits[i]) || (c && !(abits[i] != bbits[i]));
        }
    }
    out = from_bits(res);
    return c;
}

limb pick(std::mt19937_64& rng) {
    switch (rng() % 5U) {
    case 0:
        return 0;
    case 1:
        return max_limb;
    case 2:
        return 1;
    default:
        return static_cast<limb>(rng());
    }
}

std::vector<limb> random_limbs(std::mt19937_64& rng, const std::size_t n) {
    std::vector<limb> v(n);
    for (auto& x : v) {
        x = pick(rng);
    }
    return v;
}

constexpr limb poison = static_cast<limb>(0xA5A5A5A5A5A5A5A5ULL);

template <bool Sub>
void check_addsub(const bool in_place) {
    std::mt19937_64 rng{Sub ? 11U : 7U};
    for (std::size_t m = 0; m <= 9; ++m) {
        for (std::size_t n = 0; n <= m; ++n) {
            for (int trial = 0; trial < 300; ++trial) {
                std::vector<limb> a = random_limbs(rng, m);
                std::vector<limb> b = random_limbs(rng, n);
                if (trial % 7 == 0) { // long carry / borrow ripples
                    for (auto& x : a) {
                        x = Sub ? limb{0} : max_limb;
                    }
                    if (n != 0) {
                        b[0] = 1;
                    }
                }
                std::vector<limb> expected;
                const bool        expected_flag = ref_addsub(expected, a, b, Sub);

                std::vector<limb> dst_storage = in_place ? a : std::vector<limb>(m, poison);
                const limb*       a_ptr       = in_place ? dst_storage.data() : a.data();
                const std::span<const limb> a_span{a_ptr, m};
                const std::span<limb>       dst_span{dst_storage.data(), m};
                const bool flag = Sub ? bb::detail::sub_n_tail(dst_span, a_span, std::span<const limb>{b})
                                      : bb::detail::add_n_tail(dst_span, a_span, std::span<const limb>{b});
                ASSERT_EQ(flag, expected_flag) << "m=" << m << " n=" << n;
                ASSERT_EQ(dst_storage, expected) << "m=" << m << " n=" << n;
            }
        }
    }
}

} // namespace

TEST(SpanPrimitives, AddNTailSeparateDestination) { check_addsub<false>(false); }
TEST(SpanPrimitives, AddNTailInPlace) { check_addsub<false>(true); }
TEST(SpanPrimitives, SubNTailSeparateDestination) { check_addsub<true>(false); }
TEST(SpanPrimitives, SubNTailInPlace) { check_addsub<true>(true); }

namespace {

std::vector<limb> ref_shift(const std::vector<limb>& src, const unsigned bits, const bool left, limb& out_bits) {
    const bits_t in = to_bits(src);
    bits_t       res(in.size(), false);
    out_bits = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (left) {
            if (i + bits < in.size()) {
                res[i + bits] = in[i];
            } else if (in[i]) {
                out_bits |= static_cast<limb>(limb{1} << (i + bits - in.size()));
            }
        } else {
            if (i >= bits) {
                res[i - bits] = in[i];
            } else if (in[i]) {
                out_bits |= static_cast<limb>(limb{1} << (limb_width - bits + i));
            }
        }
    }
    return from_bits(res);
}

void check_shift(const bool left, const bool aliased) {
    std::mt19937_64 rng{left ? 3U : 5U};
    for (std::size_t n = 0; n <= 9; ++n) {
        for (unsigned bits = 1; bits < limb_width; bits += (bits < 5 || bits + 6 > limb_width ? 1 : 7)) {
            for (int trial = 0; trial < 20; ++trial) {
                const std::vector<limb> src = random_limbs(rng, n);
                limb                    expected_out = 0;
                const auto              expected     = ref_shift(src, bits, left, expected_out);

                // Non-aliased: destination below (rshift) or above (lshift) in a bigger buffer.
                std::vector<limb> buf(n + 3, poison);
                limb*             dst = nullptr;
                const limb*       s   = nullptr;
                std::vector<limb> src_buf = src;
                if (aliased) {
                    buf.assign(src.begin(), src.end());
                    buf.resize(n + 3, poison);
                    dst = buf.data();
                    s   = buf.data();
                } else {
                    dst = buf.data() + 1;
                    s   = src_buf.data();
                }
                const limb out = left ? bb::detail::lshift_copy(dst, s, n, bits) : bb::detail::rshift_copy(dst, s, n, bits);
                ASSERT_EQ(out, expected_out) << "n=" << n << " bits=" << bits;
                ASSERT_EQ(std::vector<limb>(dst, dst + n), expected) << "n=" << n << " bits=" << bits;
                if (!aliased) {
                    ASSERT_EQ(buf[0], poison);
                    ASSERT_EQ(buf[n + 1], poison);
                    ASSERT_EQ(buf[n + 2], poison);
                }
            }
        }
    }
}

// Overlapping but not identical ranges: lshift with dst above src, rshift with dst below src.
void check_shift_overlap(const bool left) {
    std::mt19937_64 rng{left ? 13U : 17U};
    for (std::size_t n = 1; n <= 9; ++n) {
        for (std::size_t offset = 1; offset <= 3; ++offset) {
            const unsigned          bits = 1 + static_cast<unsigned>(rng() % (limb_width - 1));
            const std::vector<limb> src  = random_limbs(rng, n);
            limb                    expected_out = 0;
            const auto              expected     = ref_shift(src, bits, left, expected_out);

            std::vector<limb> buf(n + offset, poison);
            limb*             s   = left ? buf.data() : buf.data() + offset;
            limb*             dst = left ? buf.data() + offset : buf.data();
            std::copy(src.begin(), src.end(), s);
            const limb out = left ? bb::detail::lshift_copy(dst, s, n, bits) : bb::detail::rshift_copy(dst, s, n, bits);
            ASSERT_EQ(out, expected_out);
            ASSERT_EQ(std::vector<limb>(dst, dst + n), expected) << "n=" << n << " offset=" << offset;
        }
    }
}

} // namespace

TEST(SpanPrimitives, LshiftCopySeparate) { check_shift(true, false); }
TEST(SpanPrimitives, LshiftCopyAliased) { check_shift(true, true); }
TEST(SpanPrimitives, LshiftCopyOverlapping) { check_shift_overlap(true); }
TEST(SpanPrimitives, RshiftCopySeparate) { check_shift(false, false); }
TEST(SpanPrimitives, RshiftCopyAliased) { check_shift(false, true); }
TEST(SpanPrimitives, RshiftCopyOverlapping) { check_shift_overlap(false); }

// ----- constant evaluation -----

namespace {

consteval bool constexpr_primitives() {
    std::array<limb, 5> a{max_limb, max_limb, max_limb, max_limb, 5};
    std::array<limb, 2> b{1, 0};
    std::array<limb, 5> dst{};
    // a + b: the carry ripples through four limbs.
    if (bb::detail::add_n_tail(dst, a, b) || dst[0] != 0 || dst[1] != 0 || dst[2] != 0 || dst[3] != 0 ||
        dst[4] != 6) {
        return false;
    }
    // In-place subtract undoes it.
    const std::array<limb, 5> sum = dst;
    if (bb::detail::sub_n_tail(dst, sum, b) || dst != a) {
        return false;
    }
    // 0 - 1 borrows out of the top.
    std::array<limb, 2> z{};
    std::array<limb, 2> one{1, 0};
    if (!bb::detail::sub_n_tail(z, std::array<limb, 2>{}, one) || z[0] != max_limb || z[1] != max_limb) {
        return false;
    }
    // Shifts round-trip.
    std::array<limb, 3> v{0x123, 0x456, 0x789};
    std::array<limb, 3> w{};
    const limb          out = bb::detail::lshift_copy(w.data(), v.data(), 3, 4);
    if (out != 0 || w[0] != 0x1230 || w[1] != 0x4560 || w[2] != 0x7890) {
        return false;
    }
    const limb lost = bb::detail::rshift_copy(w.data(), w.data(), 3, 4);
    return lost == 0 && w == v;
}
static_assert(constexpr_primitives());

} // namespace
