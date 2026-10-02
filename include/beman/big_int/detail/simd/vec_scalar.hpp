// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SIMD_VEC_SCALAR_HPP
#define BEMAN_BIG_INT_SIMD_VEC_SCALAR_HPP

#include <cmath>
#include <cstddef>

#include <beman/big_int/detail/config.hpp>

// The scalar (width-1) vector type: the portable fallback and the executable
// specification the SIMD kernels must match bit for bit. std::fma / std::rint are
// correctly rounded and bit-exact even without hardware FMA.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

struct vec1d {
    inline static constexpr std::size_t width = 1;
    double                              v;

    [[nodiscard]] static vec1d loadu(const double* const p) noexcept { return vec1d{*p}; }
    void                       storeu(double* const p) const noexcept { *p = v; }
    [[nodiscard]] static vec1d splat(const double x) noexcept { return vec1d{x}; }
};

[[nodiscard]] inline vec1d operator+(const vec1d a, const vec1d b) noexcept { return vec1d{a.v + b.v}; }
[[nodiscard]] inline vec1d operator-(const vec1d a, const vec1d b) noexcept { return vec1d{a.v - b.v}; }
[[nodiscard]] inline vec1d vmul(const vec1d a, const vec1d b) noexcept { return vec1d{a.v * b.v}; }
// a*b - c, single-rounded.
[[nodiscard]] inline vec1d vfmsub(const vec1d a, const vec1d b, const vec1d c) noexcept {
    return vec1d{std::fma(a.v, b.v, -c.v)};
}
// c - a*b, single-rounded.
[[nodiscard]] inline vec1d vfnmadd(const vec1d a, const vec1d b, const vec1d c) noexcept {
    return vec1d{std::fma(-a.v, b.v, c.v)};
}
[[nodiscard]] inline vec1d vround(const vec1d a) noexcept { return vec1d{std::rint(a.v)}; }

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_SIMD_VEC_SCALAR_HPP
