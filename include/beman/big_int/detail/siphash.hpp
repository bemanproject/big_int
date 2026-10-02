// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SIPHASH_HPP
#define BEMAN_BIG_INT_SIPHASH_HPP

#include <beman/big_int/detail/config.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstdint>
    #include <cstddef>
    #include <bit>
    #include <span>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

struct state_holder {
    std::uint64_t v0;
    std::uint64_t v1;
    std::uint64_t v2;
    std::uint64_t v3;
};

namespace impl {

[[nodiscard]] constexpr state_holder sipround(state_holder s) {
    s.v0 += s.v1;
    s.v1 = std::rotl(s.v1, 13);
    s.v1 ^= s.v0;
    s.v0 = std::rotl(s.v0, 32);

    s.v2 += s.v3;
    s.v3 = std::rotl(s.v3, 16);
    s.v3 ^= s.v2;

    s.v0 += s.v3;
    s.v3 = std::rotl(s.v3, 21);
    s.v3 ^= s.v0;

    s.v2 += s.v1;
    s.v1 = std::rotl(s.v1, 17);
    s.v1 ^= s.v2;
    s.v2 = std::rotl(s.v2, 32);
    return s;
}

[[nodiscard]] constexpr state_holder compress(state_holder s, const std::uint64_t m) {
    s.v3 ^= m;
    s = sipround(s);
    s = sipround(s);
    s.v0 ^= m;
    return s;
}

// Length of the little-endian byte string of the magnitude `limbs`, trimmed of its
// most significant zero bytes. That is ceil(bit_width(magnitude) / 8), which does not
// depend on how the magnitude is split into limbs. A zero magnitude has length zero.
[[nodiscard]] constexpr std::size_t significant_byte_len(const std::span<const uint_multiprecision_t> limbs) noexcept {
    for (std::size_t i = limbs.size(); i != 0; --i) {
        if (const auto top = limbs[i - 1]; top != 0) {
            return (i - 1) * sizeof(uint_multiprecision_t) +
                   div_to_pos_inf(static_cast<std::size_t>(std::bit_width(top)), std::size_t{8});
        }
    }
    return 0;
}

} // namespace impl

[[nodiscard]] constexpr std::size_t siphash(const std::span<const uint_multiprecision_t> limbs, const bool sign) {

    using impl::compress;
    using impl::sipround;

    constexpr std::uint64_t k0 = 0x0706050403020100ULL;
    constexpr std::uint64_t k1 = 0x0f0e0d0c0b0a0908ULL;
    state_holder            s{
        0x736f6d6570736575ULL ^ k0,
        0x646f72616e646f6dULL ^ k1,
        0x6c7967656e657261ULL ^ k0,
        0x7465646279746573ULL ^ k1,
    };

    // Fold the sign into the initial state so it propagates through every round
    if (sign) {
        s.v3 ^= std::numeric_limits<std::uint64_t>::max();
    }

    // SipHash-2-4 over the little-endian byte string of the magnitude, trimmed of its
    // most significant zero bytes.
    constexpr std::size_t limb_bytes = sizeof(uint_multiprecision_t);

    const auto byte_len = impl::significant_byte_len(limbs);

    const auto limb_at = [limbs](const std::size_t i) -> std::uint64_t {
        return i < limbs.size() ? static_cast<std::uint64_t>(limbs[i]) : std::uint64_t{0};
    };

    // Block k of that byte string
    const auto block_at = [&limb_at](const std::size_t k) -> std::uint64_t {
        if constexpr (limb_bytes == sizeof(std::uint64_t)) {
            return limb_at(k);
        } else {
            return limb_at(k * 2) | (limb_at(k * 2 + 1) << 32U);
        }
    };

    const auto blocks = byte_len / 8;
    for (std::size_t k = 0; k != blocks; ++k) {
        s = compress(s, block_at(k));
    }

    // Final block: length in the high byte, then the trailing partial block, which is
    // the low byte_len % 8 bytes of the block past the last full one.
    const auto tail_mask = (std::uint64_t{1} << ((byte_len % 8) * 8)) - 1;
    const auto b         = (static_cast<std::uint64_t>(byte_len) << 56) | (block_at(blocks) & tail_mask);

    s = compress(s, b);

    s.v2 ^= 0xFFULL;

    s = sipround(s);
    s = sipround(s);
    s = sipround(s);
    s = sipround(s);

    const auto h = s.v0 ^ s.v1 ^ s.v2 ^ s.v3;

    if constexpr (sizeof(std::size_t) == sizeof(std::uint64_t)) {
        return h;
    } else {
        BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
        BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wuseless-cast")
        return static_cast<std::size_t>(h ^ (h >> 32U));
        BEMAN_BIG_INT_DIAGNOSTIC_POP()
    }
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_SIPHASH_HPP
