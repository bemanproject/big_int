// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// This C++ work has benefited from parts of andreacorbellini/ecc (in Python script).
//   see also: https://github.com/andreacorbellini/ecc
//   and also: https://github.com/andreacorbellini/ecc/blob/master/scripts/ecdsa.py

// For a description of the geometric ECDSA algorithm, please consult also:
//   D. Hankerson, A. Menezes, S. Vanstone, "Guide to Elliptic
//   Curve Cryptography", Springer 2004, Chapter 4, in particular
//   Algorithm 4.24 (keygen on page 180), and Algorithms 4.29 and 4.30.
//   Complete descriptions of sign/verify are featured on page 184.

// For another algorithmic description of ECDSA,
//   see also: https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.186-5.pdf

// For algorithmic description of SHA-2 HASH-256,
//   see also: https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf

// The SHA-2 HASH-256 implementation has been taken (with slight modification)
//   from: https://github.com/imahjoub/hash_sha256

// You can define these for alternate big integer types in the elliptic curve calculations.
#define ELLIPTIC_CPP_INT_USE_STD_BIG_INT
// #define ELLIPTIC_CPP_INT_USE_GMP_INT

#if defined(ELLIPTIC_CPP_INT_USE_STD_BIG_INT)
    #include <beman/big_int.hpp>
#elif defined(ELLIPTIC_CPP_INT_USE_GMP_INT)
    #include <boost/multiprecision/gmp.hpp>
#else
    #include <boost/multiprecision/cpp_int.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(ELLIPTIC_CPP_INT_USE_STD_BIG_INT)
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")
#endif

#if defined(ELLIPTIC_CPP_INT_USE_STD_BIG_INT)
using big_sint_type = BEMAN_BIG_INT_NAMESPACE::big_int;
#elif defined(ELLIPTIC_CPP_INT_USE_GMP_INT)
using big_sint_backend_type = boost::multiprecision::gmp_int;
using big_sint_type         = boost::multiprecision::number<big_sint_backend_type, boost::multiprecision::et_off>;
#else
using big_sint_backend_type = boost::multiprecision::cpp_int_backend<>;
using big_sint_type         = boost::multiprecision::number<big_sint_backend_type, boost::multiprecision::et_off>;
#endif

namespace ckormanyos::big_int::example::local::concurrency {

template <class ClockType = std::chrono::high_resolution_clock>
struct stopwatch {
  public:
    using time_point_type = std::uint64_t;

    auto reset() -> void { m_start = now(); }

    template <class RepresentationRequestedTimeType>
    static auto elapsed_time(const stopwatch& my_stopwatch) noexcept -> RepresentationRequestedTimeType {
        using local_time_type = RepresentationRequestedTimeType;

        return local_time_type{static_cast<local_time_type>(my_stopwatch.elapsed()) /
                               local_time_type{UINTMAX_C(1000000000)}};
    }

  private:
    time_point_type m_start{now()};

    [[nodiscard]] static auto now() -> time_point_type {
        using local_clock_type = ClockType;

        const auto current_now = static_cast<std::uintmax_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(local_clock_type::now().time_since_epoch()).count());

        return static_cast<time_point_type>(current_now);
    }

    [[nodiscard]] auto elapsed() const -> time_point_type {
        const time_point_type stop{now()};

        const time_point_type elapsed_ns{stop - m_start};

        return elapsed_ns;
    }
};

} // namespace ckormanyos::big_int::example::local::concurrency

namespace ckormanyos::big_int::example {

namespace detail {

auto div_rem_to_neg_inf(const big_sint_type& a, const big_sint_type& b) -> std::pair<big_sint_type, big_sint_type>;

auto div_rem_to_neg_inf(const big_sint_type& a, const big_sint_type& b) -> std::pair<big_sint_type, big_sint_type> {

    const bool numer_was_neg{a < 0};
    const bool denom_was_neg{b < 0};

    big_sint_type       ua{(!numer_was_neg) ? a : -a};
    const big_sint_type ub{(!denom_was_neg) ? b : -b};

    const big_sint_type quotient{ua / ub};
    big_sint_type       ur{ua - (ub * quotient)};

    ua = quotient;

    using divmod_result_pair_type = std::pair<big_sint_type, big_sint_type>;

    divmod_result_pair_type result{big_sint_type{}, big_sint_type{}};

    if (numer_was_neg == denom_was_neg) {
        result.first  = big_sint_type{ua};
        result.second = (!numer_was_neg) ? big_sint_type{ur} : -big_sint_type{ur};
    } else {
        const bool division_is_exact{ur == static_cast<unsigned>(UINT8_C(0))};

        if (!division_is_exact) {
            ++ua;
        }

        result.first = big_sint_type(ua);

        result.first = -result.first;

        if (!division_is_exact) {
            ur -= ub;
        }

        result.second = big_sint_type{ur};

        if (!denom_was_neg) {
            result.second = -result.second;
        }
    }

    return result;
}

template <class BigIntegerType, typename IteratorType>
auto import_bits(BigIntegerType& value, IteratorType first, IteratorType last) -> void {
    std::string str{};

    while (first != last) {
        std::stringstream strm{};

        strm << std::hex << std::setw(2) << std::setfill('0') << unsigned{*first++};

        str.insert(str.length(), strm.str());
    }

#if defined(ELLIPTIC_CPP_INT_USE_STD_BIG_INT)
    static_cast<void>(from_chars(str.c_str(), str.c_str() + str.length(), value, 16));
#else
    value = BigIntegerType("0x" + str);
#endif
}

auto from_chars_16(const char* first) -> big_sint_type {
    big_sint_type value{};

#if defined(ELLIPTIC_CPP_INT_USE_STD_BIG_INT)
    static_cast<void>(from_chars(first, first + strlen(first), value, 16));
#else
    value = big_sint_type(std::string("0x") + first);
#endif

    return value;
}

} // namespace detail

class hash_sha256 {
  private:
    using transform_context_type = std::array<std::uint32_t, static_cast<std::size_t>(UINT8_C(8))>;
    using data_array_type        = std::array<std::uint8_t, static_cast<std::size_t>(UINT8_C(64))>;

    using data_array_size_type        = typename data_array_type::size_type;
    using transform_context_size_type = typename transform_context_type::size_type;

  public:
    using result_type = std::array<std::uint8_t, static_cast<std::size_t>(UINT8_C(32))>;

    // LCOV_EXCL_START
    constexpr hash_sha256()                       = default;
    constexpr hash_sha256(const hash_sha256&)     = default;
    constexpr hash_sha256(hash_sha256&&) noexcept = default;
    ~hash_sha256()                                = default;

    constexpr auto operator=(hash_sha256&&) noexcept -> hash_sha256& = default;
    constexpr auto operator=(const hash_sha256&) -> hash_sha256&     = default;
    // LCOV_EXCL_STOP

    constexpr auto hash(const std::uint8_t* msg, const size_t length) -> result_type {
        init();
        update(msg, length);
        return finalize();
    }

    constexpr void init() {
        my_datalen = static_cast<std::uint32_t>(UINT8_C(0));
        my_bitlen  = static_cast<std::uint64_t>(UINT8_C(0));

        transform_context[static_cast<transform_context_size_type>(UINT8_C(0))] = UINT32_C(0x6A09E667);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(1))] = UINT32_C(0xBB67AE85);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(2))] = UINT32_C(0x3C6EF372);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(3))] = UINT32_C(0xA54FF53A);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(4))] = UINT32_C(0x510E527F);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(5))] = UINT32_C(0x9B05688C);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(6))] = UINT32_C(0x1F83D9AB);
        transform_context[static_cast<transform_context_size_type>(UINT8_C(7))] = UINT32_C(0x5BE0CD19);
    }

    constexpr void update(const std::uint8_t* msg, const size_t length) {

        for (auto i = static_cast<std::size_t>(UINT8_C(0)); i < length; ++i) {
            my_data[static_cast<data_array_size_type>(my_datalen)] = msg[i];
            my_datalen++;

            if (my_datalen == static_cast<std::uint32_t>(UINT8_C(64))) {
                // LCOV_EXCL_START
                sha256_transform();

                my_datalen = static_cast<std::uint32_t>(UINT8_C(0));

                my_bitlen = my_bitlen + static_cast<std::uint_fast16_t>(UINT16_C(512));
                // LCOV_EXCL_STOP
            }
        }
    }

    constexpr auto finalize() -> result_type {
        result_type hash_result{};

        auto hash_index = static_cast<std::size_t>(my_datalen);

        my_data[static_cast<data_array_size_type>(hash_index)] = static_cast<std::uint8_t>(UINT8_C(0x80));

        ++hash_index;

        // Pad whatever data is left in the buffer.
        if (my_datalen < static_cast<std::uint32_t>(UINT8_C(56))) {
            std::fill((my_data.begin() + hash_index),
                      (my_data.begin() + static_cast<std::size_t>(UINT8_C(56))),
                      static_cast<std::uint8_t>(UINT8_C(0)));
        } else {
            // LCOV_EXCL_START
            std::fill((my_data.begin() + hash_index), my_data.end(), static_cast<std::uint8_t>(UINT8_C(0)));

            sha256_transform();

            std::fill(my_data.begin(),
                      my_data.begin() + static_cast<std::size_t>(UINT8_C(56)),
                      static_cast<std::uint8_t>(UINT8_C(0)));
            // LCOV_EXCL_STOP
        }

        // Append to the padding the total message length (in bits) and subsequently transform.
        my_bitlen = (my_bitlen + (static_cast<std::uint64_t>(my_datalen) * static_cast<std::uint8_t>(UINT8_C(8))));

        my_data[static_cast<data_array_size_type>(UINT8_C(63))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(0)));
        my_data[static_cast<data_array_size_type>(UINT8_C(62))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(8)));
        my_data[static_cast<data_array_size_type>(UINT8_C(61))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(16)));
        my_data[static_cast<data_array_size_type>(UINT8_C(60))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(24)));
        my_data[static_cast<data_array_size_type>(UINT8_C(59))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(32)));
        my_data[static_cast<data_array_size_type>(UINT8_C(58))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(40)));
        my_data[static_cast<data_array_size_type>(UINT8_C(57))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(48)));
        my_data[static_cast<data_array_size_type>(UINT8_C(56))] =
            static_cast<std::uint8_t>(my_bitlen >> static_cast<unsigned>(UINT8_C(56)));

        sha256_transform();

        // Since this implementation uses little endian byte ordering and SHA uses big endian,
        // reverse all the bytes when copying the final transform_context to the output hash.
        constexpr auto conversion_scale =
            static_cast<std::size_t>(std::numeric_limits<typename transform_context_type::value_type>::digits /
                                     std::numeric_limits<std::uint8_t>::digits);

        for (auto output_index = static_cast<std::size_t>(UINT8_C(0));
             output_index < std::tuple_size<result_type>::value;
             ++output_index) {

            const auto right_shift_amount =
                ((conversion_scale - (UINT8_C(1))) - (output_index % conversion_scale)) * UINT8_C(8);

            hash_result[output_index] =
                static_cast<std::uint8_t>(transform_context[output_index / conversion_scale] >> right_shift_amount);
        }

        return hash_result;
    }

  private:
    std::uint32_t          my_datalen{};
    std::uint64_t          my_bitlen{};
    data_array_type        my_data{};
    transform_context_type transform_context{};

    constexpr auto sha256_transform() -> void {
        std::array<std::uint32_t, static_cast<std::size_t>(UINT8_C(64))> m{};

        for (auto i = static_cast<std::size_t>(UINT8_C(0)), j = static_cast<std::size_t>(UINT8_C(0));
             i < static_cast<std::size_t>(UINT8_C(16));
             ++i, j = j + static_cast<std::size_t>(UINT8_C(4))) {
            m[i] = (static_cast<std::uint32_t>(my_data[j + UINT8_C(0)]) << static_cast<unsigned>(UINT8_C(24))) |
                   (static_cast<std::uint32_t>(my_data[j + UINT8_C(1)]) << static_cast<unsigned>(UINT8_C(16))) |
                   (static_cast<std::uint32_t>(my_data[j + UINT8_C(2)]) << static_cast<unsigned>(UINT8_C(8))) |
                   (static_cast<std::uint32_t>(my_data[j + UINT8_C(3)]) << static_cast<unsigned>(UINT8_C(0)));
        }

        for (auto i = static_cast<std::size_t>(UINT8_C(16)); i < static_cast<std::size_t>(UINT8_C(64)); ++i) {
            m[i] = ssig1(m[i - static_cast<std::size_t>(UINT8_C(2))]) + m[i - static_cast<std::size_t>(UINT8_C(7))] +
                   ssig0(m[i - static_cast<std::size_t>(UINT8_C(15))]) + m[i - static_cast<std::size_t>(UINT8_C(16))];
        }

        constexpr std::array<std::uint32_t, 64U> transform_constants{
            UINT32_C(0x428A2F98), UINT32_C(0x71374491), UINT32_C(0xB5C0FBCF), UINT32_C(0xE9B5DBA5),
            UINT32_C(0x3956C25B), UINT32_C(0x59F111F1), UINT32_C(0x923F82A4), UINT32_C(0xAB1C5ED5),
            UINT32_C(0xD807AA98), UINT32_C(0x12835B01), UINT32_C(0x243185BE), UINT32_C(0x550C7DC3),
            UINT32_C(0x72BE5D74), UINT32_C(0x80DEB1FE), UINT32_C(0x9BDC06A7), UINT32_C(0xC19BF174),
            UINT32_C(0xE49B69C1), UINT32_C(0xEFBE4786), UINT32_C(0x0FC19DC6), UINT32_C(0x240CA1CC),
            UINT32_C(0x2DE92C6F), UINT32_C(0x4A7484AA), UINT32_C(0x5CB0A9DC), UINT32_C(0x76F988DA),
            UINT32_C(0x983E5152), UINT32_C(0xA831C66D), UINT32_C(0xB00327C8), UINT32_C(0xBF597FC7),
            UINT32_C(0xC6E00BF3), UINT32_C(0xD5A79147), UINT32_C(0x06CA6351), UINT32_C(0x14292967),
            UINT32_C(0x27B70A85), UINT32_C(0x2E1B2138), UINT32_C(0x4D2C6DFC), UINT32_C(0x53380D13),
            UINT32_C(0x650A7354), UINT32_C(0x766A0ABB), UINT32_C(0x81C2C92E), UINT32_C(0x92722C85),
            UINT32_C(0xA2BFE8A1), UINT32_C(0xA81A664B), UINT32_C(0xC24B8B70), UINT32_C(0xC76C51A3),
            UINT32_C(0xD192E819), UINT32_C(0xD6990624), UINT32_C(0xF40E3585), UINT32_C(0x106AA070),
            UINT32_C(0x19A4C116), UINT32_C(0x1E376C08), UINT32_C(0x2748774C), UINT32_C(0x34B0BCB5),
            UINT32_C(0x391C0CB3), UINT32_C(0x4ED8AA4A), UINT32_C(0x5B9CCA4F), UINT32_C(0x682E6FF3),
            UINT32_C(0x748F82EE), UINT32_C(0x78A5636F), UINT32_C(0x84C87814), UINT32_C(0x8CC70208),
            UINT32_C(0x90BEFFFA), UINT32_C(0xA4506CEB), UINT32_C(0xBEF9A3F7), UINT32_C(0xC67178F2)};

        transform_context_type state = transform_context;

        for (auto i = static_cast<std::size_t>(UINT8_C(0)); i < static_cast<std::size_t>(UINT8_C(64)); ++i) {
            const auto tmp1 = state[static_cast<std::size_t>(UINT8_C(7))] +
                              bsig1(state[static_cast<std::size_t>(UINT8_C(4))]) +
                              ch(state[static_cast<std::size_t>(UINT8_C(4))],
                                 state[static_cast<std::size_t>(UINT8_C(5))],
                                 state[static_cast<std::size_t>(UINT8_C(6))]) +
                              transform_constants[i] + m[i];

            const auto tmp2 =
                bsig0(state[static_cast<std::size_t>(UINT8_C(0))]) + maj(state[static_cast<std::size_t>(UINT8_C(0))],
                                                                         state[static_cast<std::size_t>(UINT8_C(1))],
                                                                         state[static_cast<std::size_t>(UINT8_C(2))]);

            state[static_cast<std::size_t>(UINT8_C(7))] = state[static_cast<std::size_t>(UINT8_C(6))];
            state[static_cast<std::size_t>(UINT8_C(6))] = state[static_cast<std::size_t>(UINT8_C(5))];
            state[static_cast<std::size_t>(UINT8_C(5))] = state[static_cast<std::size_t>(UINT8_C(4))];
            state[static_cast<std::size_t>(UINT8_C(4))] = state[static_cast<std::size_t>(UINT8_C(3))] + tmp1;
            state[static_cast<std::size_t>(UINT8_C(3))] = state[static_cast<std::size_t>(UINT8_C(2))];
            state[static_cast<std::size_t>(UINT8_C(2))] = state[static_cast<std::size_t>(UINT8_C(1))];
            state[static_cast<std::size_t>(UINT8_C(1))] = state[static_cast<std::size_t>(UINT8_C(0))];
            state[static_cast<std::size_t>(UINT8_C(0))] = tmp1 + tmp2;
        }

        transform_context[static_cast<std::size_t>(UINT8_C(0))] += state[static_cast<std::size_t>(UINT8_C(0))];
        transform_context[static_cast<std::size_t>(UINT8_C(1))] += state[static_cast<std::size_t>(UINT8_C(1))];
        transform_context[static_cast<std::size_t>(UINT8_C(2))] += state[static_cast<std::size_t>(UINT8_C(2))];
        transform_context[static_cast<std::size_t>(UINT8_C(3))] += state[static_cast<std::size_t>(UINT8_C(3))];
        transform_context[static_cast<std::size_t>(UINT8_C(4))] += state[static_cast<std::size_t>(UINT8_C(4))];
        transform_context[static_cast<std::size_t>(UINT8_C(5))] += state[static_cast<std::size_t>(UINT8_C(5))];
        transform_context[static_cast<std::size_t>(UINT8_C(6))] += state[static_cast<std::size_t>(UINT8_C(6))];
        transform_context[static_cast<std::size_t>(UINT8_C(7))] += state[static_cast<std::size_t>(UINT8_C(7))];
    }

    static constexpr auto rotl(std::uint32_t a, unsigned b) -> std::uint32_t {
        return ((a << b) | (a >> (static_cast<unsigned>(UINT8_C(32)) - b)));
    }
    static constexpr auto rotr(std::uint32_t a, unsigned b) -> std::uint32_t {
        return ((a >> b) | (a << (static_cast<unsigned>(UINT8_C(32)) - b)));
    }

    static constexpr auto ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) -> std::uint32_t {
        return ((x & y) ^ (~x & z));
    }
    static constexpr auto maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) -> std::uint32_t {
        return ((x & y) ^ (x & z) ^ (y & z));
    }

    static constexpr auto bsig0(std::uint32_t x) -> std::uint32_t {
        return (rotr(x, static_cast<unsigned>(UINT8_C(2))) ^ rotr(x, static_cast<unsigned>(UINT8_C(13))) ^
                rotr(x, static_cast<unsigned>(UINT8_C(22))));
    }
    static constexpr auto bsig1(std::uint32_t x) -> std::uint32_t {
        return (rotr(x, static_cast<unsigned>(UINT8_C(6))) ^ rotr(x, static_cast<unsigned>(UINT8_C(11))) ^
                rotr(x, static_cast<unsigned>(UINT8_C(25))));
    }
    static constexpr auto ssig0(std::uint32_t x) -> std::uint32_t {
        return (rotr(x, static_cast<unsigned>(UINT8_C(7))) ^ rotr(x, static_cast<unsigned>(UINT8_C(18))) ^
                (x >> static_cast<unsigned>(UINT8_C(3))));
    }
    static constexpr auto ssig1(std::uint32_t x) -> std::uint32_t {
        return (rotr(x, static_cast<unsigned>(UINT8_C(17))) ^ rotr(x, static_cast<unsigned>(UINT8_C(19))) ^
                (x >> static_cast<unsigned>(UINT8_C(10))));
    }
};

struct ecc_point {
    const unsigned CurveBits;
    const char*    CoordX;
    const char*    CoordY;

    ecc_point(const unsigned curve_bits, const char* coord_x, const char* coord_y)
        : CurveBits(curve_bits), CoordX(coord_x), CoordY(coord_y) {}

    struct my_point_type {
        explicit my_point_type(big_sint_type x = 0U, big_sint_type y = 0U) noexcept
            : my_x{x}, my_y{y} {} // LCOV_EXCL_LINE

        big_sint_type my_x;
        big_sint_type my_y;
    };

    using point_type = my_point_type;
};

class elliptic_curve : public ecc_point {
  public:
    explicit elliptic_curve(const unsigned curve_bits,
                            const char*    curve_name,
                            const char*    field_characteristic_p,
                            const char*    curve_coefficient_a,
                            const char*    curve_coefficient_b,
                            const char*    coord_gx,
                            const char*    coord_gy,
                            const char*    subgroup_order_n,
                            const int      subgroup_cofactor_h)
        : ecc_point(curve_bits, coord_gx, coord_gy),
          CurveName(curve_name),
          FieldCharacteristicP(field_characteristic_p),
          CurveCoefficientA(curve_coefficient_a),
          CurveCoefficientB(curve_coefficient_b),
          SubGroupOrderN(subgroup_order_n),
          SubGroupCoFactorH(subgroup_cofactor_h) {
        static_cast<void>(CurveName[std::size_t{UINT8_C(0)}]);
        static_cast<void>(SubGroupCoFactorH);
    }

    using base_class_type = ecc_point;

    using point_type = typename base_class_type::point_type;

    using keypair_type = std::pair<big_sint_type, std::pair<big_sint_type, big_sint_type>>;

    auto curve_p() noexcept -> big_sint_type { return detail::from_chars_16(FieldCharacteristicP); }
    auto curve_a() noexcept -> big_sint_type { return detail::from_chars_16(CurveCoefficientA); }
    auto curve_b() noexcept -> big_sint_type { return detail::from_chars_16(CurveCoefficientB); }

    auto curve_gx() noexcept -> big_sint_type { return detail::from_chars_16(CoordX); }
    auto curve_gy() noexcept -> big_sint_type { return detail::from_chars_16(CoordY); }

    auto curve_n() noexcept -> big_sint_type { return detail::from_chars_16(SubGroupOrderN); }

    auto inverse_mod(const big_sint_type& k, const big_sint_type& p) -> big_sint_type {
        // Returns the inverse of k modulo p.
        // This function returns the only integer x such that (x * k) % p == 1.
        // k must be non-zero and p must be a prime.

        if (k == 0) {
            // Error: Division by zero.
            return 0; // LCOV_EXCL_LINE
        }

        if (k < 0) {
            // k ** -1 = p - (-k) ** -1  (mod p)
            return p - inverse_mod(-k, p);
        }

        // Extended Euclidean algorithm.
        big_sint_type s{static_cast<unsigned>(UINT8_C(0))};
        big_sint_type old_s{static_cast<unsigned>(UINT8_C(1))};

        big_sint_type r{p};
        big_sint_type old_r{k};

        while (r != 0U) {
            const big_sint_type quotient{detail::div_rem_to_neg_inf(old_r, r).first};

            const big_sint_type tmp_r{r};
            r     = old_r - (quotient * r);
            old_r = tmp_r;

            const big_sint_type tmp_s{s};
            s     = old_s - (quotient * s);
            old_s = tmp_s;
        }

        return detail::div_rem_to_neg_inf(old_s, p).second;
    }

    // Functions that work on curve points

    auto is_on_curve(const point_type& point) -> bool {
        // Returns true if the given point lies on the elliptic curve.
        // Otherwise returns false.

        if ((point.my_x == 0) && (point.my_y == 0)) {
            // Zero represents the point at infinity.
            return true; // LCOV_EXCL_LINE
        }

        // Test the condition:
        //   (y * y - x * x * x - curve.a * x -curve.b) % curve.p == 0

        const big_sint_type num{(point.my_y * point.my_y) - (point.my_x * (point.my_x * point.my_x)) -
                                (point.my_x * curve_a()) - curve_b()};

        const big_sint_type divmod_result{detail::div_rem_to_neg_inf(num, curve_p()).second};

        return (divmod_result == 0);
    }

    // LCOV_EXCL_START
    auto point_neg(const point_type& point) -> point_type {
        // Returns the negation of the point on the curve (i.e., -point).

        return {((point.my_x == 0) && (point.my_y == 0))
                    ? point_type{}
                    : point_type{point.my_x, -detail::div_rem_to_neg_inf(point.my_y, curve_p()).second}};
    }
    // LCOV_EXCL_STOP

    auto point_add(const point_type& point1, const point_type& point2) -> point_type {
        // Returns the result of (point1 + point2) according to the group law.

        const auto& x1{point1.my_x};
        const auto& y1{point1.my_y};
        const auto& x2{point2.my_x};
        const auto& y2{point2.my_y};

        if ((x1 == 0) && (y1 == 0)) {
            // 0 + point2 = point2
            return point2;
        }

        if ((x2 == 0) && (y2 == 0)) {
            // point1 + 0 = point1
            return point1; // LCOV_EXCL_LINE
        }

        if ((x1 == x2) && (y1 != y2)) {
            // Equivalent to: point1 + (-point1) = 0
            return point_type{}; // LCOV_EXCL_LINE
        }

        // Differentiate the cases (point1 == point2) and (point1 != point2).

        const big_sint_type m{(x1 == x2) ? (x1 * x1 * 3 + curve_a()) * inverse_mod(y1 * 2, curve_p())
                                         : (y1 - y2) * inverse_mod(x1 - x2, curve_p())};

        const big_sint_type x3{(m * m) - (x1 + x2)};

        // Negate y3 for the modulus operation below.
        const big_sint_type y3{(m * (x1 - x3)) - y1};

        return point_type{detail::div_rem_to_neg_inf(x3, curve_p()).second,
                          detail::div_rem_to_neg_inf(y3, curve_p()).second};
    }

    auto scalar_mult(const big_sint_type& k, const point_type& point) -> point_type { // NOLINT(misc-no-recursion)
        // Returns k * point computed using the double and point_add algorithm.

        if (((k % curve_n()) == 0) || ((point.my_x == 0) && (point.my_y == 0))) {
            return point_type{}; // LCOV_EXCL_LINE
        }

        if (k < 0) {
            // k * point = -k * (-point)
            return scalar_mult(-k, point_neg(point)); // LCOV_EXCL_LINE
        }

        point_type result{};
        point_type addend{point};

        big_sint_type k_val{k};

        do {
            const auto lo_bit = static_cast<unsigned>(k_val) & static_cast<unsigned>(UINT8_C(1));

            if (lo_bit != static_cast<unsigned>(UINT8_C(0))) {
                // Add.
                result = point_add(result, addend);
            }

            // Double.
            addend = point_add(addend, addend);

            k_val >>= static_cast<unsigned>(UINT8_C(1));
        } while (k_val != 0);

        return result;
    }

    template <class UnknownWideUintType>
    static auto get_pseudo_random_uint(const unsigned             bits_to_get,
                                       const UnknownWideUintType& max_value = UnknownWideUintType(0U))
        -> UnknownWideUintType {
        using local_wide_unsigned_integer_type = UnknownWideUintType;

        local_wide_unsigned_integer_type unsigned_pseudo_random_value{
            get_pseudo_random_uint_worker<local_wide_unsigned_integer_type>(bits_to_get)};

        if ((max_value != 0U) && (unsigned_pseudo_random_value > max_value)) {
            unsigned_pseudo_random_value = unsigned_pseudo_random_value % max_value;
        }

        return unsigned_pseudo_random_value;
    }

    auto make_keypair(const big_sint_type* p_uint_seed = nullptr) -> keypair_type {
        // This subroutine generates a random private-public key pair.
        // The input parameter p_uint_seed can, however, be used to
        // provide a fixed-input value for the private key.
        // Also be sure to limit to random.randrange(1, curve.n).

        const auto private_key =
            big_sint_type{(p_uint_seed == nullptr)
                              ? get_pseudo_random_uint(
                                    static_cast<unsigned>(std::tuple_size<typename hash_sha256::result_type>::value *
                                                          std::size_t{8U}),
                                    curve_n())
                              : *p_uint_seed};

        const auto public_key = scalar_mult(private_key, point_type(curve_gx(), curve_gy()));

        return {private_key, {big_sint_type{public_key.my_x}, big_sint_type{public_key.my_y}}};
    }

    template <class MsgIteratorType>
    auto hash_message(MsgIteratorType msg_first, MsgIteratorType msg_last) -> big_sint_type {

        // This subroutine returns the hash of the message (msg) as a big integer.
        // The type of the hash is 256-bit SHA2, as implemented locally above.
        // Thereby the returned big integer type is also 256-bits in width.

        // For those interested in the general case of ECC, a larger/smaller
        // bit-length hash needs to be left/right shifted for cases when there
        // are different hash/curve bit-lengths (as specified in FIPS 180).

        const std::vector<std::uint8_t> message(msg_first, msg_last);

        using hash_type = hash_sha256;

        hash_type hash_object{};

        const typename hash_type::result_type hash_result{hash_object.hash(message.data(), message.size())};

        big_sint_type z{};

        detail::import_bits(z, hash_result.cbegin(), hash_result.cend());

        return z;
    }

    template <class MsgIteratorType>
    auto sign_message(const big_sint_type& private_key,
                      MsgIteratorType      msg_first,
                      MsgIteratorType      msg_last,
                      const big_sint_type* p_uint_seed = nullptr) -> std::pair<big_sint_type, big_sint_type> {

        // This subroutine signs a pre-hashed message and returns the
        // point {r,s}. These are the signature components on the
        // elliptic curve.

        const auto z{hash_message(msg_first, msg_last)};

        big_sint_type r{};
        big_sint_type s{};

        const auto n{curve_n()};

        const auto pk{private_key};

        while ((r == 0) || (s == 0)) {
            const big_sint_type uk{
                (p_uint_seed == nullptr)
                    ? std::move(get_pseudo_random_uint<big_sint_type>(static_cast<unsigned>(
                          std::tuple_size<typename hash_sha256::result_type>::value * std::size_t{8U})))
                    : *p_uint_seed};

            const big_sint_type k{uk};

            const point_type pt{scalar_mult(k, point_type(curve_gx(), curve_gy()))};

            r = detail::div_rem_to_neg_inf(pt.my_x, curve_n()).second;

            const big_sint_type num{(z + (r * pk)) * inverse_mod(k, curve_n())};

            s = detail::div_rem_to_neg_inf(num, n).second;
        }

        return {big_sint_type(r), big_sint_type(s)};
    }

    template <class MsgIteratorType>
    auto verify_signature(const std::pair<big_sint_type, big_sint_type>& pub,
                          MsgIteratorType                                msg_first,
                          MsgIteratorType                                msg_last,
                          const std::pair<big_sint_type, big_sint_type>& sig) -> bool {

        // This subroutine verifies a signed, pre-hashed message using
        // the public key and the point {r,s} (i.e., the signature components)
        // on the elliptic curve.

        const big_sint_type w(inverse_mod(sig.second, curve_n()));

        const auto z = hash_message(msg_first, msg_last);

        const big_sint_type u1{detail::div_rem_to_neg_inf(z * w, curve_n()).second};
        const big_sint_type u2{detail::div_rem_to_neg_inf(sig.first * w, curve_n()).second};

        const auto pt = point_add(scalar_mult(u1, point_type(curve_gx(), curve_gy())),
                                  scalar_mult(u2, point_type(pub.first, pub.second)));

        return (detail::div_rem_to_neg_inf(sig.first, curve_n()).second ==
                detail::div_rem_to_neg_inf(pt.my_x, curve_n()).second);
    }

  private:
    const char* CurveName;
    const char* FieldCharacteristicP;
    const char* CurveCoefficientA;
    const char* CurveCoefficientB;
    const char* SubGroupOrderN;
    const int   SubGroupCoFactorH;

    template <class UnknownWideUintType>
    static auto get_pseudo_random_uint_worker(const unsigned bits_to_get) -> UnknownWideUintType {
        using local_wide_unsigned_integer_type = UnknownWideUintType;

        using local_distribution_type = std::uniform_int_distribution<std::uint64_t>;

        using local_random_engine_type = std::mt19937_64;
        using local_random_device_type = std::random_device;

        local_random_device_type dev{};

        const auto seed_value = static_cast<typename local_random_engine_type::result_type>(dev());

        local_random_engine_type generator(seed_value);

        local_distribution_type dist{std::uint64_t{UINT64_C(0x1000000000000001)},
                                     std::uint64_t{UINT64_C(0xFFFFFFFFFFFFFFFF)}};

        local_wide_unsigned_integer_type unsigned_pseudo_random_value{};

        for (unsigned bit_index = 0U; bit_index < bits_to_get; bit_index += 64U) {
            if (bit_index != 0U) {
                unsigned_pseudo_random_value <<= 64U;
            }

            unsigned_pseudo_random_value += dist(generator);
        }

        return unsigned_pseudo_random_value;
    }
};

namespace curve_params {

inline constexpr char CurveName[]            = "secp256k1";
inline constexpr char FieldCharacteristicP[] = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F";
inline constexpr char CurveCoefficientA[]    = "0";
inline constexpr char CurveCoefficientB[]    = "7";
inline constexpr char BasePointGx[]          = "79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798";
inline constexpr char BasePointGy[]          = "483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8";
inline constexpr char SubGroupOrderN[]       = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141";
inline constexpr auto SubGroupCoFactorH      = 1;

} // namespace curve_params

} // namespace ckormanyos::big_int::example

namespace ckormanyos::big_int::example {
auto ecdsa_sign_verify() -> bool;
} // namespace ckormanyos::big_int::example

auto ckormanyos::big_int::example::ecdsa_sign_verify() -> bool {
    auto result_is_ok = true;

    using elliptic_curve_type = elliptic_curve;

    elliptic_curve_type my_elliptic_curve(static_cast<unsigned>(UINT16_C(256)),
                                          curve_params::CurveName,
                                          curve_params::FieldCharacteristicP,
                                          curve_params::CurveCoefficientA,
                                          curve_params::CurveCoefficientB,
                                          curve_params::BasePointGx,
                                          curve_params::BasePointGy,
                                          curve_params::SubGroupOrderN,
                                          curve_params::SubGroupCoFactorH);

    // Declare the message "Hello!" as an array of chars.
    constexpr std::array<char, static_cast<std::size_t>(UINT8_C(6))> msg_as_array{'H', 'e', 'l', 'l', 'o', '!'};

    // Get the message to sign as a string and ensure that it is "Hello!".
    const auto msg_as_string = std::string(msg_as_array.cbegin(), msg_as_array.cend());

    const auto result_msg_as_string_is_ok = (msg_as_string == "Hello!");

    result_is_ok = (result_msg_as_string_is_ok && result_is_ok);

    {
        // Test the hash SHA-2 HASH-256 implementation.

        const auto hash_result = my_elliptic_curve.hash_message(msg_as_array.cbegin(), msg_as_array.cend());

        const auto result_hash_is_ok =
            (hash_result == ckormanyos::big_int::example::detail::from_chars_16(
                                "334D016F755CD6DC58C53A86E183882F8EC14F52FB05345887C8A5EDD42C87B7"));

        result_is_ok = (result_hash_is_ok && result_is_ok);
    }

    {
        // Test ECC key generation, sign and verify. In this case we use random
        // (but pre-defined) seeds for both keygen as well as signing.

        const auto seed_keygen = ckormanyos::big_int::example::detail::from_chars_16(
            "C6455BF2F380F6B81F5FD1A1DBC2392B3783ED1E7D91B62942706E5584BA0B92");

        const auto keypair = my_elliptic_curve.make_keypair(&seed_keygen);

        using local_point_type = typename elliptic_curve_type::point_type;

        const bool result_is_on_curve_is_ok{
            my_elliptic_curve.is_on_curve(local_point_type{std::get<1>(keypair).first, std::get<1>(keypair).second})};

        const auto result_private_is_ok =
            (std::get<0>(keypair) == ckormanyos::big_int::example::detail::from_chars_16(
                                         "C6455BF2F380F6B81F5FD1A1DBC2392B3783ED1E7D91B62942706E5584BA0B92"));
        const auto result_public_x_is_ok =
            (std::get<1>(keypair).first == ckormanyos::big_int::example::detail::from_chars_16(
                                               "C6235629F157690E1DF37248256C4FB7EFF073D0250F5BD85DF40B9E127A8461"));
        const auto result_public_y_is_ok =
            (std::get<1>(keypair).second == ckormanyos::big_int::example::detail::from_chars_16(
                                                "CBAA679F07F9B98F915C1FB7D85A379D0559A9EEE6735B1BE0CE0E2E2B2E94DE"));

        const auto result_keygen_is_ok = (result_private_is_ok && result_public_x_is_ok && result_public_y_is_ok);

        result_is_ok = (result_is_on_curve_is_ok && result_keygen_is_ok && result_is_ok);

        const big_sint_type priv = ckormanyos::big_int::example::detail::from_chars_16(
            "6F73D8E95D6DDBF0EB352A9F0B2CE91931511EDAF9AC8F128D5A4F877C4F0450");

        const std::pair<big_sint_type, big_sint_type> sig{
            my_elliptic_curve.sign_message(std::get<0>(keypair), msg_as_string.cbegin(), msg_as_string.cend(), &priv)};

        const bool result_sig_is_ok = {
            (sig == std::make_pair(ckormanyos::big_int::example::detail::from_chars_16(
                                       "65717A860F315A21E6E23CDE411C8940DE42A69D8AB26C2465902BE8F3B75E7B"),
                                   ckormanyos::big_int::example::detail::from_chars_16(
                                       "DB8B8E75A7B0C2F0D9EB8DBF1B5236EDEB89B2116F5AEBD40E770F8CCC3D6605")))};

        result_is_ok = (result_sig_is_ok && result_is_ok);

        const auto result_verify_is_ok = my_elliptic_curve.verify_signature(
            std::get<1>(keypair), msg_as_string.cbegin(), msg_as_string.cend(), sig);

        result_is_ok = (result_verify_is_ok && result_is_ok);

        {
            std::stringstream strm{};

            strm << "result predef01: result_is_ok: " << std::boolalpha << result_is_ok;

            std::cout << strm.str() << std::endl;
        }
    }

    {
        // We will now test a sequence of multiple successful keygen, sign, verify sequences.

        for (auto count = static_cast<unsigned>(UINT8_C(0)); count < static_cast<unsigned>(UINT8_C(10)); ++count) {
            const auto keypair = my_elliptic_curve.make_keypair();

            using local_distribution_type = std::uniform_int_distribution<std::uint64_t>;

            using local_random_engine_type = std::mt19937_64;
            using local_random_device_type = std::random_device;

            local_random_device_type dev{};

            const auto seed_value = static_cast<typename local_random_engine_type::result_type>(dev());

            local_random_engine_type generator(seed_value);

            local_distribution_type dist{std::uint64_t{UINT64_C(0x1000000000000001)},
                                         std::uint64_t{UINT64_C(0xFFFFFFFFFFFFFFFF)}};

            const auto msg_str_append_index = msg_as_string + std::to_string(dist(generator));

            const auto sig = my_elliptic_curve.sign_message(
                std::get<0>(keypair), msg_str_append_index.cbegin(), msg_str_append_index.cend());

            const auto result_verify_is_ok = my_elliptic_curve.verify_signature(
                std::get<1>(keypair), msg_str_append_index.cbegin(), msg_str_append_index.cend(), sig);

            result_is_ok = (result_verify_is_ok && result_is_ok);

            {
                std::stringstream strm{};

                strm << "result random" << std::setw(2) << std::setfill('0') << std::right << (count + 1)
                     << ": result_is_ok: " << std::boolalpha << result_is_ok;

                std::cout << strm.str() << std::endl;
            }
        }
    }

    {
        // We will now test keygen, sign, and a (purposely failing!) verification.
        // Here, the message being verified has been artificially modified and
        // signature verification is intended to fail and does fail, as expected.

        const auto keypair{my_elliptic_curve.make_keypair()};

        const std::pair<big_sint_type, big_sint_type> sig{
            my_elliptic_curve.sign_message(std::get<0>(keypair), msg_as_string.cbegin(), msg_as_string.cend())};

        const auto msg_str_to_fail = msg_as_string + "x";

        const auto result_verify_expected_fail_is_ok = (!my_elliptic_curve.verify_signature(
            std::get<1>(keypair), msg_str_to_fail.cbegin(), msg_str_to_fail.cend(), sig));

        result_is_ok = (result_verify_expected_fail_is_ok && result_is_ok);
    }

    return result_is_ok;
}

auto main() -> int;

auto main() -> int {
    using local_stopwatch_type = ckormanyos::big_int::example::local::concurrency::stopwatch<>;

    local_stopwatch_type my_stopwatch{};

    const bool result_is_ok{ckormanyos::big_int::example::ecdsa_sign_verify()};

    const float elapsed{local_stopwatch_type::elapsed_time<float>(my_stopwatch)};

    const bool result_stopwatch_is_ok{(elapsed > 0.02F) && (elapsed < 200.F)};

    {
        std::stringstream strm{};

        strm << "result total___: result_is_ok: " << std::boolalpha << result_is_ok;

        std::cout << strm.str() << std::endl;
    }

    {
        std::stringstream strm{};

        strm << "stopwatch time: " << std::fixed << std::setprecision(1) << elapsed << "s";

        std::cout << strm.str() << std::endl;
    }

    return ((result_is_ok && result_stopwatch_is_ok) ? 0 : -1);
}

#if defined(ELLIPTIC_CPP_INT_USE_STD_BIG_INT)
BEMAN_BIG_INT_DIAGNOSTIC_POP()
#endif
