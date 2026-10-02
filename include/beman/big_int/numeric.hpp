// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_NUMERIC_HPP
#define BEMAN_BIG_INT_NUMERIC_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <limits>
    #include <memory>
    #include <span>
    #include <type_traits>
    #include <utility>
#endif

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/gcd_impl.hpp>

BEMAN_BIG_INT_BEGIN_NAMESPACE

// [c.math.abs]
// Returns the absolute value (magnitude) of `j`.
// This cannot overflow since `basic_big_int` is unbounded
template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A> abs(const basic_big_int<b, L, A>& j) {
    // Plain copy construction, so the result's allocator comes from
    // `select_on_container_copy_construction`: an allocator that declines to
    // propagate into a copy declines to propagate into an arithmetic result too.
    basic_big_int<b, L, A> result(j);
    result.unchecked_set_sign(false);
    return result;
}

// The overload for an argument the caller has handed over: the storage is taken
// as-is, so clearing the sign is the whole operation and nothing is allocated.
template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A> abs(basic_big_int<b, L, A>&& j) noexcept {
    basic_big_int<b, L, A> result(std::move(j));
    result.unchecked_set_sign(false);
    return result;
}

// [numeric.sat.cast]
// Casts `x` to `R`, clamping to the range of `R`. If the integer value of `x`
// is representable as `R`, that value is returned; otherwise the largest or
// smallest representable value of `R`, whichever is closer to `x`.
BEMAN_BIG_INT_EXPORT template <class R, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<R>
constexpr R saturating_cast(const basic_big_int<b, L, A>& x) noexcept {
    using U = detail::make_unsigned_t<R>;

    constexpr std::size_t width = detail::width_v<R>;
    if constexpr (detail::unsigned_integer<R>) {
        constexpr R hi = static_cast<R>(~U{0});

        if (x < R{0}) {
            return R{0};
        }
        if (x > hi) {
            return hi;
        }
    } else {
        constexpr R lo = static_cast<R>(U{1} << (width - 1));
        constexpr R hi = static_cast<R>((U{1} << (width - 1)) - U{1});

        if (x < lo) {
            return lo;
        }
        if (x > hi) {
            return hi;
        }
    }

    return static_cast<R>(x);
}

BEMAN_BIG_INT_EXPORT template <class R, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<R>
constexpr bool in_range(const basic_big_int<b, L, A>& t) noexcept {
    return (t >= std::numeric_limits<R>::min()) && (t <= std::numeric_limits<R>::max());
}

namespace detail {

// A fundamental-integer operand has to have its magnitude spelled out into limbs
// before the span algorithms can see it; a `basic_big_int` already stores limbs,
// so it needs no storage at all.
template <class T>
[[nodiscard]] constexpr auto operand_limbs(const T& x) noexcept {
    if constexpr (is_basic_big_int_v<T>) {
        return std::array<uint_multiprecision_t, 0>{};
    } else {
        return to_limbs(uabs(x));
    }
}

// The magnitude of an operand as a limb span, paired with the storage handed out
// by `operand_limbs` for the fundamental-integer case.
template <class T, std::size_t n>
[[nodiscard]] constexpr std::span<const uint_multiprecision_t>
operand_magnitude(const T& x, const std::array<uint_multiprecision_t, n>& limbs) noexcept {
    if constexpr (is_basic_big_int_v<T>) {
        return x.representation();
    } else {
        return std::span<const uint_multiprecision_t>{limbs.data(), n};
    }
}

// The allocator that every value a mixed-operand function creates -- including
// the one it returns -- is built with. Selected exactly as a binary operator's
// result allocator is, so `gcd` and friends follow the same rule as `abs` and
// `operator*`.
template <class M, class N>
[[nodiscard]] constexpr auto operand_allocator(const M& m, const N& n) noexcept {
    if constexpr (is_basic_big_int_v<M>) {
        return result_allocator<M>(m, n);
    } else {
        return result_allocator<N>(m, n);
    }
}

// The driver behind `gcd`, taking both operands as forwarding references and
// classifying them into `binary_op_form` exactly as the binary operators do: an
// operand the caller handed over has its storage consumed, a borrowed one is
// copied, and the paths that need no mutable copy borrow both.
template <class M, class N>
[[nodiscard]] constexpr common_big_int_type<M, N> gcd_impl(M&& m, N&& n) {
    using Result     = common_big_int_type<M, N>;
    using const_span = std::span<const uint_multiprecision_t>;

    constexpr auto form = classify_form_v<M, N>;
    constexpr bool steal_m =
        form == binary_op_form::move_move || form == binary_op_form::move_copy || form == binary_op_form::move_int;
    constexpr bool steal_n =
        form == binary_op_form::move_move || form == binary_op_form::copy_move || form == binary_op_form::int_move;

    const auto alloc = detail::operand_allocator(m, n);

    // |value| as a result the algorithm below may consume in place: a handed-over
    // big_int gives up its storage, a borrowed one is copied. The `steal` tag is a
    // parameter rather than a template argument because a lambda cannot take an
    // explicit template argument at the call site.
    const auto magnitude_of = [&alloc]<class T, bool steal>(T&& value, std::bool_constant<steal>) -> Result {
        if constexpr (is_basic_big_int_v<std::remove_cvref_t<T>>) {
            Result r = [&]() -> Result {
                if constexpr (steal) {
                    return Result{std::move(value)};
                } else {
                    return Result{value, alloc};
                }
            }();
            r.unchecked_set_sign(false);
            return r;
        } else {
            return Result{uabs(value), alloc};
        }
    };

    // Both operands are viewed as bare magnitudes: the signs are dropped here and
    // never looked at again.
    const auto       m_store = detail::operand_limbs(m);
    const auto       n_store = detail::operand_limbs(n);
    const const_span m_mag   = detail::operand_magnitude(m, m_store);
    const const_span n_mag   = detail::operand_magnitude(n, n_store);
    const const_span m_trim  = m_mag.first(detail::trimmed_size_span(m_mag));
    const const_span n_trim  = n_mag.first(detail::trimmed_size_span(n_mag));

    // gcd(x, 0) == |x|, and so gcd(0, 0) == 0.
    if (detail::is_span_zero(n_trim)) {
        return magnitude_of(m, std::bool_constant<steal_m>{});
    }
    if (detail::is_span_zero(m_trim)) {
        return magnitude_of(n, std::bool_constant<steal_n>{});
    }

    // Small operands never allocate: a magnitude that fits a limb is handled by
    // the scalar algorithm, and a single-limb operand reduces the other one with
    // a short division rather than a copy.
    if (m_trim.size() == 1 && n_trim.size() == 1) {
        return Result{detail::gcd_limbs(m_trim[0], n_trim[0]), alloc};
    }
    if (n_trim.size() == 1) {
        return Result{detail::gcd_short(m_trim, n_trim[0]), alloc};
    }
    if (m_trim.size() == 1) {
        return Result{detail::gcd_short(n_trim, m_trim[0]), alloc};
    }

    // Both magnitudes span several limbs, so the reduction needs writable copies
    // of them. Everything past this point works on `a` and `b`; the views above
    // are not touched again.
    Result a = magnitude_of(m, std::bool_constant<steal_m>{});
    Result b = magnitude_of(n, std::bool_constant<steal_n>{});

    // Euclidean steps while the operands are far apart in size: one division wipes
    // out a bit-length gap that the reduction below would otherwise grind away a
    // bit at a time. Both branches leave the remainder in `b`, and each shrinks
    // the larger operand below the smaller, so a gap this wide is gone after a
    // step or two. A single-limb operand is left to the reduction itself, which
    // finishes those with a short division.
    constexpr std::size_t gap = detail::width_v<uint_multiprecision_t> / 2;
    for (;;) {
        if (detail::trimmed_size_span(a.representation()) == 1 || detail::trimmed_size_span(b.representation()) == 1) {
            break;
        }
        const std::size_t a_bits = a.size();
        const std::size_t b_bits = b.size();
        if (a_bits > b_bits + gap) {
            Result r = a % b;
            a        = std::move(b);
            b        = std::move(r);
        } else if (b_bits > a_bits + gap) {
            b = b % a;
        } else {
            break;
        }
        if (detail::is_span_zero(b.representation())) {
            return a; // `a` divides the other operand exactly.
        }
    }

    const std::size_t na    = detail::trimmed_size_span(a.representation());
    const std::size_t nb    = detail::trimmed_size_span(b.representation());
    const std::size_t width = std::max(na, nb) + 1;

    // Lehmer's reduction needs a third buffer and a limb of headroom in each
    // operand. Narrower operands are reduced by binary steps alone, which need
    // neither, so nothing extra is allocated for them.
    const bool wide = std::max(na, nb) >= 3;
    Result     t{alloc};
    if (wide) {
        a.reserve_representation(width);
        b.reserve_representation(width);
        t.reserve_representation(width);
    }

    const std::size_t size =
        detail::gcd_unsigned_spans(std::span<uint_multiprecision_t>{a.limb_ptr(), a.representation_capacity()},
                                   na,
                                   std::span<uint_multiprecision_t>{b.limb_ptr(), b.representation_capacity()},
                                   nb,
                                   wide ? std::span<uint_multiprecision_t>{t.limb_ptr(), t.representation_capacity()}
                                        : std::span<uint_multiprecision_t>{});

    a.unchecked_set_limb_count(static_cast<std::uint32_t>(size));
    if (a.is_representation_inplace()) {
        // Restore the "limbs above the limb count are zero" invariant that inline
        // storage carries.
        uint_multiprecision_t* const limbs = a.limb_ptr();
        for (std::size_t i = size; i < Result::inplace_capacity; ++i) {
            limbs[i] = 0;
        }
    }
    return a;
}

// The driver behind `lcm`, which has no reduction of its own: the least common
// multiple is the product of the two magnitudes with their greatest common
// divisor taken out once, so the work is one `gcd`, one exact division, and one
// multiplication. The operands are classified into `binary_op_form` the way
// `gcd_impl` classifies its own, except that the gcd needs both of them, so the
// storage of an operand the caller handed over is consumed by the division and
// the multiplication that follow rather than by the reduction.
template <class M, class N>
[[nodiscard]] constexpr common_big_int_type<M, N> lcm_impl(M&& m, N&& n) {
    using Result     = common_big_int_type<M, N>;
    using const_span = std::span<const uint_multiprecision_t>;

    constexpr auto form = classify_form_v<M, N>;
    constexpr bool steal_m =
        form == binary_op_form::move_move || form == binary_op_form::move_copy || form == binary_op_form::move_int;
    constexpr bool steal_n =
        form == binary_op_form::move_move || form == binary_op_form::copy_move || form == binary_op_form::int_move;

    const auto alloc = detail::operand_allocator(m, n);

    // One operand as a value the arithmetic below may consume: a big_int the
    // caller handed over gives up its storage, a borrowed one is copied, and a
    // fundamental integer contributes its magnitude -- so only a big_int operand
    // can carry a sign into the product, and that one is dropped at the end. The
    // `steal` tag is a parameter rather than a template argument because a lambda
    // cannot take an explicit template argument at the call site.
    const auto owned = [&alloc]<class T, bool steal>(T&& value, std::bool_constant<steal>) -> Result {
        if constexpr (is_basic_big_int_v<std::remove_cvref_t<T>>) {
            if constexpr (steal) {
                return Result{std::move(value)};
            } else {
                return Result{value, alloc};
            }
        } else {
            return Result{uabs(value), alloc};
        }
    };

    // Only the magnitudes are ever looked at; the result of `lcm` is the least
    // common multiple of those, and so is never negative.
    const auto       m_store = detail::operand_limbs(m);
    const auto       n_store = detail::operand_limbs(n);
    const const_span m_mag   = detail::operand_magnitude(m, m_store);
    const const_span n_mag   = detail::operand_magnitude(n, n_store);
    const const_span m_trim  = m_mag.first(detail::trimmed_size_span(m_mag));
    const const_span n_trim  = n_mag.first(detail::trimmed_size_span(n_mag));

    // lcm(x, 0) == 0: zero is the only multiple the two operands have in common,
    // and so lcm(0, 0) == 0 as well.
    if (detail::is_span_zero(m_trim) || detail::is_span_zero(n_trim)) {
        return Result{0, alloc};
    }

    // Magnitudes that both fit a limb keep the whole computation scalar: the
    // product of a cofactor and a limb spans at most two limbs, and a result
    // that fits one is built as a single limb so that it can stay in the
    // small-object buffer.
    if (m_trim.size() == 1 && n_trim.size() == 1) {
        const uint_multiprecision_t g        = detail::gcd_limbs(m_trim[0], n_trim[0]);
        const uint_multiprecision_t cofactor = m_trim[0] / g;
        const auto [low, high]               = detail::widening_mul(cofactor, n_trim[0]);
        if (high == 0) {
            return Result{low, alloc};
        }
        const std::array<uint_multiprecision_t, 2> limbs{low, high};
        return Result{limbs.begin(), limbs.end(), alloc};
    }

    // `m` and `n` are lvalues here, so the reduction borrows both of them and
    // leaves them to the division and the multiplication below.
    const Result g = detail::gcd_impl(m, n);

    // |m| * |n| with the greatest common divisor taken out once, and taken out
    // before the multiplication so that no intermediate value is wider than the
    // result. Compound assignment keeps the arithmetic in `dividend`, which is
    // what carries `alloc` into the result: the binary operators build a result of
    // their own, and it follows the allocator convention of an expression rather
    // than `gcd`'s.
    const auto reduce_and_multiply = [&g](Result dividend, const auto& other) -> Result {
        dividend /= g; // Exact: `g` divides both operands.
        dividend *= other;
        return abs(std::move(dividend)); // The product carries the signs; the result has none.
    };

    // The operand with fewer limbs is the cheaper one to divide, and dividing it
    // leaves the cheaper multiplication behind as well.
    if (n_trim.size() < m_trim.size()) {
        return reduce_and_multiply(owned(n, std::bool_constant<steal_n>{}), m);
    }
    return reduce_and_multiply(owned(m, std::bool_constant<steal_m>{}), n);
}

template <class T>
[[nodiscard]] constexpr bool operand_is_negative(const T& x) noexcept {
    if constexpr (is_basic_big_int_v<T>) {
        return x < 0;
    } else if constexpr (signed_integer<T>) {
        return x < T{0};
    } else {
        return false;
    }
}

// The driver behind `midpoint`. The midpoint of `m` and `n` is `m + (n - m) / 2`
// with the halving truncated toward zero, which is what rounds the result toward
// `m` when the two operands are an odd distance apart.
template <class M, class N>
[[nodiscard]] constexpr common_big_int_type<M, N> midpoint_impl(M&& m, N&& n) {
    using Result     = common_big_int_type<M, N>;
    using const_span = std::span<const uint_multiprecision_t>;

    constexpr auto form = classify_form_v<M, N>;
    constexpr bool steal_n =
        form == binary_op_form::move_move || form == binary_op_form::copy_move || form == binary_op_form::int_move;

    const auto alloc = detail::operand_allocator(m, n);

    const auto       m_store = detail::operand_limbs(m);
    const auto       n_store = detail::operand_limbs(n);
    const const_span m_mag   = detail::operand_magnitude(m, m_store);
    const const_span n_mag   = detail::operand_magnitude(n, n_store);
    const const_span m_trim  = m_mag.first(detail::trimmed_size_span(m_mag));
    const const_span n_trim  = n_mag.first(detail::trimmed_size_span(n_mag));

    const bool m_negative = detail::operand_is_negative(m);
    const bool n_negative = detail::operand_is_negative(n);

    // Magnitudes that both fit a limb keep the whole computation scalar and
    // allocation-free: the midpoint is never farther from zero than the operand
    // that is, so a result built out of two single-limb magnitudes fits a limb
    // as well.
    if (m_trim.size() == 1 && n_trim.size() == 1) {
        const uint_multiprecision_t am = m_trim[0];
        const uint_multiprecision_t an = n_trim[0];

        uint_multiprecision_t magnitude = 0;
        bool                  negative  = false;
        if (m_negative == n_negative) {
            // Both operands on the same side of zero: the midpoint lies between
            // the two magnitudes, so half the gap between them is added onto |m|
            // or taken off it, and the sign is the one the operands share.
            magnitude = an >= am ? am + (an - am) / 2 : am - (am - an) / 2;
            negative  = m_negative;
        } else {
            // Operands on opposite sides of zero: the gap is |m| + |n|, which can
            // carry past a limb, so it is halved as it is formed. The midpoint
            // sits that far from `m` toward zero, which puts it on `n`'s side once
            // the half gap runs past |m|.
            const uint_multiprecision_t half_gap = (am & an) + ((am ^ an) >> 1);
            magnitude                            = am >= half_gap ? am - half_gap : half_gap - am;
            negative                             = am >= half_gap ? m_negative : n_negative;
        }

        Result r{magnitude, alloc};
        if (negative && magnitude != 0) {
            r.unchecked_set_sign(true);
        }
        return r;
    }

    // `n` as a value the arithmetic below consumes: a big_int the caller handed
    // over gives up its storage, and anything else -- a borrowed big_int, or a
    // fundamental integer -- is built with the allocator the result carries.
    Result r = [&]() -> Result {
        if constexpr (steal_n) {
            return Result{std::move(n)};
        } else {
            return Result{n, alloc};
        }
    }();

    r -= m; // n - m

    // (n - m) / 2, truncated toward zero rather than toward negative infinity:
    // the sign is set aside so that the shift only ever sees the magnitude, and
    // truncating the magnitude is what rounds the result toward `m`.
    const bool difference_negative = r.is_negative();
    r.unchecked_set_sign(false);
    r.shift_right(1);
    r.unchecked_set_sign(difference_negative && !r.unchecked_is_magnitude_zero());

    r += m; // m + (n - m) / 2
    return r;
}

} // namespace detail

// [numeric.gcd]
// Computes the greatest common divisor of integers m and n
// If either `M` or `N` is not an integer type, or if either is
// (possibly cv-qualified) `bool` the program is ill-formed
// If either |M| or |N| is not representable as a value of type
// `std::common_type<M, N>`, the behavior is undefined.
// In this case the common type is always a `basic_big_int`
//
// The operands are taken by forwarding reference, so a `basic_big_int` argument
// the caller hands over has its storage consumed and a borrowed one is only copied
// where the reduction needs a mutable value. The big_int operand is spelled as a
// specialization rather than a deduced parameter on purpose: every basic_big_int
// drags namespace std into argument-dependent lookup through its allocator, so
// `std::gcd` is always a candidate too, and only a parameter that std::gcd's plain
// type parameter cannot deduce makes these overloads the more specialized ones.
// Whether the other operand is admissible is left to the return type, which is
// ill-formed for anything but the same specialization or a signed or unsigned
// integer type.
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A, class N>
[[nodiscard]] constexpr detail::common_big_int_type<basic_big_int<b, L, A>, N> gcd(basic_big_int<b, L, A>&& m, N&& n) {
    return detail::gcd_impl(std::move(m), std::forward<N>(n));
}

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A, class N>
[[nodiscard]] constexpr detail::common_big_int_type<basic_big_int<b, L, A>, N> gcd(const basic_big_int<b, L, A>& m,
                                                                                   N&&                           n) {
    return detail::gcd_impl(m, std::forward<N>(n));
}

// The mirrored pair. `M` is held to an integer type so that a pair of big_ints
// does not match both it and the overloads above.
BEMAN_BIG_INT_EXPORT template <class M, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<std::remove_cvref_t<M>>
[[nodiscard]] constexpr basic_big_int<b, L, A> gcd(M&& m, basic_big_int<b, L, A>&& n) {
    return detail::gcd_impl(std::forward<M>(m), std::move(n));
}

BEMAN_BIG_INT_EXPORT template <class M, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<std::remove_cvref_t<M>>
[[nodiscard]] constexpr basic_big_int<b, L, A> gcd(M&& m, const basic_big_int<b, L, A>& n) {
    return detail::gcd_impl(std::forward<M>(m), n);
}

// [numeric.lcm]
// Computes the least common multiple of integers m and n
// If either `M` or `N` is not an integer type, or if either is
// (possibly cv-qualified) `bool` the program is ill-formed
// If either |M| or |N|, or the least common multiple of the two, is not
// representable as a value of type `std::common_type<M, N>`, the behavior is
// undefined. In this case the common type is always a `basic_big_int`, which is
// unbounded, so no value can fail to be representable
//
// The operands are taken by forwarding reference, so a `basic_big_int` argument
// the caller hands over has its storage consumed by the division or the
// multiplication that produces the result. The overload set is spelled out the
// way `gcd`'s is, and for the same reason: `std::lcm` is a candidate of every
// unqualified call through argument-dependent lookup on the allocator, and only
// a parameter that its plain type parameter cannot deduce makes these overloads
// the more specialized ones.
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A, class N>
[[nodiscard]] constexpr detail::common_big_int_type<basic_big_int<b, L, A>, N> lcm(basic_big_int<b, L, A>&& m, N&& n) {
    return detail::lcm_impl(std::move(m), std::forward<N>(n));
}

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A, class N>
[[nodiscard]] constexpr detail::common_big_int_type<basic_big_int<b, L, A>, N> lcm(const basic_big_int<b, L, A>& m,
                                                                                   N&&                           n) {
    return detail::lcm_impl(m, std::forward<N>(n));
}

// The mirrored pair. `M` is held to an integer type so that a pair of big_ints
// does not match both it and the overloads above.
BEMAN_BIG_INT_EXPORT template <class M, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<std::remove_cvref_t<M>>
[[nodiscard]] constexpr basic_big_int<b, L, A> lcm(M&& m, basic_big_int<b, L, A>&& n) {
    return detail::lcm_impl(std::forward<M>(m), std::move(n));
}

BEMAN_BIG_INT_EXPORT template <class M, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<std::remove_cvref_t<M>>
[[nodiscard]] constexpr basic_big_int<b, L, A> lcm(M&& m, const basic_big_int<b, L, A>& n) {
    return detail::lcm_impl(std::forward<M>(m), n);
}

// [numeric.midpoint]
// Computes half the sum of integers m and n, rounded toward m when the sum is
// odd. If either `M` or `N` is not an integer type, or if either is
// (possibly cv-qualified) `bool` the program is ill-formed
// No overflow occurs: in this case the common type is always a `basic_big_int`,
// which is unbounded, so every value the computation forms is representable
//
// The operands are taken by forwarding reference, so a `basic_big_int` passed as
// the second argument has its storage consumed by the arithmetic that produces
// the result; the first is read after the halving, so it is left alone. The
// overload set is spelled out the way `gcd`'s is so that an unqualified call
// resolves the same way: `std::midpoint` is a candidate of every such call
// through argument-dependent lookup on the allocator, and although its own
// constraints already drop it for a `basic_big_int` argument, spelling the
// big_int operand as a specialization is what lets these overloads accept the
// mixed pairs that `std::midpoint`'s single type parameter cannot deduce.
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A, class N>
[[nodiscard]] constexpr detail::common_big_int_type<basic_big_int<b, L, A>, N> midpoint(basic_big_int<b, L, A>&& m,
                                                                                        N&&                      n) {
    return detail::midpoint_impl(std::move(m), std::forward<N>(n));
}

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A, class N>
[[nodiscard]] constexpr detail::common_big_int_type<basic_big_int<b, L, A>, N>
midpoint(const basic_big_int<b, L, A>& m, N&& n) {
    return detail::midpoint_impl(m, std::forward<N>(n));
}

// The mirrored pair. `M` is held to an integer type so that a pair of big_ints
// does not match both it and the overloads above.
BEMAN_BIG_INT_EXPORT template <class M, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<std::remove_cvref_t<M>>
[[nodiscard]] constexpr basic_big_int<b, L, A> midpoint(M&& m, basic_big_int<b, L, A>&& n) {
    return detail::midpoint_impl(std::forward<M>(m), std::move(n));
}

BEMAN_BIG_INT_EXPORT template <class M, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<std::remove_cvref_t<M>>
[[nodiscard]] constexpr basic_big_int<b, L, A> midpoint(M&& m, const basic_big_int<b, L, A>& n) {
    return detail::midpoint_impl(std::forward<M>(m), n);
}

BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_NUMERIC_HPP
