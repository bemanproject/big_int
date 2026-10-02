// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_NTT_FP_HPP
#define BEMAN_BIG_INT_NTT_FP_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>
#include <beman/big_int/detail/ntt.hpp> // for ntt_direction

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Double-precision floating-point NTT, the SIMD-friendly transform (FLINT
// fft_small / van der Hoeven). Residues are doubles in a CENTERED representation;
// the exact modular multiply is fp_mulmod, provably exact for primes p < 2^50
// (the 53-bit mantissa makes h + l == a*b exact via a single-rounded FMA, and the
// quotient estimate is bounded). The butterfly is written ONCE as a template over
// a vector type V (scalar / NEON / AVX2); a runtime dispatcher selects the kernel.
// Round-to-nearest mode is assumed, and the FMAs must not be contracted -- the TUs
// that compile this are built with -ffp-contract=off / /fp:strict.

// Architectures with a hand-written SIMD kernel.
// NEON is mandatory baseline on AArch64 (no dispatch);
// x86-64 selects AVX2 at runtime, else the scalar kernel.
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    #define BEMAN_BIG_INT_NTT_FP_X86 1
#else
    #define BEMAN_BIG_INT_NTT_FP_X86 0
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    #define BEMAN_BIG_INT_NTT_FP_ARM64 1
#else
    #define BEMAN_BIG_INT_NTT_FP_ARM64 0
#endif

// An NTT prime for the FP transform: the integer modulus (used only for setup --
// roots, inverses, CRT) plus the double constants the transform needs.
struct ntt_fp_modulus {
    ntt_modulus mod;  // integer Montgomery modulus (50-bit prime) for setup/CRT
    double      n;    // (double)p, exact since p < 2^53
    double      ninv; // 1.0 / n (round-to-nearest)

    [[nodiscard]] static constexpr ntt_fp_modulus
    make(const std::uint64_t p, const std::uint64_t g, const std::uint64_t adicity) noexcept {
        return ntt_fp_modulus{ntt_modulus::make(p, g, adicity), static_cast<double>(p), 1.0 / static_cast<double>(p)};
    }
};

// Three NTT-friendly primes p = c*2^k + 1 with 2^49 < p < 2^50, large 2-adicity,
// and passing FLINT's fft_small_mulmod_satisfies_bounds (verified). The largest
// is FLINT's own default. Product ~2^149 bounds every convolution coefficient
// (< 2^144) at b <= 50; the smallest 2-adicity (42) caps the transform length at
// 2^42. Stored in ASCENDING order (p0 < p1 < p2) so the 3-prime Garner CRT in
// src/fft_mul.cpp needs no extra residue reductions.
inline constexpr ntt_fp_modulus ntt_fp_primes[3] = {
    ntt_fp_modulus::make(659706976665601ull, 11ull, 43ull),  // 75 * 2^43 + 1
    ntt_fp_modulus::make(699289395265537ull, 5ull, 42ull),   // 159 * 2^42 + 1
    ntt_fp_modulus::make(1108307720798209ull, 11ull, 44ull), // 63 * 2^44 + 1 (FLINT default)
};

// Exact a*b mod n for n < 2^50, with a, b, and the result in a centered range
// (output in (-n, n) when |a*b| < 2*n^2). The only FMAs MUST be these explicit
// ones -- see the -ffp-contract=off requirement on the TUs that use this.
[[nodiscard]] inline double fp_mulmod(const double a, const double b, const double n, const double ninv) noexcept {
    const double h = a * b;               // rounded product
    const double q = std::rint(h * ninv); // quotient estimate, round-to-nearest
    const double l = std::fma(a, b, -h);  // exact low part: h + l == a*b
    return std::fma(-q, n, h) + l;        // (h - q*n) + l
}

// Reduce a into the centered range [-n, n] (a representative of a mod n).
[[nodiscard]] inline double fp_reduce_to_pm1n(const double a, const double n, const double ninv) noexcept {
    return std::fma(-std::rint(a * ninv), n, a);
}

// Reduce a into the canonical [0, n) (used for the final read-back to integers).
[[nodiscard]] inline double fp_reduce_to_0n(const double a, const double n, const double ninv) noexcept {
    const double r = fp_reduce_to_pm1n(a, n, ninv);
    return r < 0.0 ? r + n : r;
}

// Center an integer residue r in [0, p) to [-p/2, p/2] as an exact double.
[[nodiscard]] inline double fp_center(const std::uint64_t r, const std::uint64_t p) noexcept {
    return r <= p / 2 ? static_cast<double>(r) : static_cast<double>(r) - static_cast<double>(p);
}

// Exact modular multiply / reduction over a vector type V (which supplies vmul,
// vfmsub, vfnmadd, vround, operator+/-, found by ADL). Identical formula to
// fp_mulmod / fp_reduce_to_pm1n, lane for lane.
template <class V>
[[nodiscard]] V vmulmod(const V a, const V b, const V n, const V ninv) noexcept {
    const V h = vmul(a, b);
    const V q = vround(vmul(h, ninv));
    const V l = vfmsub(a, b, h);
    return vfnmadd(q, n, h) + l;
}

template <class V>
[[nodiscard]] V vreduce_to_pm1n(const V a, const V n, const V ninv) noexcept {
    return vfnmadd(vround(vmul(a, ninv)), n, a);
}

// Forward transform (Gentleman-Sande DIF) over vector width V::width. The twiddle
// table is per-level contiguous (built by ntt_fp_build_twiddles), so each level's
// twiddles load as full vectors. Levels with half >= width vectorize; the last
// log2(width) levels (half < width) run scalar. Centered residues stay in [-n, n].
template <class V>
void ntt_fp_forward_impl(double* const         data,
                         const std::size_t     n,
                         const double* const   tw,
                         const ntt_fp_modulus& m) noexcept {
    const V      vn     = V::splat(m.n);
    const V      vninv  = V::splat(m.ninv);
    const double sn     = m.n;
    const double sninv  = m.ninv;
    std::size_t  tw_off = 0;
    for (std::size_t len = n; len > 1; len >>= 1) {
        const std::size_t   half = len >> 1;
        const double* const tlev = tw + tw_off;
        if (half >= V::width) {
            for (std::size_t start = 0; start < n; start += len) {
                double* const lo = data + start;
                double* const hi = data + start + half;
                for (std::size_t j = 0; j < half; j += V::width) {
                    const V u = V::loadu(lo + j);
                    const V v = V::loadu(hi + j);
                    const V w = V::loadu(tlev + j);
                    vreduce_to_pm1n(u + v, vn, vninv).storeu(lo + j);
                    vmulmod(u - v, w, vn, vninv).storeu(hi + j);
                }
            }
        } else {
            for (std::size_t start = 0; start < n; start += len) {
                for (std::size_t j = 0; j < half; ++j) {
                    const double u         = data[start + j];
                    const double v         = data[start + j + half];
                    data[start + j]        = fp_reduce_to_pm1n(u + v, sn, sninv);
                    data[start + j + half] = fp_mulmod(u - v, tlev[j], sn, sninv);
                }
            }
        }
        tw_off += half;
    }
}

// Inverse transform (Cooley-Tukey DIT) plus the 1/n scaling. Consumes the
// bit-reversed layout from ntt_fp_forward_impl; the inverse twiddle table.
template <class V>
void ntt_fp_inverse_impl(double* const         data,
                         const std::size_t     n,
                         const double* const   tw,
                         const ntt_fp_modulus& m) noexcept {
    const V      vn     = V::splat(m.n);
    const V      vninv  = V::splat(m.ninv);
    const double sn     = m.n;
    const double sninv  = m.ninv;
    std::size_t  tw_off = 0;
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t   half = len >> 1;
        const double* const tlev = tw + tw_off;
        if (half >= V::width) {
            for (std::size_t start = 0; start < n; start += len) {
                double* const lo = data + start;
                double* const hi = data + start + half;
                for (std::size_t j = 0; j < half; j += V::width) {
                    const V u = V::loadu(lo + j);
                    const V v = vmulmod(V::loadu(hi + j), V::loadu(tlev + j), vn, vninv);
                    vreduce_to_pm1n(u + v, vn, vninv).storeu(lo + j);
                    vreduce_to_pm1n(u - v, vn, vninv).storeu(hi + j);
                }
            }
        } else {
            for (std::size_t start = 0; start < n; start += len) {
                for (std::size_t j = 0; j < half; ++j) {
                    const double u         = data[start + j];
                    const double v         = fp_mulmod(data[start + j + half], tlev[j], sn, sninv);
                    data[start + j]        = fp_reduce_to_pm1n(u + v, sn, sninv);
                    data[start + j + half] = fp_reduce_to_pm1n(u - v, sn, sninv);
                }
            }
        }
        tw_off += half;
    }

    const std::uint64_t n_inv   = m.mod.inv(static_cast<std::uint64_t>(n) % m.mod.p);
    const double        n_inv_c = fp_center(n_inv, m.mod.p);
    const V             vnc     = V::splat(n_inv_c);
    std::size_t         i       = 0;
    for (; i + V::width <= n; i += V::width) {
        vmulmod(V::loadu(data + i), vnc, vn, vninv).storeu(data + i);
    }
    for (; i < n; ++i) {
        data[i] = fp_mulmod(data[i], n_inv_c, sn, sninv);
    }
}

// Elementwise a[i] <- a[i] * b[i] mod p (centered); a and b may alias.
template <class V>
void ntt_fp_pointwise_impl(double* const         a,
                           const double* const   b,
                           const std::size_t     n,
                           const ntt_fp_modulus& m) noexcept {
    const V      vn    = V::splat(m.n);
    const V      vninv = V::splat(m.ninv);
    const double sn    = m.n;
    const double sninv = m.ninv;
    std::size_t  i     = 0;
    for (; i + V::width <= n; i += V::width) {
        vmulmod(V::loadu(a + i), V::loadu(b + i), vn, vninv).storeu(a + i);
    }
    for (; i < n; ++i) {
        a[i] = fp_mulmod(a[i], b[i], sn, sninv);
    }
}

// Per-ISA kernels (raw pointer + length, the dispatch signatures). Defined in the
// matching src/ntt_fp_*.cpp, each instantiating the templates above with its
// vector type.
using ntt_fp_transform_fn = void (*)(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
using ntt_fp_pointwise_fn = void (*)(double*, const double*, std::size_t, const ntt_fp_modulus&) noexcept;

void ntt_fp_forward_scalar(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
void ntt_fp_inverse_scalar(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
void ntt_fp_pointwise_scalar(double*, const double*, std::size_t, const ntt_fp_modulus&) noexcept;
#if BEMAN_BIG_INT_NTT_FP_ARM64
void ntt_fp_forward_neon(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
void ntt_fp_inverse_neon(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
void ntt_fp_pointwise_neon(double*, const double*, std::size_t, const ntt_fp_modulus&) noexcept;
#endif
#if BEMAN_BIG_INT_NTT_FP_X86
void ntt_fp_forward_avx2(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
void ntt_fp_inverse_avx2(double*, std::size_t, const double*, const ntt_fp_modulus&) noexcept;
void ntt_fp_pointwise_avx2(double*, const double*, std::size_t, const ntt_fp_modulus&) noexcept;
#endif

// The selected kernel set (forward / inverse / pointwise), chosen once by
// ntt_fp_dispatch() from the running CPU's features and cached.
struct ntt_fp_kernels {
    ntt_fp_transform_fn forward;
    ntt_fp_transform_fn inverse;
    ntt_fp_pointwise_fn pointwise;
};

[[nodiscard]] const ntt_fp_kernels& ntt_fp_dispatch() noexcept;

// Fill `tw[0 .. n-1)` with the per-level centered-double twiddle tables (level
// lengths n, n/2, ..., 2 for forward; 2, ..., n for inverse), each level's powers
// laid out contiguously so the transform loads them as vectors. Powers are
// computed exactly with integer arithmetic, then centered to [-p/2, p/2].
void ntt_fp_build_twiddles(std::span<double>     tw,
                           std::size_t           n,
                           const ntt_fp_modulus& m,
                           ntt_direction         direction) noexcept;

// Public, dispatched entry points used by src/fft_mul.cpp.
inline void
ntt_fp_forward(const std::span<double> data, const std::span<const double> tw, const ntt_fp_modulus& m) noexcept {
    ntt_fp_dispatch().forward(data.data(), data.size(), tw.data(), m);
}

inline void
ntt_fp_inverse(const std::span<double> data, const std::span<const double> tw, const ntt_fp_modulus& m) noexcept {
    ntt_fp_dispatch().inverse(data.data(), data.size(), tw.data(), m);
}

inline void
ntt_fp_pointwise(const std::span<double> a, const std::span<const double> b, const ntt_fp_modulus& m) noexcept {
    ntt_fp_dispatch().pointwise(a.data(), b.data(), a.size(), m);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_NTT_FP_HPP
