// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/ntt_fp.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

// FFT (small-prime NTT) multiplication. Operands are split into base-2^b
// coefficients (b chosen per multiply by fft_choose_coeff_bits), convolved modulo
// three NTT primes with the double-precision transform (src/ntt_fp.cpp), the
// per-prime integer residues recombined coefficient by coefficient with a
// three-prime Garner CRT, and carry-propagated into the product. The transform
// runs in `double`; the packing, CRT, and carry recomposition run in
// std::uint64_t. b need not divide the limb width, so they use sliding bit
// buffers. The dispatcher supplies both a `double` scratch span (the transform
// buffers) and a uint64 scratch span (the per-prime residues), keeping the kernel
// independent of the library limb width.

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

namespace {

// Width of the std::uint64_t words the bit buffers and accumulator are built from.
inline constexpr unsigned word_bits = 64;

// Split `src` (limb_bits-wide limbs) into base-2^b coefficients, reduce each mod p
// and center it to [-p/2, p/2] as a double (the transform's input form), then
// zero-fill the remainder (the transform padding). Uses a 128-bit sliding bit
// buffer because b need not divide the limb width.
void fft_pack_fp(const std::span<double>                      coeffs,
                 const std::span<const uint_multiprecision_t> src,
                 const unsigned                               b,
                 const ntt_fp_modulus&                        m) noexcept {
    constexpr unsigned  limb_bits = static_cast<unsigned>(width_v<uint_multiprecision_t>);
    const std::uint64_t mask      = (std::uint64_t{1} << b) - 1;
    const double        nn        = m.n;
    const double        ninv      = m.ninv;
    std::uint64_t       buf_lo    = 0;
    std::uint64_t       buf_hi    = 0;
    unsigned            nbits     = 0; // valid bits in the buffer (always < b before a limb is added)
    std::size_t         out       = 0;
    for (const uint_multiprecision_t limb : src) {
        const std::uint64_t value = static_cast<std::uint64_t>(limb);
        if (nbits == 0) {
            buf_lo = value;
        } else {
            buf_lo |= value << nbits;
            buf_hi |= value >> (word_bits - nbits);
        }
        nbits += limb_bits;
        while (nbits >= b) {
            coeffs[out++] = fp_reduce_to_pm1n(static_cast<double>(buf_lo & mask), nn, ninv);
            buf_lo        = (buf_lo >> b) | (buf_hi << (word_bits - b));
            buf_hi >>= b;
            nbits -= b;
        }
    }
    if (nbits != 0) {
        coeffs[out++] = fp_reduce_to_pm1n(static_cast<double>(buf_lo & mask), nn, ninv);
    }
    for (; out < coeffs.size(); ++out) {
        coeffs[out] = 0.0;
    }
}

// The CRT constants for three primes p0 < p1 < p2: the two Garner inverses plus
// the 128-bit product p0*p1.
struct crt3_constants {
    std::uint64_t inv01;   // (p0 mod p1)^-1 mod p1
    std::uint64_t inv012;  // (p0*p1 mod p2)^-1 mod p2
    std::uint64_t p0p1_lo; // low 64 bits of p0*p1
    std::uint64_t p0p1_hi; // high bits of p0*p1
};

[[nodiscard]] crt3_constants crt3_make_constants() noexcept {
    const ntt_modulus&        m0   = ntt_fp_primes[0].mod;
    const ntt_modulus&        m1   = ntt_fp_primes[1].mod;
    const ntt_modulus&        m2   = ntt_fp_primes[2].mod;
    const wide<std::uint64_t> p0p1 = widening_mul(m0.p, m1.p); // p0 < p1 < p2
    return crt3_constants{
        m1.inv(m0.p % m1.p), m2.inv(m2.mul(m0.p % m2.p, m1.p % m2.p)), p0p1.low_bits, p0p1.high_bits};
}

// A non-negative 192-bit value (the CRT result is < p0*p1*p2 ~ 2^149).
struct crt3_word {
    std::uint64_t w0;
    std::uint64_t w1;
    std::uint64_t w2;
};

// Three-prime Garner CRT: the unique value in [0, p0*p1*p2) congruent to r_t mod
// p_t. With p0 < p1 < p2, r0 and the partial result are valid residues in the
// larger moduli without extra reduction.
[[nodiscard]] crt3_word
crt3(const std::uint64_t r0, const std::uint64_t r1, const std::uint64_t r2, const crt3_constants& cc) noexcept {
    const ntt_modulus&  m0 = ntt_fp_primes[0].mod;
    const ntt_modulus&  m1 = ntt_fp_primes[1].mod;
    const ntt_modulus&  m2 = ntt_fp_primes[2].mod;
    const std::uint64_t p0 = m0.p;

    const std::uint64_t       c1   = m1.mul(m1.sub(r1, r0), cc.inv01); // < p1
    const wide<std::uint64_t> p0c1 = widening_mul(p0, c1);
    const auto                x0c  = carrying_add(p0c1.low_bits, r0);
    const std::uint64_t       x0   = x0c.value; // x = r0 + p0*c1 < p0*p1, two words
    const std::uint64_t       x1   = p0c1.high_bits + std::uint64_t{x0c.carry};

    const std::uint64_t x_mod_p2 = m2.add(r0, m2.mul(p0, c1));              // (r0 + p0*c1) mod p2
    const std::uint64_t c2       = m2.mul(m2.sub(r2, x_mod_p2), cc.inv012); // < p2

    // term2 = p0*p1 * c2, a three-word value added to the two-word x.
    const wide<std::uint64_t> lo  = widening_mul(cc.p0p1_lo, c2);
    const wide<std::uint64_t> hi  = widening_mul(cc.p0p1_hi, c2);
    const auto                t1c = carrying_add(lo.high_bits, hi.low_bits);
    const std::uint64_t       u0  = lo.low_bits;
    const std::uint64_t       u1  = t1c.value;
    const std::uint64_t       u2  = hi.high_bits + std::uint64_t{t1c.carry};

    const auto s0 = carrying_add(x0, u0);
    const auto s1 = carrying_add(x1, u1, s0.carry);
    return crt3_word{s0.value, s1.value, u2 + std::uint64_t{s1.carry}};
}

// CRT-combine the three primes' residues coefficient by coefficient and
// carry-propagate result = sum_k c[k] * 2^(b*k) into `result`. A 4-word (256-bit)
// accumulator holds the active window of the product starting at the next output
// limb; each ~150-bit CRT coefficient is added at its bit offset within the
// window (always < limb_bits after flushing, so it fits the window), and whole
// limbs are flushed as the window advances. `result.size()` must be exactly the
// product length so the accumulator drains to zero.
void fft_recompose(const std::span<uint_multiprecision_t> result,
                   const std::span<const std::uint64_t>   res0,
                   const std::span<const std::uint64_t>   res1,
                   const std::span<const std::uint64_t>   res2,
                   const crt3_constants&                  cc,
                   const unsigned                         b) noexcept {
    constexpr unsigned limb_bits    = static_cast<unsigned>(width_v<uint_multiprecision_t>);
    const std::size_t  result_coeff = res0.size();
    const std::size_t  result_limbs = result.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(res1.size() == result_coeff && res2.size() == result_coeff);

    std::uint64_t acc0 = 0; // 256-bit accumulator, low word first, holding the
    std::uint64_t acc1 = 0; // product window that starts at bit out * limb_bits
    std::uint64_t acc2 = 0;
    std::uint64_t acc3 = 0;
    std::size_t   out  = 0;

    const auto flush_limb = [&]() noexcept {
        result[out++] = static_cast<uint_multiprecision_t>(acc0);
        if constexpr (limb_bits == word_bits) {
            acc0 = acc1;
            acc1 = acc2;
            acc2 = acc3;
            acc3 = 0;
        } else {
            acc0 = (acc0 >> limb_bits) | (acc1 << (word_bits - limb_bits));
            acc1 = (acc1 >> limb_bits) | (acc2 << (word_bits - limb_bits));
            acc2 = (acc2 >> limb_bits) | (acc3 << (word_bits - limb_bits));
            acc3 >>= limb_bits;
        }
    };

    for (std::size_t k = 0; k < result_coeff; ++k) {
        std::size_t offset = b * k - out * limb_bits; // bit offset of c[k] within the window
        while (offset >= limb_bits) {
            flush_limb();
            offset -= limb_bits;
        }
        const crt3_word c  = crt3(res0[k], res1[k], res2[k], cc);
        const unsigned  sh = static_cast<unsigned>(offset); // < limb_bits <= word_bits
        std::uint64_t   t0 = 0;
        std::uint64_t   t1 = 0;
        std::uint64_t   t2 = 0;
        std::uint64_t   t3 = 0;
        if (sh == 0) {
            t0 = c.w0;
            t1 = c.w1;
            t2 = c.w2;
        } else {
            t0 = c.w0 << sh;
            t1 = (c.w0 >> (word_bits - sh)) | (c.w1 << sh);
            t2 = (c.w1 >> (word_bits - sh)) | (c.w2 << sh);
            t3 = c.w2 >> (word_bits - sh);
        }
        const auto s0 = carrying_add(acc0, t0);
        const auto s1 = carrying_add(acc1, t1, s0.carry);
        const auto s2 = carrying_add(acc2, t2, s1.carry);
        acc0          = s0.value;
        acc1          = s1.value;
        acc2          = s2.value;
        acc3          = acc3 + t3 + std::uint64_t{s2.carry};
    }

    while (out < result_limbs) {
        flush_limb();
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(acc0 == 0 && acc1 == 0 && acc2 == 0 && acc3 == 0);
}

// Cyclic counterpart of fft_recompose: the coefficients span exactly the
// wrap bit-length (b * L == 64 * w), so after draining w limbs the
// accumulator holds T = floor(sum / 2^(64w)) < 2^(fft_crt_bits + 1 - b) --
// at most two words -- which folds back into the bottom because
// 2^(64w) == 1 (mod 2^(64w) - 1). Output is semi-canonical (the all-ones
// pattern, equal to the modulus, may appear and means zero).
void fft_recompose_cyclic(const std::span<uint_multiprecision_t> result,
                          const std::span<const std::uint64_t>   res0,
                          const std::span<const std::uint64_t>   res1,
                          const std::span<const std::uint64_t>   res2,
                          const crt3_constants&                  cc,
                          const unsigned                         b) noexcept {
    constexpr unsigned limb_bits    = static_cast<unsigned>(width_v<uint_multiprecision_t>);
    const std::size_t  result_coeff = res0.size();
    const std::size_t  result_limbs = result.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(res1.size() == result_coeff && res2.size() == result_coeff);

    std::uint64_t acc0 = 0; // 256-bit accumulator, low word first, holding the
    std::uint64_t acc1 = 0; // product window that starts at bit out * limb_bits
    std::uint64_t acc2 = 0;
    std::uint64_t acc3 = 0;
    std::size_t   out  = 0;

    const auto take_limb = [&]() noexcept {
        const auto limb = static_cast<uint_multiprecision_t>(acc0);
        if constexpr (limb_bits == word_bits) {
            acc0 = acc1;
            acc1 = acc2;
            acc2 = acc3;
            acc3 = 0;
        } else {
            acc0 = (acc0 >> limb_bits) | (acc1 << (word_bits - limb_bits));
            acc1 = (acc1 >> limb_bits) | (acc2 << (word_bits - limb_bits));
            acc2 = (acc2 >> limb_bits) | (acc3 << (word_bits - limb_bits));
            acc3 >>= limb_bits;
        }
        return limb;
    };

    for (std::size_t k = 0; k < result_coeff; ++k) {
        std::size_t offset = b * k - out * limb_bits; // bit offset of c[k] within the window
        while (offset >= limb_bits) {
            result[out++] = take_limb();
            offset -= limb_bits;
        }
        const crt3_word c  = crt3(res0[k], res1[k], res2[k], cc);
        const unsigned  sh = static_cast<unsigned>(offset); // < limb_bits <= word_bits
        std::uint64_t   t0 = 0;
        std::uint64_t   t1 = 0;
        std::uint64_t   t2 = 0;
        std::uint64_t   t3 = 0;
        if (sh == 0) {
            t0 = c.w0;
            t1 = c.w1;
            t2 = c.w2;
        } else {
            t0 = c.w0 << sh;
            t1 = (c.w0 >> (word_bits - sh)) | (c.w1 << sh);
            t2 = (c.w1 >> (word_bits - sh)) | (c.w2 << sh);
            t3 = c.w2 >> (word_bits - sh);
        }
        const auto s0 = carrying_add(acc0, t0);
        const auto s1 = carrying_add(acc1, t1, s0.carry);
        const auto s2 = carrying_add(acc2, t2, s1.carry);
        acc0          = s0.value;
        acc1          = s1.value;
        acc2          = s2.value;
        acc3          = acc3 + t3 + std::uint64_t{s2.carry};
    }

    while (out < result_limbs) {
        result[out++] = take_limb();
    }

    // The spill past the wrap boundary folds back as an addition (B^w == 1).
    uint_multiprecision_t spill[4] = {};
    std::size_t           n_spill  = 0;
    while ((acc0 | acc1 | acc2 | acc3) != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(n_spill < 4);
        spill[n_spill++] = take_limb();
    }
    if (n_spill != 0) {
        add_mod_bnm1(result, std::span<const uint_multiprecision_t>{spill, n_spill});
    }
}

// The smallest 2-adicity across the three primes caps the transform length.
[[nodiscard]] std::uint64_t fft_min_adicity() noexcept {
    return std::min(
        {ntt_fp_primes[0].mod.log2_order, ntt_fp_primes[1].mod.log2_order, ntt_fp_primes[2].mod.log2_order});
}

} // namespace

void multiply_fft(const std::span<uint_multiprecision_t>       result,
                  const std::span<const uint_multiprecision_t> a_untrimmed,
                  const std::span<const uint_multiprecision_t> b_untrimmed,
                  const std::span<double>                      fp_workspace,
                  const std::span<std::uint64_t>               int_workspace) noexcept {
    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty() && !b.empty());

    const std::size_t na           = a.size();
    const std::size_t nb           = b.size();
    const unsigned    coeff_bits   = fft_choose_coeff_bits(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, coeff_bits) + fft_coeff_count(nb, coeff_bits) - 1;
    const std::size_t n            = fft_transform_length(na, nb);
    BEMAN_BIG_INT_DEBUG_ASSERT(fp_workspace.size() >= fft_mul_fp_storage_size(na, nb));
    BEMAN_BIG_INT_DEBUG_ASSERT(int_workspace.size() >= fft_mul_int_storage_size(na, nb));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= fft_min_adicity());

    const auto                     fca    = fp_workspace.subspan(0, n);
    const auto                     fcb    = fp_workspace.subspan(n, n);
    const auto                     ftw    = fp_workspace.subspan(2 * n, n); // per-level twiddle table (n-1 entries)
    const std::span<std::uint64_t> res[3] = {
        int_workspace.subspan(0, result_coeff),
        int_workspace.subspan(result_coeff, result_coeff),
        int_workspace.subspan(2 * result_coeff, result_coeff),
    };

    for (std::size_t t = 0; t < 3; ++t) {
        const ntt_fp_modulus& m = ntt_fp_primes[t];
        fft_pack_fp(fca, a, coeff_bits, m);
        fft_pack_fp(fcb, b, coeff_bits, m);
        ntt_fp_build_twiddles(ftw, n, m, ntt_direction::forward);
        ntt_fp_forward(fca, ftw, m);
        ntt_fp_forward(fcb, ftw, m);
        ntt_fp_pointwise(fca, fcb, m);
        ntt_fp_build_twiddles(ftw, n, m, ntt_direction::inverse);
        ntt_fp_inverse(fca, ftw, m);
        for (std::size_t k = 0; k < result_coeff; ++k) {
            res[t][k] = static_cast<std::uint64_t>(fp_reduce_to_0n(fca[k], m.n, m.ninv));
        }
    }

    fft_recompose(result.first(na + nb), res[0], res[1], res[2], crt3_make_constants(), coeff_bits);
}

void square_fft(const std::span<uint_multiprecision_t>       result,
                const std::span<const uint_multiprecision_t> a_untrimmed,
                const std::span<double>                      fp_workspace,
                const std::span<std::uint64_t>               int_workspace) noexcept {
    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());

    const std::size_t na           = a.size();
    const unsigned    coeff_bits   = fft_choose_coeff_bits(na, na);
    const std::size_t result_coeff = 2 * fft_coeff_count(na, coeff_bits) - 1;
    const std::size_t n            = fft_transform_length(na, na);
    BEMAN_BIG_INT_DEBUG_ASSERT(fp_workspace.size() >= square_fft_fp_storage_size(na));
    BEMAN_BIG_INT_DEBUG_ASSERT(int_workspace.size() >= square_fft_int_storage_size(na));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= fft_min_adicity());

    const auto                     fca    = fp_workspace.subspan(0, n);
    const auto                     ftw    = fp_workspace.subspan(n, n); // per-level twiddle table (n-1 entries)
    const std::span<std::uint64_t> res[3] = {
        int_workspace.subspan(0, result_coeff),
        int_workspace.subspan(result_coeff, result_coeff),
        int_workspace.subspan(2 * result_coeff, result_coeff),
    };

    for (std::size_t t = 0; t < 3; ++t) {
        const ntt_fp_modulus& m = ntt_fp_primes[t];
        fft_pack_fp(fca, a, coeff_bits, m);
        ntt_fp_build_twiddles(ftw, n, m, ntt_direction::forward);
        ntt_fp_forward(fca, ftw, m);
        ntt_fp_pointwise(fca, fca, m);
        ntt_fp_build_twiddles(ftw, n, m, ntt_direction::inverse);
        ntt_fp_inverse(fca, ftw, m);
        for (std::size_t k = 0; k < result_coeff; ++k) {
            res[t][k] = static_cast<std::uint64_t>(fp_reduce_to_0n(fca[k], m.n, m.ninv));
        }
    }

    fft_recompose(result.first(2 * na), res[0], res[1], res[2], crt3_make_constants(), coeff_bits);
}

void multiply_fft_cyclic(const std::span<uint_multiprecision_t>       result,
                         const std::span<const uint_multiprecision_t> a_untrimmed,
                         const std::span<const uint_multiprecision_t> b_untrimmed,
                         const fft_cyclic_params                      params,
                         const std::span<double>                      fp_workspace,
                         const std::span<std::uint64_t>               int_workspace) noexcept {
    // The 64-bit-limb wrap algebra (b * L == 64 * w); callers gate on width.
    BEMAN_BIG_INT_DEBUG_ASSERT(width_v<uint_multiprecision_t> == 64);

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty() && !b.empty());

    const std::size_t w = params.wrap_limbs;
    const std::size_t n = params.length;
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() == w);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() <= w && b.size() <= w);
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::size_t>(params.coeff_bits) * n == width_v<uint_multiprecision_t> * w);
    BEMAN_BIG_INT_DEBUG_ASSERT(fp_workspace.size() >= fft_cyclic_fp_storage_size(params));
    BEMAN_BIG_INT_DEBUG_ASSERT(int_workspace.size() >= fft_cyclic_int_storage_size(params));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= fft_min_adicity());

    const auto                     fca    = fp_workspace.subspan(0, n);
    const auto                     fcb    = fp_workspace.subspan(n, n);
    const auto                     ftw    = fp_workspace.subspan(2 * n, n); // per-level twiddle table (n-1 entries)
    const std::span<std::uint64_t> res[3] = {
        int_workspace.subspan(0, n),
        int_workspace.subspan(n, n),
        int_workspace.subspan(2 * n, n),
    };

    // No zero-padding: b * L == 64 * w makes the length-L transform's natural
    // mod x^L - 1 wraparound exactly the value wraparound mod 2^(64w) - 1.
    for (std::size_t t = 0; t < 3; ++t) {
        const ntt_fp_modulus& m = ntt_fp_primes[t];
        fft_pack_fp(fca, a, params.coeff_bits, m);
        fft_pack_fp(fcb, b, params.coeff_bits, m);
        ntt_fp_build_twiddles(ftw, n, m, ntt_direction::forward);
        ntt_fp_forward(fca, ftw, m);
        ntt_fp_forward(fcb, ftw, m);
        ntt_fp_pointwise(fca, fcb, m);
        ntt_fp_build_twiddles(ftw, n, m, ntt_direction::inverse);
        ntt_fp_inverse(fca, ftw, m);
        for (std::size_t k = 0; k < n; ++k) {
            res[t][k] = static_cast<std::uint64_t>(fp_reduce_to_0n(fca[k], m.n, m.ninv));
        }
    }

    fft_recompose_cyclic(result, res[0], res[1], res[2], crt3_make_constants(), params.coeff_bits);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE
