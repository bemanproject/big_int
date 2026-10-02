// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mod_arith.hpp>
#include <beman/big_int/detail/ntt.hpp>

#include "boost_mp_testing.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

namespace {

using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_modulus;
using ::BEMAN_BIG_INT_NAMESPACE::detail::ntt_primes;
using ::boost::multiprecision::cpp_int;

// --------------------------------------------------------------------------
// Compile-time validation of the constants and the Montgomery arithmetic.
// These checks are self-referential (no external reference needed): they pin
// down the n' identity, the Montgomery round-trip, the primitive-root and
// root-order properties, and the modular inverse.
// --------------------------------------------------------------------------
consteval bool check_modulus(const ntt_modulus m) {
    // p * (-p^-1) == -1 (mod 2^64).
    if (m.p * m.n_prime != ~std::uint64_t{0}) {
        return false;
    }

    // Montgomery conversion round-trips.
    for (const std::uint64_t x : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{2}, m.p - 1, m.p >> 1}) {
        if (m.from_mont(m.to_mont(x)) != x) {
            return false;
        }
    }

    // g is a primitive root: g^(p-1) == 1 and g^((p-1)/2) == p-1 (i.e. -1).
    if (m.pow(m.g, m.p - 1) != std::uint64_t{1}) {
        return false;
    }
    if (m.pow(m.g, (m.p - 1) >> 1) != m.p - 1) {
        return false;
    }

    // The maximal 2^k-th root of unity has exact order 2^k.
    const std::uint64_t w = m.root(m.log2_order);
    if (m.pow(w, std::uint64_t{1} << m.log2_order) != std::uint64_t{1}) {
        return false;
    }
    if (m.pow(w, std::uint64_t{1} << (m.log2_order - 1)) != m.p - 1) {
        return false;
    }

    // a * a^-1 == 1.
    for (const std::uint64_t a : {std::uint64_t{1}, std::uint64_t{2}, m.g, m.p - 1}) {
        if (m.mul(a, m.inv(a)) != std::uint64_t{1}) {
            return false;
        }
    }

    return true;
}

static_assert(check_modulus(ntt_primes[0]));
static_assert(check_modulus(ntt_primes[1]));
static_assert(check_modulus(ntt_primes[2]));

// --------------------------------------------------------------------------
// Randomized runtime checks against an independent boost::cpp_int reference.
// --------------------------------------------------------------------------
TEST(ModArith, MulMatchesReference) {
    std::mt19937_64 rng{0x1234abcdu};
    for (const auto& m : ntt_primes) {
        std::uniform_int_distribution<std::uint64_t> dist(0, m.p - 1);
        for (int iter = 0; iter < 20000; ++iter) {
            const std::uint64_t a   = dist(rng);
            const std::uint64_t b   = dist(rng);
            const auto          ref = static_cast<std::uint64_t>((cpp_int(a) * b) % m.p);
            ASSERT_EQ(m.mul(a, b), ref) << "p=" << m.p << " a=" << a << " b=" << b;
        }
    }
}

TEST(ModArith, AddSubMatchesReference) {
    std::mt19937_64 rng{0x55aa55aau};
    for (const auto& m : ntt_primes) {
        std::uniform_int_distribution<std::uint64_t> dist(0, m.p - 1);
        for (int iter = 0; iter < 20000; ++iter) {
            const std::uint64_t a = dist(rng);
            const std::uint64_t b = dist(rng);
            ASSERT_EQ(m.add(a, b), static_cast<std::uint64_t>((cpp_int(a) + b) % m.p));
            ASSERT_EQ(m.sub(a, b), static_cast<std::uint64_t>(((cpp_int(a) - b) % m.p + m.p) % m.p));
        }
    }
}

TEST(ModArith, PowMatchesReference) {
    std::mt19937_64 rng{0xdeadbeefu};
    for (const auto& m : ntt_primes) {
        std::uniform_int_distribution<std::uint64_t> base_dist(0, m.p - 1);
        std::uniform_int_distribution<std::uint64_t> exp_dist(0, (std::uint64_t{1} << 40) - 1);
        for (int iter = 0; iter < 2000; ++iter) {
            const std::uint64_t a = base_dist(rng);
            const std::uint64_t e = exp_dist(rng);
            const auto          ref =
                static_cast<std::uint64_t>(boost::multiprecision::powm(cpp_int(a), cpp_int(e), cpp_int(m.p)));
            ASSERT_EQ(m.pow(a, e), ref) << "p=" << m.p << " a=" << a << " e=" << e;
        }
    }
}

TEST(ModArith, InverseRoundTrips) {
    std::mt19937_64 rng{0x0badf00du};
    for (const auto& m : ntt_primes) {
        std::uniform_int_distribution<std::uint64_t> dist(1, m.p - 1);
        for (int iter = 0; iter < 20000; ++iter) {
            const std::uint64_t a = dist(rng);
            ASSERT_EQ(m.mul(a, m.inv(a)), std::uint64_t{1}) << "p=" << m.p << " a=" << a;
        }
    }
}

} // namespace
