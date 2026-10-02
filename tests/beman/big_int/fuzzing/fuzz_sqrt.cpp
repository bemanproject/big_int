// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "fuzz_common.hpp"

#include <cstddef>
#include <cstdint>

template <class T = void>
struct sqrt_exec {
    [[nodiscard]] T operator()(const T& my_arg) const { return sqrt(my_arg); }
};

template <>
struct sqrt_exec<void> {
    template <typename T>
    [[nodiscard]] T operator()(const T& my_arg) const {
        return sqrt(my_arg);
    }
};

BEMAN_BIG_INT_BEGIN_NAMESPACE

namespace detail {

template <class Limb>
static auto msb_limb(Limb x) -> int {
    if (x == Limb{0}) {
        return -1;
    }

    constexpr Limb limb_mask{Limb{1} << (std::numeric_limits<Limb>::digits - 1)};

    int bpos{};

    while ((x & limb_mask) == Limb{0}) {
        ++bpos;
        x <<= 1;
    }

    return (std::numeric_limits<Limb>::digits - 1) - bpos;
}

static auto msb(const big_int& m) -> int {

    const auto hi_limb{m.representation().back()};

    int bpos{msb_limb(hi_limb)};

    bpos += static_cast<int>(m.representation().size() - 1U) *
            std::numeric_limits<BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>::digits;

    return bpos;
}

} // namespace detail

static auto sqrt(big_int m) -> big_int {
    // Calculate the square root.

    big_int s{};

    if (m <= 0) {
        s = 0;
    } else {
        // Obtain the initial guess via algorithms
        // involving the position of the msb.
        const auto msb_pos = detail::msb(m);

        const auto msb_pos_mod_2 = msb_pos % 2;

        // Obtain the initial value.
        const auto left_shift_amount = 1 + ((msb_pos_mod_2 == 0) ? msb_pos / 2 : (msb_pos + 1) / 2);

        big_int u{big_int{1} << left_shift_amount};

        // Perform the iteration for the square root.
        // See Algorithm 1.13 SqrtInt, Sect. 1.5.1
        // in R.P. Brent and Paul Zimmermann, "Modern Computer Arithmetic",
        // Cambridge University Press, 2011.

        for (auto i{0}; i < 64; ++i) {
            static_cast<void>(i);

            s = u;

            u = (s + (m / s)) / 2;

            if (u >= s) {
                break;
            }
        }
    }

    return s;
}

BEMAN_BIG_INT_END_NAMESPACE

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return ::BEMAN_BIG_INT_NAMESPACE::fuzz::run_unary(sqrt_exec<>{}, data, size, false);
}
