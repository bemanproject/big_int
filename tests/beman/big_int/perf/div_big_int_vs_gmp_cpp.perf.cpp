// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>
#include <boost/multiprecision/gmp.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace local {

namespace detail {

namespace bmp {

namespace detail {

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

} // namespace detail

[[nodiscard]] inline std::string random_big_int(const std::size_t bits, const bool negative = false) {
    static std::mt19937_64 rng{std::random_device{}()};

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

} // namespace bmp

using str_pair_type = std::pair<std::string, std::string>;

using random_engine_length_type = std::minstd_rand;

auto get_hex_string_pair(const unsigned len_in_bits) -> std::pair<std::string, std::string>;

auto get_hex_string_pair(const unsigned len_in_bits) -> std::pair<std::string, std::string> {
    const std::string str_a{bmp::random_big_int(len_in_bits)};
    const std::string str_b{bmp::random_big_int(len_in_bits / 2)};

    return {str_a, str_b};
}

} // namespace detail

using BEMAN_BIG_INT_NAMESPACE::big_int;
using gmp_int = boost::multiprecision::number<boost::multiprecision::gmp_int, boost::multiprecision::et_off>;
using cpp_int = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;

auto to_hex_string_bn(big_int value_to_convert) -> std::string {
    // Calculate the hex-expected string length and also align to 16.
    const std::size_t buf_size{(((value_to_convert.size() + 4U) / 4U) / 16U + 1U) * 16U};

    std::string result(buf_size, '\0');

    // big_int supports to_chars, found in this example by ADL.
    const auto [ptr, ec]{to_chars(result.data(), result.data() + result.size(), value_to_convert, 16)};

    static_cast<void>(ec);

    result.resize(static_cast<std::string::size_type>(std::distance(result.data(), ptr)));

    return result;
}

} // namespace local

auto main(int argc, char** argv) -> int {
    auto result_total_is_ok = true;

    constexpr unsigned limb_bits{
        static_cast<unsigned>(std::numeric_limits<::BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits)};

    // argv[1] = operand width in limbs, argv[2] = trial count (both optional). The
    // defaults reproduce the original single 512-limb, 0x4000-trial run; passing a
    // size sweeps the big_int / cpp_int / gmp_int comparison table.
    const unsigned      limbs{(argc > 1) ? static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10)) : 512U};
    const std::uint32_t max_trial{(argc > 2) ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10))
                                             : static_cast<std::uint32_t>(UINT32_C(0x4000))};
    auto                trial = static_cast<std::uint32_t>(UINT32_C(0));

    std::uint64_t elapsed_total_ops_bn{};
    std::uint64_t elapsed_total_ops_gm{};
    std::uint64_t elapsed_total_ops_cp{};

    const unsigned length_in_bits{limbs * limb_bits};

    for (; ((trial < max_trial) && result_total_is_ok); ++trial) {
        const local::detail::str_pair_type str_pair{local::detail::get_hex_string_pair(length_in_bits)};

        // Make commands like the following:

        local::big_int bn_a{};
        local::big_int bn_b{};

        const auto fc_result_a{
            from_chars(str_pair.first.data(), str_pair.first.data() + str_pair.first.size(), bn_a, 16)};
        const auto fc_result_b{
            from_chars(str_pair.second.data(), str_pair.second.data() + str_pair.second.size(), bn_b, 16)};

        static_cast<void>(fc_result_a);
        static_cast<void>(fc_result_b);

        local::gmp_int gm_a{"0x" + str_pair.first};
        local::gmp_int gm_b{"0x" + str_pair.second};

        local::cpp_int cp_a{"0x" + str_pair.first};
        local::cpp_int cp_b{"0x" + str_pair.second};

        {
            const auto start{std::chrono::high_resolution_clock::now()};

            const local::big_int mul_result = bn_a / bn_b;

            const auto stop{std::chrono::high_resolution_clock::now()};

            const auto elapsed_one_op{std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()};

            elapsed_total_ops_bn = elapsed_total_ops_bn + static_cast<std::uint64_t>(elapsed_one_op);
        }

        {
            const auto start{std::chrono::high_resolution_clock::now()};

            const local::cpp_int mul_result = cp_a / cp_b;

            const auto stop{std::chrono::high_resolution_clock::now()};

            const auto elapsed_one_op{std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()};

            elapsed_total_ops_cp = elapsed_total_ops_cp + static_cast<std::uint64_t>(elapsed_one_op);
        }

        {
            const auto start{std::chrono::high_resolution_clock::now()};

            const local::gmp_int mul_result = gm_a / gm_b;

            const auto stop{std::chrono::high_resolution_clock::now()};

            const auto elapsed_one_op{std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()};

            elapsed_total_ops_gm = elapsed_total_ops_gm + static_cast<std::uint64_t>(elapsed_one_op);
        }

        {
            if ((trial > 0U) && ((trial % 32U) == UINT32_C(0))) {
                const double average_op_time_us_bn =
                    (static_cast<double>(elapsed_total_ops_bn) / static_cast<double>(trial)) / 1000.0;

                {
                    std::stringstream strm{};

                    strm << "trial: " << trial << ", average_op_time_us_bn: " << std::setprecision(1) << std::fixed
                         << average_op_time_us_bn;

                    std::cout << strm.str() << std::endl;
                }

                const double average_op_time_us_cp =
                    (static_cast<double>(elapsed_total_ops_cp) / static_cast<double>(trial)) / 1000.0;

                {
                    std::stringstream strm{};

                    strm << "trial: " << trial << ", average_op_time_us_cp: " << std::setprecision(1) << std::fixed
                         << average_op_time_us_cp;

                    std::cout << strm.str() << std::endl;
                }

                const double average_op_time_us_gm =
                    (static_cast<double>(elapsed_total_ops_gm) / static_cast<double>(trial)) / 1000.0;

                {
                    std::stringstream strm{};

                    strm << "trial: " << trial << ", average_op_time_us_gm: " << std::setprecision(1) << std::fixed
                         << average_op_time_us_gm;

                    std::cout << strm.str() << std::endl;
                }
            }
        }
    }

    result_total_is_ok = ((trial == max_trial) && result_total_is_ok);

    {
        const double avg_bn = (trial != 0U ? static_cast<double>(elapsed_total_ops_bn) / trial : 0.0) / 1000.0;
        const double avg_gm = (trial != 0U ? static_cast<double>(elapsed_total_ops_gm) / trial : 0.0) / 1000.0;
        const double avg_cp = (trial != 0U ? static_cast<double>(elapsed_total_ops_cp) / trial : 0.0) / 1000.0;

        std::stringstream strm;

        strm << '\n';
        strm << "Summary                            : " << trial << " trials, " << limbs << " limbs" << '\n';
        strm << "result_total_is_ok                 : " << std::boolalpha << result_total_is_ok << '\n';
        strm << std::fixed << std::setprecision(3);
        strm << "us per op big_int / cpp_int / gmp : " << avg_bn << " / " << avg_cp << " / " << avg_gm << '\n';

        std::cout << strm.str() << std::endl;
    }

    return (result_total_is_ok ? 0 : -1);
}
