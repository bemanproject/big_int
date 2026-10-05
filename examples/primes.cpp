// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

// #define BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY

namespace rnd_gens {
using gen_type = std::mt19937_64;

auto eng1() -> gen_type& {
    static gen_type instance{};

    return instance;
};

auto eng2() -> gen_type& {
    static gen_type instance{};

    return instance;
};

} // namespace rnd_gens

template <class BigIntType, class RndEngineType>
[[nodiscard]] auto get_pseudo_random_integer(RndEngineType& eng, const BigIntType& max_val) -> BigIntType {
    BigIntType value_to_get{};

    for (int bit_index{0}; value_to_get < max_val; bit_index += 64) {
        if (bit_index != 0U) {
            value_to_get <<= 64U;
        }

        // Get the next 64-bit chunk. Also ensure each chunk retrieved
        // is non-zero and also that the resulting candidate will, after
        // piecing the chunks together, have the full bit width.
        value_to_get += (eng() | std::uint64_t{0x8000000000000000});
    }

    return value_to_get % max_val;
}

template <class BigIntType>
auto powm(BigIntType b, BigIntType p, const BigIntType& m) -> BigIntType {

    // Calculate (b ^ p) % m.

    BigIntType x{1};

    unsigned p0{};

    while (((p0 = static_cast<unsigned>(p)) != 0U) || (p != 0U)) {
        if ((p0 & 1U) != 0U) {
            x *= b;
            x %= m;
        }

        b *= b;
        b %= m;

        p >>= 1U;
    }

    return x;
}

constexpr auto lsb_position(std::uint64_t x) noexcept -> unsigned {
    // We use tricky, enhanced knowledge here. Because of the way the prime
    // candidates are created, each 64-bit chunk is "rigged" to have a non-zero
    // value. So here, we can safely calculate the LSB of the entire big_int
    // based on the single lowest chunk.

    unsigned pos{};

    while ((static_cast<unsigned>(x) & 1U) == 0U) {
        x >>= 1U;
        ++pos;
    }

    return pos;
}

template <class BigIntType>
auto miller_rabin(const BigIntType& np, const int trials) -> bool {
    // Perform the Miller-Rabin primality test on the prime candidate np.
    // This subroutine returns true if the prime candidate is prime within
    // the limits of Miller-Rabin testing for the given input of trials.

    // Table[Prime[i], {i, 2, 49, 1}]
    constexpr std::array<unsigned, 48> small_primes = {
        3U,   5U,   7U,   11U,  13U,  17U,  19U,  23U,  29U,  31U,  37U,  41U,  43U,  47U,  53U,  59U,
        61U,  67U,  71U,  73U,  79U,  83U,  89U,  97U,  101U, 103U, 107U, 109U, 113U, 127U, 131U, 137U,
        139U, 149U, 151U, 157U, 163U, 167U, 173U, 179U, 181U, 191U, 193U, 197U, 199U, 211U, 223U, 227U};

    {
        // Handle even numbers.
        const auto n0{static_cast<unsigned>(np)};

        const bool n_is_even{(n0 & 1U) == 0U};

        if (n_is_even) {
            // If true:
            // Handle the trivial special case of 2, which is prime.

            // If false:
            // The prime candidate is not prime because it is either
            // even and larger than 2 or equal to zero. Herewith, we
            // handle non-prime even numbers and the non-primality of 0.
            const bool is_prime_two_or_is_non_prime_even{(n0 == 2U) && (np == 2U)};

            return is_prime_two_or_is_non_prime_even;
        }

        if ((n0 <= small_primes.back()) && (np <= small_primes.back())) {
            // This handles the trivial special case of the (non-primality) of 1.
            if (n0 == 1U) {
                return false;
            }

            // Exclude pure small primes from the small_primes table.
            // We are already restricted to np <= small_primes.back()
            // via the query above. So it is sufficient to test only
            // the lowest unsigned-casted value, n0.
            const bool is_small_prime{std::ranges::contains(small_primes, n0)};

            if (is_small_prime) {
                return true;
            }
        }

        // Handle numbers divisible by small primes in the small_primes table.
        const bool is_small_prime_divisible{
            std::ranges::any_of(small_primes, [&np](unsigned p) { return (np % p) == 0U; })};

        if (is_small_prime_divisible) {
            return false;
        }
    }

    const BigIntType nm1{np - 1U};

    auto local_functor_isone{[](const BigIntType& t1) { return ((static_cast<unsigned>(t1) == 1U) && (t1 == 1U)); }};

    {
        // Perform a single Fermat test which will exclude many non-prime candidates.
        // If this fails, np is definitely composite. If it passes, np might still
        // be composite (Carmichael numbers are the classic troublemakers).
        // But this simple test weeds out many non-prime candidates. The value
        // 228 (which is small_primes.back + 1) is not a correctness requirement.
        // Rather, it is just a performance tradeoff in this interpretation
        // of Miller-Rabin primality testing.

        const BigIntType fn{powm(BigIntType{small_primes.back() + 1U}, nm1, np)};

        if (!local_functor_isone(fn)) {
            return false;
        }
    }

    const unsigned k{lsb_position(static_cast<std::uint64_t>(nm1))};

    const BigIntType q{nm1 >> k};

    // Assume the test will pass, even though it usually does not pass.
    bool result_candidate_is_prime{true};

    const BigIntType nm2{np - 2};

    // We will now run the trials.
    for (int trial{0}; ((trial < trials) && result_candidate_is_prime); ++trial) {
        static_cast<void>(trial);

        BigIntType next_rnd{get_pseudo_random_integer(rnd_gens::eng1(), nm2)};

        BigIntType y{powm(next_rnd, q, np)};

        for (auto j{0U}; ((j < k) && result_candidate_is_prime); ++j) {
            if (y == nm1) {
                // This trial passes and the candidate is very probably prime
                // within the limits of Miller-Rabin primality testing.

                break;
            }

            if (local_functor_isone(y)) {
                // Failure and the candidate is not prime, but only if this is
                // not the first step.

                if (j != 0U) {
                    result_candidate_is_prime = false;
                }

                break;
            }

            // Compute y = y^2 mod np.
            y = (y * y) % np;

            // If we reach the final iteration without hitting nm1,
            // then the candidate is not prime within the limits of
            // Miller-Rabin primality testing.

            if ((j + 1U) == k) {
                result_candidate_is_prime = false;
            }
        }
    }

    return result_candidate_is_prime;
}

[[nodiscard]] auto str_prime_density(std::uint64_t trial_count, std::uint64_t prime_count) -> std::string {
    std::stringstream strm{};

    strm << "prime density: 1/" << std::fixed << std::setprecision(1)
         << static_cast<float>(static_cast<double>(trial_count) / static_cast<double>(prime_count));

    return strm.str();
}

auto main() -> int {

    using beman::big_int::big_int;

    constexpr unsigned prime_candidate_bits{512U};

    const big_int max_val{(big_int{1} << prime_candidate_bits) - 1};

    std::uint64_t prime_count{};
    std::uint64_t trial_count{};

    constexpr std::uint64_t max_prime_count{32};

    big_int next_prime{};

    while (prime_count < max_prime_count) {

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY)
        static unsigned seed_prescaler{};
        if ((seed_prescaler % 128U) == 0U) {
            rnd_gens::eng1().seed(static_cast<typename rnd_gens::gen_type::result_type>(std::random_device{}()));
            rnd_gens::eng2().seed(static_cast<typename rnd_gens::gen_type::result_type>(std::random_device{}()));
        }
        ++seed_prescaler;
#endif

        const big_int prime_candidate{get_pseudo_random_integer(rnd_gens::eng2(), max_val)};
        ++trial_count;

        const bool is_prime{miller_rabin(prime_candidate, 25)};

        if (is_prime) {
            next_prime = prime_candidate;

            ++prime_count;

            std::cout << "prime" << prime_count << "/" << trial_count << ", "
                      << str_prime_density(trial_count, prime_count) << ": " << to_string(next_prime) << std::endl;
        }
    }

    std::cout << str_prime_density(trial_count, prime_count) << std::endl;

    int ret_val{};

#if !defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY)
    if constexpr ((max_prime_count == 32U) && (prime_candidate_bits == 512U)) {
        using namespace beman::big_int::literals;
        ret_val =
            (next_prime ==
             10143171719460317030776606042161455743692462665971704190268979952647685451042758582438652518606262043864611102965422197344907990728961691317186137657753121_n)
                ? 0
                : -1;
    }
#endif

    return ret_val;
}
