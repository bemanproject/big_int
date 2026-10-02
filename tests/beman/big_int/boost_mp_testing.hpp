// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP
#define BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP

#define BOOST_MP_STANDALONE 1

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

#include <boost/multiprecision/cpp_int.hpp>

#include <cstddef>
#include <ios>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace boost_mp_testing {

namespace detail {

using cpp_int = ::boost::multiprecision::cpp_int;

// Generates a random hex string for a size of exactly `bits` bits,
// meaning the MSB is 1 (so the value is in [2^(bits-1), 2^bits)).
// `bits == 0` returns "0".
[[nodiscard]] inline std::string random_hex_of_bits(std::mt19937_64& rng, const std::size_t bits) {
    static constexpr char table[] = "0123456789abcdef";
    if (bits == 0) {
        return std::string{"0"};
    }

    const std::size_t num_hex  = (bits + 3) / 4;
    const std::size_t top_bits = bits - (num_hex - 1) * 4; // 1..4

    std::string s;
    s.reserve(num_hex);

    const unsigned                          top_high = 1u << (top_bits - 1);
    std::uniform_int_distribution<unsigned> top_rand(0, top_high - 1);
    s.push_back(table[top_high | top_rand(rng)]);

    std::uniform_int_distribution<unsigned> any(0, 15);
    for (std::size_t i = 1; i < num_hex; ++i) {
        s.push_back(table[any(rng)]);
    }
    return s;
}

// Parses a signed hex string (e.g. "deadbeef" or "-deadbeef"; no "0x" prefix) into a beman::big_int.
[[nodiscard]] inline ::BEMAN_BIG_INT_NAMESPACE::big_int parse_big_int(const std::string_view signed_hex) {
    ::BEMAN_BIG_INT_NAMESPACE::big_int out;
    const char* const                  first = signed_hex.data();
    const char* const                  last  = first + signed_hex.size();
    const auto [p, ec]                       = ::BEMAN_BIG_INT_NAMESPACE::from_chars(first, last, out, 16);
    if (ec != std::errc{} || p != last) {
        throw std::runtime_error("parse_big_int: from_chars failed to parse hex string");
    }
    return out;
}

// Parses a signed hex string (e.g. "deadbeef" or "-deadbeef"; no "0x" prefix) into a boost cpp_int.
[[nodiscard]] inline cpp_int parse_cpp_int(std::string_view signed_hex) {
    std::string s;
    s.reserve(signed_hex.size() + 3);
    if (!signed_hex.empty() && signed_hex.front() == '-') {
        s.push_back('-');
        signed_hex.remove_prefix(1);
    }
    s += "0x";
    s += signed_hex;
    return cpp_int{s};
}

// Returns the number of bytes before the trailing run of zero bytes.
[[nodiscard]] inline std::size_t significant_byte_len(const std::span<const std::byte> bytes) noexcept {
    std::size_t n = bytes.size();
    while (n > 0 && bytes[n - 1] == std::byte{0}) {
        --n;
    }
    return n;
}

// Asserts that a beman::big_int and a boost::cpp_int represent the same integer.
// Compares sign + zeroness directly, then compares magnitudes via std::as_bytes.
// Both libraries store limbs in little endian order,
// so the byte sequences are identical regardless of limb width
[[nodiscard]] inline ::testing::AssertionResult same_value(const ::BEMAN_BIG_INT_NAMESPACE::big_int& bn,
                                                           const cpp_int&                            cp) {
    const bool bn_zero = (bn == 0);
    const bool cp_zero = cp.is_zero();
    if (bn_zero != cp_zero) {
        return ::testing::AssertionFailure()
               << "zeroness mismatch: big_int::is_zero=" << bn_zero << " cpp_int::is_zero=" << cp_zero;
    }
    if (!bn_zero) {
        const bool bn_neg = (bn < 0);
        const bool cp_neg = cp.backend().sign();
        if (bn_neg != cp_neg) {
            return ::testing::AssertionFailure()
                   << "sign mismatch: big_int<0=" << bn_neg << " cpp_int.sign()=" << cp_neg;
        }
    }

    const std::span<const ::boost::multiprecision::limb_type> cp_rep{cp.backend().limbs(), cp.backend().size()};
    const auto                                                bn_bytes = std::as_bytes(bn.representation());
    const auto                                                cp_bytes = std::as_bytes(cp_rep);

    const auto bn_sig = significant_byte_len(bn_bytes);
    const auto cp_sig = significant_byte_len(cp_bytes);

    if (bn_sig != cp_sig) {
        return ::testing::AssertionFailure()
               << "significant byte count differs: big_int=" << bn_sig << " cpp_int=" << cp_sig;
    }
    for (std::size_t i = 0; i < bn_sig; ++i) {
        if (bn_bytes[i] != cp_bytes[i]) {
            return ::testing::AssertionFailure()
                   << "byte " << i << " differs: big_int=0x" << std::hex << std::to_integer<int>(bn_bytes[i])
                   << " cpp_int=0x" << std::to_integer<int>(cp_bytes[i]);
        }
    }
    return ::testing::AssertionSuccess();
}

} // namespace detail

// Applies binary-`op` to each backend after parsing `lhs` and `rhs` as signed hex
// strings (leading '-' allowed; no "0x" prefix), then asserts the two results
// match bit-for-bit. `op` must be a generic callable that accepts two
// integers of either library type, e.g. `std::plus<>{}`, `std::minus<>{}`,
// `std::multiplies<>{}`, or a user lambda.
//
// Example:
//     EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "deadbeefcafebabe", "1234567890abcdef"));
template <class BinOp>
[[nodiscard]] inline ::testing::AssertionResult
check_cpp_int_equal(BinOp&& op, const std::string_view lhs, const std::string_view rhs) {
    const auto a_bn = detail::parse_big_int(lhs);
    const auto b_bn = detail::parse_big_int(rhs);
    const auto a_cp = detail::parse_cpp_int(lhs);
    const auto b_cp = detail::parse_cpp_int(rhs);
    return detail::same_value(op(a_bn, b_bn), op(a_cp, b_cp));
}

// Applies unary-`op` to each backend after parsing `arg` as signed hex
// strings (leading '-' allowed; no "0x" prefix), then asserts the two results
// match bit-for-bit. `op` must be a generic callable that accepts one
// integer of either library type, e.g. a user function such as sqrt.
//
// Example:
//     EXPECT_TRUE(check_cpp_int_equal(sqrt_exec<>{}, "deadbeefcafebabe"));
template <class UnaryOp>
[[nodiscard]] inline ::testing::AssertionResult check_cpp_int_equal_unary(UnaryOp&& op, const std::string_view arg) {
    const auto a_bn = detail::parse_big_int(arg);
    const auto a_cp = detail::parse_cpp_int(arg);
    return detail::same_value(op(a_bn), op(a_cp));
}

// Returns a signed hex string representing a random integer whose magnitude
// has exactly `bits` bits (the MSB is set).
// If `negative` is true, the returned string is prefixed with '-'. `bits == 0` returns "0".
//
// The intended use is to feed the result directly into `check_cpp_int_equal`:
//     EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, random_big_int(1024), random_big_int(1024)));
//
// Uses a function-local std::mt19937_64 seeded with the fixed value 42 so
// that the sequence is deterministic across runs.
// The RNG state advances on every call and is shared across all callers of this function.
[[nodiscard]] inline std::string random_big_int(const std::size_t bits, const bool negative = false) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp) - deterministic seed is intentional for test reproducibility.
    static std::mt19937_64 rng{42};
    if (bits == 0) {
        return std::string{"0"};
    }
    std::string signed_hex;
    signed_hex.reserve(((bits + 3) / 4) + 1);
    if (negative) {
        signed_hex.push_back('-');
    }
    signed_hex += detail::random_hex_of_bits(rng, bits);
    return signed_hex;
}

} // namespace boost_mp_testing
BEMAN_BIG_INT_END_NAMESPACE

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP
