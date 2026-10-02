// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SIMD_VEC_NEON_HPP
#define BEMAN_BIG_INT_SIMD_VEC_NEON_HPP

#include <cstddef>

#include <arm_neon.h>

#include <beman/big_int/detail/config.hpp>

// The AArch64 NEON (width-2) vector type. float64x2_t + vfmaq/vfmsq (single-
// rounded FMA), vrndnq (round to nearest, ties to even), and the basic FP ops are
// mandatory baseline on every AArch64 CPU, so this needs no runtime dispatch and
// produces results bit-identical to vec1d.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

struct vec2d {
    inline static constexpr std::size_t width = 2;
    float64x2_t                         v;

    [[nodiscard]] static vec2d loadu(const double* const p) noexcept { return vec2d{vld1q_f64(p)}; }
    void                       storeu(double* const p) const noexcept { vst1q_f64(p, v); }
    [[nodiscard]] static vec2d splat(const double x) noexcept { return vec2d{vdupq_n_f64(x)}; }
};

[[nodiscard]] inline vec2d operator+(const vec2d a, const vec2d b) noexcept { return vec2d{vaddq_f64(a.v, b.v)}; }
[[nodiscard]] inline vec2d operator-(const vec2d a, const vec2d b) noexcept { return vec2d{vsubq_f64(a.v, b.v)}; }
[[nodiscard]] inline vec2d vmul(const vec2d a, const vec2d b) noexcept { return vec2d{vmulq_f64(a.v, b.v)}; }
// a*b - c == (-c) + a*b, via the fused multiply-accumulate vfmaq_f64(acc, x, y) = acc + x*y.
[[nodiscard]] inline vec2d vfmsub(const vec2d a, const vec2d b, const vec2d c) noexcept {
    return vec2d{vfmaq_f64(vnegq_f64(c.v), a.v, b.v)};
}
// c - a*b, via the fused multiply-subtract vfmsq_f64(acc, x, y) = acc - x*y.
[[nodiscard]] inline vec2d vfnmadd(const vec2d a, const vec2d b, const vec2d c) noexcept {
    return vec2d{vfmsq_f64(c.v, a.v, b.v)};
}
[[nodiscard]] inline vec2d vround(const vec2d a) noexcept { return vec2d{vrndnq_f64(a.v)}; }

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_SIMD_VEC_NEON_HPP
