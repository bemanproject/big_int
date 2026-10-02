// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SIMD_VEC_AVX2_HPP
#define BEMAN_BIG_INT_SIMD_VEC_AVX2_HPP

#include <cstddef>

#include <immintrin.h>

#include <beman/big_int/detail/config.hpp>

// The x86-64 AVX2 + FMA (width-4) vector type. __m256d + _mm256_fmsub_pd /
// _mm256_fnmadd_pd (single-rounded FMA) and _mm256_round_pd (round to nearest,
// ties to even) produce results bit-identical to vec1d. This header is compiled
// ONLY into src/ntt_fp_avx2.cpp (built with -mavx2 -mfma); the dispatcher selects
// it at runtime only after confirming AVX2+FMA support, so its instructions never
// execute on a CPU that lacks them.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

struct vec4d {
    inline static constexpr std::size_t width = 4;
    __m256d                             v;

    [[nodiscard]] static vec4d loadu(const double* const p) noexcept { return vec4d{_mm256_loadu_pd(p)}; }
    void                       storeu(double* const p) const noexcept { _mm256_storeu_pd(p, v); }
    [[nodiscard]] static vec4d splat(const double x) noexcept { return vec4d{_mm256_set1_pd(x)}; }
};

[[nodiscard]] inline vec4d operator+(const vec4d a, const vec4d b) noexcept { return vec4d{_mm256_add_pd(a.v, b.v)}; }
[[nodiscard]] inline vec4d operator-(const vec4d a, const vec4d b) noexcept { return vec4d{_mm256_sub_pd(a.v, b.v)}; }
[[nodiscard]] inline vec4d vmul(const vec4d a, const vec4d b) noexcept { return vec4d{_mm256_mul_pd(a.v, b.v)}; }
// a*b - c, single-rounded.
[[nodiscard]] inline vec4d vfmsub(const vec4d a, const vec4d b, const vec4d c) noexcept {
    return vec4d{_mm256_fmsub_pd(a.v, b.v, c.v)};
}
// c - a*b, single-rounded.
[[nodiscard]] inline vec4d vfnmadd(const vec4d a, const vec4d b, const vec4d c) noexcept {
    return vec4d{_mm256_fnmadd_pd(a.v, b.v, c.v)};
}
[[nodiscard]] inline vec4d vround(const vec4d a) noexcept {
    return vec4d{_mm256_round_pd(a.v, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)};
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_SIMD_VEC_AVX2_HPP
