// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BASIC_BIG_INT_HPP
#define BEMAN_BIG_INT_BASIC_BIG_INT_HPP

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <algorithm>
    #include <array>
    #include <bit>
    #include <climits>
    #include <charconv> // for the std::from_chars_result / std::to_chars_result in the friend declarations below
    #include <cmath>
    #include <concepts>
    #include <cstddef>
    #include <cstdint>
    #include <functional>
    #include <limits>
    #include <memory>
    #include <memory_resource>
    #include <ranges>
    #include <span>
    #include <utility>
    #include <type_traits>
#endif // BEMAN_BIG_INT_BUILD_MODULE

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/floats.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/siphash.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

BEMAN_BIG_INT_BEGIN_NAMESPACE

// alias uint_multiprecision_t
using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;

// Forward decl so that we can define our concepts
BEMAN_BIG_INT_EXPORT template <std::size_t min_inplace_bits,
                               class Limb      = uint_multiprecision_t,
                               class Allocator = std::allocator<Limb>>
class basic_big_int;

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
constexpr std::from_chars_result from_chars(const char*, const char*, basic_big_int<b, L, A>&, int = 10);

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
constexpr std::to_chars_result to_chars(char*, char*, const basic_big_int<b, L, A>&, int = 10);

BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
constexpr std::to_chars_result to_chars(char*, char*, basic_big_int<b, L, A>&&, int = 10);

namespace detail {

template <class>
struct is_basic_big_int : std::false_type {};

template <std::size_t b, class L, class A>
struct is_basic_big_int<basic_big_int<b, L, A>> : std::true_type {};

template <class T>
inline constexpr bool is_basic_big_int_v = is_basic_big_int<std::remove_cvref_t<T>>::value;

// Recovers the limb type of a basic_big_int specialization, which is otherwise private.
template <class>
struct limb_type_of {};

template <std::size_t b, class L, class A>
struct limb_type_of<basic_big_int<b, L, A>> {
    using type = L;
};

template <class T>
using limb_type_of_t = typename limb_type_of<std::remove_cvref_t<T>>::type;

// [big.ing.expos]
template <class T>
concept arbitrary_integer = signed_or_unsigned<std::remove_cvref_t<T>> || detail::is_basic_big_int_v<T>;

template <class T>
concept arbitrary_arithmetic = std::is_floating_point_v<T> || arbitrary_integer<T>;

template <class LT, class RT>
auto common_big_int_type_impl() {
    if constexpr (is_basic_big_int_v<LT>) {
        if constexpr ((is_basic_big_int_v<RT> && std::is_same_v<LT, RT>)) {
            return std::type_identity<LT>{};
        } else if constexpr (signed_or_unsigned<RT>) {
            return std::type_identity<LT>{};
        }
    } else if constexpr (is_basic_big_int_v<RT> && signed_or_unsigned<LT>) {
        return std::type_identity<RT>{};
    }
}

template <class LT, class RT>
using common_big_int_type =
    typename decltype(common_big_int_type_impl<std::remove_cvref_t<LT>, std::remove_cvref_t<RT>>())::type;

template <class T, class U>
concept common_big_int_type_with = requires { typename common_big_int_type<T, U>; };

template <std::size_t inplace_bits, class T>
inline constexpr bool no_alloc_constructible_from = []() {
    if constexpr (std::integral<std::remove_cvref_t<T>>) {
        return width_v<std::remove_cvref_t<T>> <= inplace_bits;
    } else {
        return false;
    }
}();

template <std::size_t b, class L, class A, class T>
inline constexpr bool is_implicit_constructible_from = detail::signed_or_unsigned<std::remove_cvref_t<T>> ||
                                                       std::is_same_v<std::remove_cvref_t<T>, basic_big_int<b, L, A>>;

#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST
using std::allocation_result;
#else
template <class Pointer, class SizeType = std::size_t>
struct allocation_result {
    Pointer  ptr;
    SizeType count;
};
#endif // BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST

[[noreturn]] inline void throw_length_error() {
#ifdef BEMAN_BIG_INT_ALLOW_EXCEPTIONS
    throw std::length_error("beman::big_int: requested size exceeds max_size()");
#else
    std::abort();
#endif
}

// Returns the mathematically correct `abs(x)` for a given signed integer `x`,
// where the result is an unsigned integer.
// Unlike `std::abs`, this function has no undefined behavior in e.g. `uabs(INT_MIN)`.
template <signed_integer T>
[[nodiscard]] constexpr detail::make_unsigned_t<T> uabs(const T x) noexcept {
    using U = detail::make_unsigned_t<T>;
    BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4146) // unary minus on unsigned is intentional
    return x < 0 ? static_cast<U>(-static_cast<U>(x)) : static_cast<U>(x);
    BEMAN_BIG_INT_DIAGNOSTIC_POP()
}

// This is purely for convenience, so we don't have to check all the time if an integer is signed or not
template <unsigned_integer T>
[[nodiscard]] constexpr T uabs(const T x) noexcept {
    return x;
}

// The integer version of signbit, and again with unsigned impl purely out of convenience
// Follows std::signbit convention: True = value is negative, False = value is positive
template <signed_integer T>
[[nodiscard]] constexpr bool integer_signbit(const T x) noexcept {
    return x < T{0};
}

template <unsigned_integer T>
[[nodiscard]] constexpr bool integer_signbit(const T) noexcept {
    return false;
}

// Returns `std::strong_ordering::less` if `x` is `std::strong_ordering::greater`, and vice versa.
[[nodiscard]] constexpr std::strong_ordering invert(const std::strong_ordering x) noexcept {
    return std::bit_cast<std::strong_ordering>(static_cast<signed char>(-std::bit_cast<signed char>(x)));
}

static_assert(invert(std::strong_ordering::less) == std::strong_ordering::greater,
              "Weird standard library. std::strong_ordering was expected to be a wrapper for signed char, "
              "where negation exchanges less and greater.");

enum struct bitwise_op : unsigned char { and_, or_, xor_ };

// Whether an assignment that copies a value also adopts the source's allocator.
enum struct allocator_propagation : unsigned char { propagate, no_propagate };

template <bitwise_op op, cv_unqualified_integral T>
[[nodiscard]] constexpr T eval_bitwise(const T x, const T y) noexcept {
    if constexpr (op == bitwise_op::and_) {
        return static_cast<T>(x & y);
    } else if constexpr (op == bitwise_op::or_) {
        return static_cast<T>(x | y);
    } else if constexpr (op == bitwise_op::xor_) {
        return static_cast<T>(x ^ y);
    } else {
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("Unsupported operation.");
    }
}

// Returns the number of limbs needed to represent the magnitude of the result
// of `(lhs_size limbs, neg_left) OP (rhs_size limbs, neg_right)`.
template <bitwise_op op, bool neg_left, bool neg_right>
[[nodiscard]] constexpr std::size_t bitwise_result_limb_count(const std::size_t lhs_size,
                                                              const std::size_t rhs_size) noexcept {
    if constexpr (op == bitwise_op::and_) {
        if constexpr (!neg_left && !neg_right)
            return std::min(lhs_size, rhs_size);
        else if constexpr (!neg_left)
            return lhs_size;
        else if constexpr (!neg_right)
            return rhs_size;
        else
            return std::max(lhs_size, rhs_size);
    } else {
        return std::max(lhs_size, rhs_size);
    }
}

// Two's-complement bitwise operation on little-endian unsigned limb spans.
// Computes `(lhs, lhs_neg) OP (rhs, rhs_neg)` and writes the magnitude of
// the result into `out`, returning whether the result is negative.
//
// `out` may safely alias `lhs` or `rhs`: limb `i` of both inputs is read
// before limb `i` of `out` is written in the same iteration.
//
// Preconditions:
//   - out.size() >= n   where n is computed internally from op/signs
//   - lhs and rhs are trimmed (no requirement, but carry logic is exact)
template <bitwise_op op, bool neg_left, bool neg_right, std::size_t extent_a, std::size_t extent_b>
constexpr bool eval_bitwise_into_spans(const std::span<const uint_multiprecision_t, extent_a> lhs,
                                       const std::span<const uint_multiprecision_t, extent_b> rhs,
                                       const std::span<uint_multiprecision_t>                 out) noexcept {
    constexpr bool res_neg = eval_bitwise<op>(neg_left, neg_right);

    const std::size_t n = bitwise_result_limb_count<op, neg_left, neg_right>(lhs.size(), rhs.size());

    BEMAN_BIG_INT_DEBUG_ASSERT(out.size() >= n + static_cast<std::size_t>(res_neg));

    bool carry_l = neg_left;
    bool carry_r = neg_right;
    bool carry_o = res_neg;

    for (std::size_t i = 0; i < n; ++i) {
        uint_multiprecision_t l = i < lhs.size() ? lhs[i] : uint_multiprecision_t{0};
        uint_multiprecision_t r = i < rhs.size() ? rhs[i] : uint_multiprecision_t{0};

        if constexpr (neg_left) {
            l                 = ~l;
            auto [sum, carry] = carrying_add(l, uint_multiprecision_t{0}, carry_l);
            l                 = sum;
            carry_l           = carry;
        }
        if constexpr (neg_right) {
            r                 = ~r;
            auto [sum, carry] = carrying_add(r, uint_multiprecision_t{0}, carry_r);
            r                 = sum;
            carry_r           = carry;
        }

        uint_multiprecision_t res = eval_bitwise<op>(l, r);
        if constexpr (res_neg) {
            res               = ~res;
            auto [sum, carry] = carrying_add(res, uint_multiprecision_t{0}, carry_o);
            res               = sum;
            carry_o           = carry;
        }
        out[i] = res;
    }

    if constexpr (res_neg) {
        if (carry_o) {
            out[n] = uint_multiprecision_t{1};
            return true; // Wrote n+1 limbs.
        }
    }
    return false; // Exactly n limbs.
}
} // namespace detail

// [big.int.numeric], non-member numeric functions (defined in <beman/big_int/numeric.hpp>).
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A> abs(const basic_big_int<b, L, A>& j);
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A> abs(basic_big_int<b, L, A>&& j) noexcept;

// `gcd` and `midpoint` are each a set of overloads in <beman/big_int/numeric.hpp>;
// they all run through these drivers, which are the ones that need the representation.
namespace detail {
template <class M, class N>
constexpr common_big_int_type<M, N> gcd_impl(M&& m, N&& n);
template <class M, class N>
constexpr common_big_int_type<M, N> midpoint_impl(M&& m, N&& n);

// Likewise, the two `to_chars` overloads in <beman/big_int/charconv.hpp> share this
// driver. `X` deduces to `const basic_big_int&` for the lvalue overload and to
// `basic_big_int` for the rvalue one.
template <class X>
constexpr std::to_chars_result to_chars_impl(char* begin, char* end, X&& x, int base);
} // namespace detail

// [big.int.class], class template basic_big_int
template <std::size_t min_inplace_bits, class Limb, class Allocator>
class BEMAN_BIG_INT_TRIVIAL_ABI basic_big_int {

    // The limb type is a template parameter so that a future revision can change it without
    // changing the signature of basic_big_int. Only uint_multiprecision_t is supported today:
    // the compiled multiplication and division kernels in src/ have a fixed-limb ABI.
    static_assert(std::is_same_v<Limb, uint_multiprecision_t>, "Limb must be uint_multiprecision_t.");

    using limb_type        = Limb;
    using signed_limb_type = detail::int_multiprecision_t;

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT
    using double_limb_type        = detail::uint_wide_t;
    using signed_double_limb_type = detail::int_wide_t;
#endif

  public:
    using allocator_type = Allocator;
    using size_type      = std::size_t;
    using pointer        = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer  = typename std::allocator_traits<Allocator>::const_pointer;
    static_assert(std::is_same_v<typename Allocator::value_type, limb_type>,
                  "Allocator::value_type must be the limb type.");

    template <std::size_t, class, class>
    friend class basic_big_int;

    // This spells the limb parameter `L` rather than `Limb`: a member template may not
    // redeclare a template parameter of the enclosing class ([temp.local]).
    template <std::size_t b, class L, class A>
    friend constexpr std::from_chars_result from_chars(const char*, const char*, basic_big_int<b, L, A>&, int);

    // Both `to_chars` overloads render through this one driver, so it is the only
    // part of the output side that needs the representation.
    template <class X>
    friend constexpr std::to_chars_result detail::to_chars_impl(char*, char*, X&&, int);

    friend struct ::std::hash<basic_big_int>;

  private:
    using alloc_traits = std::allocator_traits<Allocator>;
    using alloc_result = detail::allocation_result<pointer>;

    static constexpr size_type bits_per_limb = detail::width_v<limb_type>;

    // Representation limits, reported publicly by max_representation_size() and max_size().
    static constexpr size_type max_limbs =
        std::min((size_type{1} << 31U) - 1U, std::numeric_limits<size_type>::max() / bits_per_limb);
    static constexpr size_type max_bits = max_limbs * bits_per_limb;

  public:
    // Never fewer limbs than would fit in the pointer footprint  of the union,
    // so the union doesn't waste space.
    static constexpr size_type inplace_capacity = std::max(detail::div_to_pos_inf(min_inplace_bits, bits_per_limb),
                                                           detail::div_to_pos_inf(sizeof(pointer), sizeof(limb_type)));
    static_assert(min_inplace_bits > 0);
    static_assert(inplace_capacity > 0);
    static constexpr size_type inplace_bits = inplace_capacity * bits_per_limb;

  private:
    union data_type {
        pointer   data;
        limb_type limbs[inplace_capacity];

        constexpr data_type() noexcept : limbs{} {}
    };

    std::uint32_t                                  m_capacity;      // 0 = static storage, >0 = heap capacity
    std::uint32_t                                  m_size_and_sign; // bit 31 = sign, bits 0-30 = limb count
    data_type                                      m_storage;
    BEMAN_BIG_INT_NO_UNIQUE_ADDRESS allocator_type m_alloc;

    // Internal accessors for the packed representation
    [[nodiscard]] constexpr bool          is_representation_inplace() const noexcept;
    [[nodiscard]] constexpr std::uint32_t limb_count() const noexcept;
    // Returns `true` if the integer value is less than zero, otherwise `false`.
    [[nodiscard]] constexpr bool is_negative() const noexcept;
    // Returns `true` if the integer value is zero, otherwise `false`.
    [[nodiscard]] constexpr bool is_zero() const noexcept;
    // Like `is_zero`, but bypasses any access to the sign bit.
    // This function is safe to call even if the object is in a corrupt negative zero state.
    [[nodiscard]] constexpr bool unchecked_is_magnitude_zero() const noexcept;
    // Sets the limb count to a nonzero number `n`.
    // This can possibly create a non-canonical representation containing trailing zeros.
    // No check or trim is performed to prevent trailing zeros.
    constexpr void unchecked_set_limb_count(std::uint32_t n) noexcept;
    // Sets the value of the sign bit to `s`,
    // meaning that the integer value becomes negative if `s` is `true`.
    // This can possibly result in a corrupt negative zero state.
    constexpr void unchecked_set_sign(bool s) noexcept;
    // Canonicalizes the magnitude by trimming any most significant zeroes.
    // This does not modify the contents of the representation,
    // only the `limb_count()` is equal to `1` after calling this function if the integer value is zero.
    // This function is safe to call on negative zero; the sign bit is not affected.
    constexpr void                                           unchecked_trim_magnitude() noexcept;
    constexpr void                                           negate() noexcept;
    constexpr void                                           set_zero() noexcept;
    [[nodiscard]] constexpr limb_type*                       limb_ptr() noexcept;
    [[nodiscard]] constexpr const limb_type*                 limb_ptr() const noexcept;
    [[nodiscard]] constexpr std::span<uint_multiprecision_t> limb_span() noexcept;

  public:
    // [big.int.cons], construct/copy/destroy
    constexpr basic_big_int() noexcept(noexcept(Allocator())) : m_capacity{0}, m_size_and_sign{1}, m_storage{} {}
    constexpr explicit basic_big_int(const Allocator& a) noexcept
        : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {}
    constexpr basic_big_int(const basic_big_int& x);
    constexpr basic_big_int(basic_big_int&& x) noexcept;
    constexpr basic_big_int(const basic_big_int& x, const std::type_identity_t<Allocator>& a);
    constexpr basic_big_int(basic_big_int&& x, const std::type_identity_t<Allocator>& a);

    // Defined inline: MSVC cannot match out-of-line definitions
    // of constructors with conditional explicit + requires.
    template <detail::arbitrary_arithmetic T>
        requires(!std::same_as<std::remove_cvref_t<T>, basic_big_int>)
    constexpr explicit(!detail::is_implicit_constructible_from<inplace_bits, limb_type, Allocator, T>)
        basic_big_int(T&& value) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>)
        : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{} {
        if constexpr (std::is_floating_point_v<std::remove_cvref_t<T>>) {
            assign_from_float(value);
        } else if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
            const auto count = value.limb_count();
            grow(count);
            auto* dst = limb_ptr();
            std::copy_n(value.limb_ptr(), count, dst);
            for (size_type i = count; i < limb_count(); ++i) {
                dst[i] = 0;
            }
            unchecked_set_limb_count(count);
            unchecked_set_sign(value.is_negative());
        } else {
            if constexpr (detail::signed_integer<std::remove_cvref_t<T>>) {
                unchecked_set_sign(value < std::remove_cvref_t<T>{0});
                assign_magnitude(detail::uabs(value));
            } else {
                assign_magnitude(value);
            }
        }
    }

    template <detail::arbitrary_arithmetic T>
    constexpr basic_big_int(const T&              value,
                            const allocator_type& a) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>);

    template <std::input_iterator I, std::sentinel_for<I> S>
        requires detail::signed_or_unsigned<std::iter_value_t<I>>
    constexpr basic_big_int(I begin, S end, const allocator_type& a = allocator_type());

#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_CONTAINERS_RANGES
    template <std::ranges::input_range R>
        requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
    constexpr basic_big_int(std::from_range_t, R&& r, const allocator_type& a = allocator_type())
        : basic_big_int(std::ranges::begin(r), std::ranges::end(r), a) {}
#endif

    constexpr ~basic_big_int();

    // [big.int.modifiers]
    constexpr basic_big_int& operator=(const basic_big_int& x);
    constexpr basic_big_int& operator=(basic_big_int&& x) noexcept;

    // Defined inline: see note above
    template <detail::arbitrary_integer T>
        requires(!std::same_as<std::remove_cvref_t<T>, basic_big_int>)
    constexpr basic_big_int& operator=(T&& x) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>) {
        if constexpr (detail::is_basic_big_int_v<T>) {
            const auto count = x.limb_count();
            grow(count);
            auto* dst = limb_ptr();
            std::copy_n(x.limb_ptr(), count, dst);
            for (size_type i = count; i < limb_count(); ++i) {
                dst[i] = 0;
            }
            unchecked_set_limb_count(count);
            unchecked_set_sign(x.is_negative());
        } else {
            using U                 = detail::make_unsigned_t<std::remove_cvref_t<T>>;
            const bool          neg = detail::signed_integer<std::remove_cvref_t<T>> && x < std::remove_cvref_t<T>{0};
            const U             mag = neg ? static_cast<U>(U{0} - static_cast<U>(x)) : static_cast<U>(x);
            constexpr size_type n   = detail::div_to_pos_inf(sizeof(U), sizeof(limb_type));
            grow(n);
            const auto old_count = limb_count();
            assign_magnitude(mag);
            auto* dst = limb_ptr();
            for (size_type i = limb_count(); i < old_count; ++i) {
                dst[i] = 0;
            }
            unchecked_set_sign(neg);
        }
        return *this;
    }

    // TODO(alcxpr): compound operators

    template <detail::signed_or_unsigned S>
    constexpr basic_big_int& operator>>=(S s);
    template <detail::signed_or_unsigned S>
    constexpr basic_big_int& operator<<=(S s);

    template <class T>
    constexpr basic_big_int& operator+=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator-=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator*=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator/=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator%=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;

    template <class T>
    constexpr basic_big_int& operator&=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator|=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator^=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;

    constexpr void
    swap(basic_big_int& x) noexcept(std::allocator_traits<Allocator>::propagate_on_container_swap::value ||
                                    std::allocator_traits<Allocator>::is_always_equal::value);

    // [big.int.ops]
    [[nodiscard]] constexpr std::span<const uint_multiprecision_t> representation() const noexcept;
    [[nodiscard]] constexpr size_type                              representation_size() const noexcept;
    [[nodiscard]] constexpr allocator_type                         get_allocator() const noexcept;
    [[nodiscard]] constexpr size_type                              size() const noexcept;
    [[nodiscard]] constexpr size_type                              max_size() const noexcept;
    [[nodiscard]] constexpr size_type                              max_representation_size() const noexcept;
    constexpr void                                                 reserve(size_type n);
    constexpr void                                                 reserve_representation(size_type n);
    [[nodiscard]] constexpr size_type                              capacity() const noexcept;
    [[nodiscard]] constexpr size_type                              representation_capacity() const noexcept;
    constexpr void                                                 shrink_to_fit();

    // [big.int.unary]
    [[nodiscard]] constexpr basic_big_int operator+() const&;
    [[nodiscard]] constexpr basic_big_int operator+() && noexcept;
    [[nodiscard]] constexpr basic_big_int operator-() const&;
    [[nodiscard]] constexpr basic_big_int operator-() && noexcept;
    [[nodiscard]] constexpr basic_big_int operator~() const&;
    [[nodiscard]] constexpr basic_big_int operator~() &&;

    constexpr basic_big_int& operator++();
    constexpr basic_big_int  operator++(int);
    constexpr basic_big_int& operator--();
    constexpr basic_big_int  operator--(int);

    // [big.int.cmp]
    template <class L, detail::common_big_int_type_with<L> R>
    friend constexpr bool operator==(const L& lhs, const R& rhs) noexcept;
    template <class L, detail::common_big_int_type_with<L> R>
    friend constexpr std::strong_ordering operator<=>(const L& lhs, const R& rhs) noexcept;

    // [big.int.binary]
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator+(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator-(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator*(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator/(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator%(L&& x, R&& y);

    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator&(L&& x, R&& y);

    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator|(L&& x, R&& y);

    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator^(L&& x, R&& y);

    template <class T, detail::signed_or_unsigned S>
        requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
    friend constexpr std::remove_cvref_t<T> operator<<(T&& x, S s);
    template <class T, detail::signed_or_unsigned S>
        requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
    friend constexpr std::remove_cvref_t<T> operator>>(T&& x, S s);

    // [big.int.conv], conversions
    template <detail::cv_unqualified_arithmetic T>
    [[nodiscard]] constexpr explicit operator T() const noexcept {
        return to<T>();
    }

    // [big.int.div], division
    template <class L, class R>
    friend constexpr div_result<detail::common_big_int_type<L, R>> div_rem_to_zero(L&&, R&&);

    // [big.int.numeric], non-member numeric functions
    template <std::size_t b, class L, class A>
    friend constexpr basic_big_int<b, L, A> abs(const basic_big_int<b, L, A>& j);
    template <std::size_t b, class L, class A>
    friend constexpr basic_big_int<b, L, A> abs(basic_big_int<b, L, A>&& j) noexcept;

    template <class M, class N>
    friend constexpr detail::common_big_int_type<M, N> detail::gcd_impl(M&& m, N&& n);

    template <class M, class N>
    friend constexpr detail::common_big_int_type<M, N> detail::midpoint_impl(M&& m, N&& n);

  private:
    template <detail::unsigned_integer T>
    constexpr void assign_magnitude(T value) noexcept;
    template <detail::cv_unqualified_floating_point F>
    constexpr void assign_from_float(F value) noexcept;

    // Throws `std::length_error` if `limbs_needed` limbs cannot be represented
    static constexpr void                       check_length(size_type limbs_needed);
    [[nodiscard]] static constexpr alloc_result alloc_limbs_from(allocator_type& a, size_type n);
    [[nodiscard]] constexpr alloc_result        alloc_limbs(size_type n);
    constexpr void                              free_limbs(pointer p, size_type n);
    constexpr void                              free_storage();
    constexpr void                              grow(size_type limbs_needed);
    // Ensures room for `n` limbs and discards the contents (no copy, no zero-fill). Does not change the limb
    // count or sign. The caller writes the limbs, sets the count, and zeroes any in-place tail
    // (see `clear_inline_tail`). No operand of the caller may alias `*this`.
    [[nodiscard]] constexpr limb_type* storage_for_overwrite(size_type n);
    // For in-place storage, zeroes the limbs in [limb_count(), old_count). Keeps the invariant
    // that in-place limbs past the limb count are zero, which `inplace_to_bit_uint` relies on.
    constexpr void clear_inline_tail(size_type old_count) noexcept;
    constexpr void copy_n_to_allocation(const limb_type* p, size_type n, alloc_result out);
    // Copies the limbs of `x` into a freshly constructed `*this` whose control word already matches `x`.
    constexpr void copy_limbs_from(const basic_big_int& x);
    constexpr void push_back_limb(limb_type limb);

    // We are limited in our shifting to what we can encode into our control block, which is 30 (or 27) bits of limbs
    // Our max shift is then the number of bits represented in these blocks plus the theoretical 63 (or 31)
    // that are in the same limb.
    using shift_type                      = uint_multiprecision_t;
    static constexpr shift_type shift_max = static_cast<shift_type>(max_limbs) * bits_per_limb;

    // Increases the magnitude by one, without affecting the sign bit.
    // Returns `true` on carry in the uppermost limb.
    constexpr bool unchecked_increment_magnitude();
    // Decreases the magnitude by one.
    // If the value was zero prior to the operation,
    // the magnitude is set to `1`.
    // Returns `true` on borrow in the uppermost limb,
    // meaning that the value was originally zero.
    constexpr bool unchecked_decrement_magnitude();

    constexpr void shift_left(shift_type s);
    constexpr void shift_right(shift_type s);
    // `*this = (src, neg) << s` / `>> s` (floor rounding) in a single pass. `src` must be canonical and
    // must not alias `*this`.
    constexpr void assign_shifted_left(std::span<const uint_multiprecision_t> src, bool neg, shift_type s);
    constexpr void assign_shifted_right(std::span<const uint_multiprecision_t> src, bool neg, shift_type s);

    template <detail::signed_or_unsigned Integer>
    [[nodiscard]] constexpr bool equals_integer(Integer x) const noexcept;
    [[nodiscard]] constexpr bool equals_big_int(const basic_big_int& x) const noexcept;
    template <std::size_t extent>
    [[nodiscard]] constexpr bool equals_limbs(std::span<const uint_multiprecision_t, extent> limbs,
                                              bool limbs_negative) const noexcept;

    template <detail::signed_or_unsigned Integer>
    [[nodiscard]] constexpr std::strong_ordering compare_integer(Integer x) const noexcept;
    [[nodiscard]] constexpr std::strong_ordering compare_big_int(const basic_big_int& x) const noexcept;
    template <std::size_t extent>
    [[nodiscard]] constexpr std::strong_ordering compare_limbs(std::span<const uint_multiprecision_t, extent> limbs,
                                                               bool limbs_negative) const noexcept;

    // Adds `(other, other_neg)` into `*this` in place. Shared core for `operator+`
    // and `operator-`: the caller chooses the destination (an rvalue operand's
    // storage or a copy of an lvalue operand) and supplies the other side as a limb
    // span + sign. Only allocates when the result genuinely requires more limbs
    // than the current capacity. Preserves the no-negative-zero and trimmed-top-limb
    // invariants.
    template <std::size_t extent_other>
    constexpr void add_in_place(std::span<const uint_multiprecision_t, extent_other> other, bool other_neg);

    // Computes `(a, a_neg) + (b, b_neg)` directly into `*this`.
    // Fuses copy and potential second allocation
    // Precondition: `a.size() >= b.size()`
    template <std::size_t extent_a, std::size_t extent_b>
    constexpr void add_into(std::span<const uint_multiprecision_t, extent_a> a,
                            bool                                             a_neg,
                            std::span<const uint_multiprecision_t, extent_b> b,
                            bool                                             b_neg);

    // Computes `a * b` and stores the result into `*this`.
    template <std::size_t extent_a, std::size_t extent_b>
    constexpr void multiply_into(std::span<const uint_multiprecision_t, extent_a> a,
                                 bool                                             a_neg,
                                 std::span<const uint_multiprecision_t, extent_b> b,
                                 bool                                             b_neg);

    // Computes the quotient, remainder, or both, of an integer division, depending on `op`.
    // If `op` is `div` or `rem`, this object is set to the desired result,
    // and the returned value is unspecified.
    // If `op` is `div_rem`, this object is set to the quotient, and the remainder is returned.
    //
    // In any case, the division is rounded towards zero (i.e. it is truncating),
    // same as the division between fundamental types.
    template <std::size_t extent_a, std::size_t extent_b>
    constexpr basic_big_int divmod_into(std::span<const uint_multiprecision_t, extent_a> dividend,
                                        bool                                             dividend_neg,
                                        std::span<const uint_multiprecision_t, extent_b> divisor,
                                        bool                                             divisor_neg,
                                        detail::division_op                              op);

    // Single-limb divisor fast path.
    // Avoids limb-vector promotion and allocation of a scratch remainder buffer.
    template <std::size_t extent_a>
    constexpr uint_multiprecision_t divmod_into_short(std::span<const uint_multiprecision_t, extent_a> dividend,
                                                      bool                                             dividend_neg,
                                                      uint_multiprecision_t                            divisor,
                                                      bool                                             divisor_neg,
                                                      detail::division_op                              op);

    // Single-limb divisor in-place fast path.
    // Replaces `*this` with the quotient (for `div` / `div_rem`) or with the
    // remainder (for `rem`), and returns the scalar remainder.
    // Does not allocate.
    constexpr uint_multiprecision_t
    divmod_in_place_short(uint_multiprecision_t divisor, bool divisor_neg, detail::division_op op);

    // `*this *= b`, keeping the allocator. `b` may alias `*this`. A one-limb `b` multiplies in place; a product
    // of at most 64 limbs is formed on the stack at run time; larger products go to a fresh buffer that replaces
    // the old one only on success.
    template <std::size_t extent_b>
    constexpr void multiply_in_place(std::span<const uint_multiprecision_t, extent_b> b, bool b_neg);

    // `*this /= divisor` or `*this %= divisor` for a multi-limb divisor in the schoolbook band, at run
    // time and with all scratch on the stack: the dividend is divided where it sits, so nothing allocates.
    // Returns false, leaving `*this` untouched, when the shape does not qualify.
    template <std::size_t extent_b>
    constexpr bool divide_in_place_small(std::span<const uint_multiprecision_t, extent_b> divisor,
                                         bool                                             divisor_neg,
                                         detail::division_op                              op);

    // A value holding exactly the trimmed magnitude `mag` (non-empty), with allocator `a`.
    [[nodiscard]] static constexpr basic_big_int
    make_from_magnitude(std::span<const uint_multiprecision_t> mag, bool neg, const allocator_type& a);

    // Shared implementation behind copy-assign, move-assign, and the lvalue
    // branches of `operator+` / `operator-`.
    // Sets `*this` to the value of `src`.
    //
    // An rvalue heap `src` is stolen (its buffer adopted, `src` left as inline zero) whenever our
    // allocator can free it, i.e. the allocator propagates, is always-equal, or compares equal to
    // `src`'s, regardless of the capacity we already hold. Our own buffer is released first, with
    // our allocator. Every other case copies, reusing the existing allocation when it already
    // fits `src.limb_count() + extra_space` limbs. `extra_space` lets callers that know they are
    // about to grow by a fixed amount (e.g., a carry-out of one limb) reserve that space up front;
    // it must be 0 for an rvalue `src`.
    //
    // When a propagating allocator compares unequal to ours, our heap buffer cannot be kept: it is
    // released through the old allocator before the new one is adopted.
    template <detail::allocator_propagation propagation = detail::allocator_propagation::propagate, class Src>
        requires std::same_as<std::remove_cvref_t<Src>, basic_big_int>
    constexpr void assign_value(Src&& src, const std::size_t extra_space = 0) {
        if (std::addressof(*this) == std::addressof(src)) {
            // `assign_value(std::move(*this), ...)` is a no-op. Also guards the
            // self-aliasing the existing `operator=` overloads protected against.
            return;
        }

        // Allocator propagation follows `std::allocator_traits`. Stateful allocators
        // such as `std::pmr::polymorphic_allocator` have a deleted copy/move
        // assignment operator, so any `m_alloc = ...` must be guarded by
        // `propagate_on_container_*_assignment`. The relevant trait is picked
        // based on `Src`'s value category, and `std::forward<Src>` then
        // produces an rvalue or lvalue allocator to match -- so move- vs
        // copy-assign of `m_alloc` does not need to be spelled out separately.
        constexpr bool propagate_alloc =
            propagation == detail::allocator_propagation::propagate &&
            (std::is_lvalue_reference_v<Src> ? alloc_traits::propagate_on_container_copy_assignment::value
                                             : alloc_traits::propagate_on_container_move_assignment::value);

        if constexpr (!std::is_lvalue_reference_v<Src>) {
            BEMAN_BIG_INT_DEBUG_ASSERT(extra_space == 0);
            if (!src.is_representation_inplace() &&
                (propagate_alloc || alloc_traits::is_always_equal::value || m_alloc == src.m_alloc)) {
                free_storage();
                if constexpr (propagate_alloc) {
                    m_alloc = std::forward<Src>(src).m_alloc;
                }
                m_capacity          = src.m_capacity;
                m_storage.data      = src.m_storage.data;
                m_size_and_sign     = src.m_size_and_sign;
                src.m_capacity      = 0;
                src.m_size_and_sign = 1;
                src.m_storage       = {};
                return;
            }
            // Inline `src`, or a heap `src` that our allocator cannot free: copy below.
        }

        const std::size_t src_count = src.limb_count();
        const std::size_t needed    = src_count + extra_space;
        const std::size_t eff_cap   = is_representation_inplace() ? inplace_capacity : m_capacity;

        // Keeping our heap buffer is only valid if our allocator stays the one that frees it.
        const bool keeps_buffer = [&] {
            if constexpr (propagate_alloc && !alloc_traits::is_always_equal::value) {
                return is_representation_inplace() || m_alloc == src.m_alloc;
            } else {
                return true;
            }
        }();

        if (keeps_buffer && needed <= eff_cap) {
            // Fast path: current buffer is already big enough
            const auto old_count = limb_count();
            m_size_and_sign      = src.m_size_and_sign;
            if constexpr (propagate_alloc) {
                m_alloc = std::forward<Src>(src).m_alloc;
            }
            limb_type* const       dst_limbs = limb_ptr();
            const limb_type* const src_limbs = src.limb_ptr();
            std::copy_n(src_limbs, src_count, dst_limbs);
            clear_inline_tail(old_count);
            return;
        }

        // Slow path. Each branch releases our buffer with our current allocator, before any
        // propagation, and publishes the control words only once the storage they describe is in place.
        if (src.is_representation_inplace() && needed <= inplace_capacity) {
            // Both src and the requested headroom fit inline. No buffer to
            // allocate; release ours (if any), propagate, and copy limbs.
            free_storage();
            if constexpr (propagate_alloc) {
                m_alloc = std::forward<Src>(src).m_alloc;
            }
            m_capacity      = 0;
            m_size_and_sign = src.m_size_and_sign;
            for (std::size_t i = 0; i < inplace_capacity; ++i) {
                m_storage.limbs[i] = src.m_storage.limbs[i];
            }
            return;
        }

        // Fresh allocation of `needed` limbs. Secure it before releasing ours, so a throwing allocation
        // leaves `*this` unchanged. A propagating allocator has to serve the new block while ours still
        // has to release the old, so allocate through a copy of `src`'s.
        const alloc_result allocation = [&] {
            if constexpr (propagate_alloc) {
                allocator_type src_alloc(src.m_alloc);
                return alloc_limbs_from(src_alloc, needed);
            } else {
                return alloc_limbs(needed);
            }
        }();
        copy_n_to_allocation(src.limb_ptr(), src_count, allocation);

        free_storage();
        if constexpr (propagate_alloc) {
            m_alloc = std::forward<Src>(src).m_alloc;
        }
        m_capacity      = static_cast<std::uint32_t>(allocation.count);
        m_storage.data  = allocation.ptr;
        m_size_and_sign = src.m_size_and_sign;
    }

    // Efficiently performs `*this |= bits << offset`, as if `bits` was wrapped in `basic_big_int`.
    // The behavior is undefined there are any existing nonzero bits overwritten.
    constexpr void unchecked_init_magnitude_bits_at(const uint_multiprecision_t bits, const size_type offset) {
        const size_type limb_offset = offset / bits_per_limb;
        const size_type bit_offset  = offset % bits_per_limb;
        if (bit_offset == 0) {
            grow(limb_offset + 1);
            BEMAN_BIG_INT_DEBUG_ASSERT(limb_ptr()[limb_offset] == 0);
            limb_ptr()[limb_offset] = bits;
            if (limb_count() < limb_offset + 1) {
                unchecked_set_limb_count(static_cast<std::uint32_t>(limb_offset + 1));
            }
        } else {
            grow(limb_offset + 2);
            limb_ptr()[limb_offset + 0] |= bits << bit_offset;
            limb_ptr()[limb_offset + 1] |= bits >> (bits_per_limb - bit_offset);
            const size_type hi_limb = (bits >> (bits_per_limb - bit_offset)) != 0 ? limb_offset + 2 : limb_offset + 1;
            if (limb_count() < hi_limb) {
                unchecked_set_limb_count(static_cast<std::uint32_t>(hi_limb));
            }
        }
    }

    // Returns the bits at the specified bit offset.
    // Bits from at most two limbs are fetched via funnel-shift.
    [[nodiscard]] constexpr uint_multiprecision_t get_bits_at(const size_type offset) const {
        const auto limb_offset = offset / bits_per_limb;
        const auto bit_offset  = static_cast<unsigned int>(offset % bits_per_limb);
        BEMAN_BIG_INT_ASSERT(limb_offset < limb_count());
        const detail::wide<uint_multiprecision_t> bits{
            .low_bits  = limb_ptr()[limb_offset],
            .high_bits = limb_offset + 1 == limb_count() ? 0 : limb_ptr()[limb_offset + 1],
        };
        return detail::funnel_shr(bits, bit_offset);
    }

    // If `true`, `inplace_to_bit_uint` may be called.
    // Otherwise, the function is deleted.
    static constexpr bool        has_inplace_to_bit_uint = inplace_bits <= BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_bit_uint() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_bit_uint
    {
        static_assert(std::has_unique_object_representations_v<uint_multiprecision_t>,
                      "_BitInt conversion doesn't work when there is padding.");
        BEMAN_BIG_INT_DEBUG_ASSERT(is_representation_inplace());

        using Result = bit_uint<inplace_bits>;
        if constexpr (inplace_bits == bits_per_limb) {
            // If there is only a single inplace limb,
            // static_cast and std::bit_cast are equivalent.
            // This special case also makes the <<= below safe.
            return static_cast<Result>(m_storage.limbs[0]);
        } else {
            if constexpr (std::endian::native == std::endian::little) {
                if BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT {
                    return std::bit_cast<Result>(m_storage.limbs);
                }
            }
            // Naive fallback implementation always works.
            // This is needed for big endian and when neither static_cast nor bit_cast work.
            // The limbs run least-significant first, so they are folded in from the top down.
            Result result = 0;
            for (size_type i = inplace_capacity; i-- > 0;) {
                result <<= bits_per_limb;
                result |= m_storage.limbs[i];
            }
            return result;
        }
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    // If `true`, `inplace_to_wide_bit_uint` may be called.
    // Otherwise, the function is deleted.
    static constexpr bool        has_inplace_to_wide_bit_uint = inplace_bits * 2 <= BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_wide_bit_uint() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_wide_bit_uint
    {
        static_assert(std::has_unique_object_representations_v<uint_multiprecision_t>,
                      "_BitInt conversion doesn't work when there is padding.");
        return static_cast<bit_uint<2 * inplace_bits>>(inplace_to_bit_uint());
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    // If `true`, `inplace_to_bit_sint` may be called.
    // Otherwise, the function is deleted.
    static constexpr bool        has_inplace_to_bit_sint = inplace_bits < BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_sbit_int() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_bit_sint
    {
        static_assert(std::has_unique_object_representations_v<uint_multiprecision_t>,
                      "_BitInt conversion doesn't work when there is padding.");
        // Use `inplace_bits + 1` to avoid signed overflow when negating a value
        // with the high bit set.
        const auto mag = static_cast<bit_int<inplace_bits + 1>>(inplace_to_bit_uint());
        return is_negative() ? -mag : mag;
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    template <detail::cv_unqualified_arithmetic T, bool ignore_sign = false>
    [[nodiscard]] constexpr T to() const noexcept;

    template <detail::bitwise_op op, class T>
    constexpr basic_big_int& bitwise_assign_impl(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;

    // `alloc` is the result's allocator, from `detail::result_allocator`.
    template <detail::bitwise_op op, bool neg_left, bool neg_right, std::size_t extent_a, std::size_t extent_b>
    [[nodiscard]] static constexpr basic_big_int
    make_bitwise_of_limbs(std::span<const uint_multiprecision_t, extent_a> lhs,
                          std::span<const uint_multiprecision_t, extent_b> rhs,
                          const allocator_type&                            alloc);

    template <detail::bitwise_op op, std::size_t extent_a, std::size_t extent_b>
    [[nodiscard]] static constexpr basic_big_int
    dispatch_bitwise(const std::span<const uint_multiprecision_t, extent_a> lhs,
                     const bool                                             lhs_neg,
                     const std::span<const uint_multiprecision_t, extent_b> rhs,
                     const bool                                             rhs_neg,
                     const allocator_type&                                  alloc);

    template <detail::bitwise_op op, class L, class R>
    [[nodiscard]] static constexpr detail::common_big_int_type<L, R> bitwise_impl(L&& x, R&& y);

    // In-place two's-complement bitwise op: *this OP= (other_limbs, other_neg).
    template <detail::bitwise_op op, std::size_t extent>
    constexpr void bitwise_in_place(std::span<const uint_multiprecision_t, extent> other, bool other_neg);
};

// =============================================================================
// Out-of-class definitions
// =============================================================================

// Internal accessors

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::is_representation_inplace() const noexcept {
    return m_capacity == 0;
}

template <std::size_t b, class L, class A>
constexpr std::uint32_t basic_big_int<b, L, A>::limb_count() const noexcept {
    constexpr std::uint32_t negative_zero_size_and_sign = 0x8000'0000U;
    BEMAN_BIG_INT_DEBUG_ASSERT(m_size_and_sign != negative_zero_size_and_sign);
    return m_size_and_sign & 0x7FFF'FFFFU;
}

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::is_negative() const noexcept {
    constexpr std::uint32_t negative_zero_size_and_sign = 0x8000'0000U;
    BEMAN_BIG_INT_DEBUG_ASSERT(m_size_and_sign != negative_zero_size_and_sign);
    return (m_size_and_sign & 0x8000'0000U) != 0;
}

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::is_zero() const noexcept {
    // We have the invariant that the sign bit is never set for zero magnitude,
    // so negative numbers short-circuit here.
    return !is_negative() && unchecked_is_magnitude_zero();
}

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::unchecked_is_magnitude_zero() const noexcept {
    const std::uint32_t unchecked_limb_count = m_size_and_sign & 0x7FFF'FFFFU;
    const limb_type*    limbs                = limb_ptr();
    for (std::uint32_t i = 0; i < unchecked_limb_count; ++i) {
        if (limbs[i] != 0) {
            return false;
        }
    }
    return true;
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::unchecked_set_limb_count(const std::uint32_t n) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0);
    m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | n;
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::unchecked_set_sign(const bool s) noexcept {
    m_size_and_sign = (m_size_and_sign & 0x7FFF'FFFFU) | (static_cast<std::uint32_t>(s) << 31);
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::negate() noexcept {
    if (!is_zero()) {
        m_size_and_sign ^= 0x8000'0000U;
    }
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::set_zero() noexcept {
    limb_type* const limbs = limb_ptr();
    limbs[0]               = 0;
    if constexpr (inplace_capacity != 1) {
        // Keep "inline limbs past the limb count are zero" (see `clear_inline_tail`).
        if (is_representation_inplace()) {
            for (size_type i = 1; i < inplace_capacity; ++i) {
                limbs[i] = 0;
            }
        }
    }
    m_size_and_sign = 1;
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::unchecked_trim_magnitude() noexcept {
    const limb_type* const limbs = limb_ptr();
    std::uint32_t          size  = m_size_and_sign & 0x7FFF'FFFFU;
    if (size > 1 && limbs[size - 1] == 0) {
        do {
            if (limbs[size - 1] != 0) {
                break;
            }
        } while (--size > 1);
        m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | size;
    }
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::limb_ptr() noexcept -> limb_type* {
    return is_representation_inplace() ? m_storage.limbs : m_storage.data;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::limb_ptr() const noexcept -> const limb_type* {
    return is_representation_inplace() ? m_storage.limbs : m_storage.data;
}

template <std::size_t b, class L, class A>
constexpr std::span<uint_multiprecision_t> basic_big_int<b, L, A>::limb_span() noexcept {
    return {limb_ptr(), limb_count()};
}

// [big.int.cons] — constructors

template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>::basic_big_int(const basic_big_int& x)
    : basic_big_int(x, alloc_traits::select_on_container_copy_construction(x.get_allocator())) {}

template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>::basic_big_int(basic_big_int&& x) noexcept
    : m_capacity{x.m_capacity}, m_size_and_sign{x.m_size_and_sign}, m_storage{}, m_alloc{std::move(x.m_alloc)} {
    if (x.is_representation_inplace()) {
        for (size_type i = 0; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = x.m_storage.limbs[i];
        }
    } else {
        m_storage.data    = x.m_storage.data;
        x.m_capacity      = 0;
        x.m_size_and_sign = 1;
        x.m_storage       = {};
    }
}

template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>::basic_big_int(const basic_big_int& x, const std::type_identity_t<A>& a)
    : m_capacity{0}, m_size_and_sign{x.m_size_and_sign}, m_storage{}, m_alloc{a} {
    copy_limbs_from(x);
}

// Takes over `x`'s buffer only when `a` can free it, i.e. compares equal to
// `x.get_allocator()`. Otherwise the limbs are copied into storage obtained
// from `a`, as the allocator-extended copy constructor does, and `x` keeps its value.
template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>::basic_big_int(basic_big_int&& x, const std::type_identity_t<A>& a)
    : m_capacity{0}, m_size_and_sign{x.m_size_and_sign}, m_storage{}, m_alloc{a} {
    if (x.is_representation_inplace() || !(alloc_traits::is_always_equal::value || m_alloc == x.m_alloc)) {
        copy_limbs_from(x);
    } else {
        m_capacity        = x.m_capacity;
        m_storage.data    = x.m_storage.data;
        x.m_capacity      = 0;
        x.m_size_and_sign = 1;
        x.m_storage       = {};
    }
}

template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>& basic_big_int<b, L, A>::operator=(const basic_big_int& x) {
    assign_value(x);
    return *this;
}

template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>& basic_big_int<b, L, A>::operator=(basic_big_int&& x) noexcept {
    // A heap `x` whose buffer our allocator can free (propagating, always-equal, or equal) is
    // stolen whatever our capacity, leaving `x` as inline zero. Otherwise `x` is copied into the
    // existing storage, which allocates only if it does not fit. That copy needs an unequal,
    // non-propagating allocator (e.g. a different pmr resource); it is the one case in which this
    // `noexcept` operator can throw, which terminates.
    assign_value(std::move(x));
    return *this;
}

// Exchanges the value and storage of `*this` and `x` without allocating or
// deallocating. Two inline operands swap their limbs, two heap operands swap
// their buffer pointers, and a mixed pair relocates the inline limbs into the
// heap operand's storage while the freed pointer is adopted by the other.
// The allocator participates only when `propagate_on_container_swap` holds;
// otherwise [container.reqmts] requires the two allocators to compare equal.
template <std::size_t b, class L, class A>
constexpr void
basic_big_int<b, L, A>::swap(basic_big_int& x) noexcept(std::allocator_traits<A>::propagate_on_container_swap::value ||
                                                        std::allocator_traits<A>::is_always_equal::value) {
    if (this == std::addressof(x)) {
        return;
    }

    if constexpr (alloc_traits::propagate_on_container_swap::value) {
        using std::swap;
        swap(m_alloc, x.m_alloc);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(alloc_traits::is_always_equal::value || m_alloc == x.m_alloc);
    }

    // Capture the storage models before swapping the control words, since
    // `is_representation_inplace()` reads `m_capacity`.
    const bool this_inplace = is_representation_inplace();
    const bool x_inplace    = x.is_representation_inplace();

    std::swap(m_capacity, x.m_capacity);
    std::swap(m_size_and_sign, x.m_size_and_sign);

    if (this_inplace && x_inplace) {
        for (size_type i = 0; i < inplace_capacity; ++i) {
            std::swap(m_storage.limbs[i], x.m_storage.limbs[i]);
        }
    } else if (!this_inplace && !x_inplace) {
        std::swap(m_storage.data, x.m_storage.data);
    } else {
        // Exactly one operand is inline. Move its limbs into the heap operand's
        // storage and hand the freed pointer to the now-inline operand.
        basic_big_int& inplace_side = this_inplace ? *this : x;
        basic_big_int& heap_side    = this_inplace ? x : *this;
        const pointer  ptr          = heap_side.m_storage.data;
        for (size_type i = 0; i < inplace_capacity; ++i) {
            heap_side.m_storage.limbs[i] = inplace_side.m_storage.limbs[i];
        }
        inplace_side.m_storage.data = ptr;
    }
}

// [big.int.special], specialized algorithms

// Exchanges the values of `x` and `y` as if by `x.swap(y)`. Found by
// argument-dependent lookup, so both an unqualified `swap(a, b)` and the
// `using std::swap; swap(a, b)` idiom that generic code relies on select this
// overload in preference to the move-based `std::swap`.
BEMAN_BIG_INT_EXPORT template <std::size_t b, class L, class A>
constexpr void swap(basic_big_int<b, L, A>& x, basic_big_int<b, L, A>& y) noexcept(noexcept(x.swap(y))) {
    x.swap(y);
}

template <std::size_t b, class L, class A>
template <detail::arbitrary_arithmetic T>
constexpr basic_big_int<b, L, A>::basic_big_int(const T& value, const allocator_type& a) noexcept(
    detail::no_alloc_constructible_from<inplace_bits, T>)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {
    if constexpr (std::is_floating_point_v<std::remove_cvref_t<T>>) {
        assign_from_float(value);
    } else if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        const auto count = value.limb_count();
        grow(count);
        auto* dst = limb_ptr();
        std::copy_n(value.limb_ptr(), count, dst);
        for (size_type i = count; i < limb_count(); ++i) {
            dst[i] = 0;
        }
        unchecked_set_limb_count(count);
        unchecked_set_sign(value.is_negative());
    } else {
        if constexpr (detail::signed_integer<std::remove_cvref_t<T>>) {
            unchecked_set_sign(value < std::remove_cvref_t<T>{0});
            assign_magnitude(detail::uabs(value));
        } else {
            assign_magnitude(value);
        }
    }
}

template <std::size_t b, class L, class A>
template <std::input_iterator I, std::sentinel_for<I> S>
    requires detail::signed_or_unsigned<std::iter_value_t<I>>
constexpr basic_big_int<b, L, A>::basic_big_int(I begin, S end, const allocator_type& a)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {

    if constexpr (std::ranges::sized_range<std::ranges::subrange<I, S>>) {
        reserve_representation(std::ranges::size(std::ranges::subrange(begin, end)));
        std::size_t i   = 0;
        auto* const dst = limb_ptr();
        for (; begin != end; ++begin) {
            using U = detail::make_unsigned_t<std::iter_value_t<I>>;
            std::construct_at(dst + i++, static_cast<limb_type>(static_cast<U>(*begin)));
        }
        if (i == 0) {
            unchecked_set_limb_count(1);
        } else {
            unchecked_set_limb_count(static_cast<std::uint32_t>(i));
            unchecked_trim_magnitude();
        }
    } else {
        for (; begin != end; ++begin) {
            using U = detail::make_unsigned_t<std::iter_value_t<I>>;
            push_back_limb(static_cast<limb_type>(static_cast<U>(*begin)));
        }
        unchecked_trim_magnitude();
    }
}

template <std::size_t b, class L, class A>
constexpr basic_big_int<b, L, A>::~basic_big_int() {
    free_storage();
}

// [big.int.modifiers]

template <std::size_t b, class L, class A>
template <detail::signed_or_unsigned S>
constexpr auto basic_big_int<b, L, A>::operator>>=(const S s) -> basic_big_int& {
    // If this pattern comes up more often, we should consider something like a `safe_cast` utility.
    // This would convert to another type and signal whether the result is exactly representable.
    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<detail::make_unsigned_t<S>>(s) <= shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    }
    shift_right(static_cast<shift_type>(s));
    return *this;
}

template <std::size_t b, class L, class A>
template <detail::signed_or_unsigned S>
constexpr auto basic_big_int<b, L, A>::operator<<=(const S s) -> basic_big_int& {
    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<detail::make_unsigned_t<S>>(s) <= shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    }
    shift_left(static_cast<shift_type>(s));
    return *this;
}

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::unchecked_increment_magnitude() {
    limb_type* const limbs    = limb_ptr();
    bool             carry_in = true;
    for (size_type i = 0; carry_in && i < limb_count(); ++i) {
        const auto [sum, carry] = detail::carrying_add(limbs[i], limb_type{0}, carry_in);
        limbs[i]                = sum;
        carry_in                = carry;
    }
    if (carry_in) {
        reserve_representation(limb_count() + 1);
        limb_ptr()[limb_count()] = limb_type{1};
        unchecked_set_limb_count(limb_count() + 1);
    }
    return carry_in;
}

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::unchecked_decrement_magnitude() {
    limb_type* const limbs     = limb_ptr();
    bool             borrow_in = true;
    for (size_type i = 0; borrow_in && i < limb_count(); ++i) {
        const auto [difference, borrow] = detail::borrowing_sub(limbs[i], limb_type{0}, borrow_in);
        limbs[i]                        = difference;
        borrow_in                       = borrow;
    }

    if (borrow_in) {
        // Getting a borrow after the loop can only happen if the magnitude was zero,
        // meaning that we produce `-1` with this operation.
        BEMAN_BIG_INT_DEBUG_ASSERT(limb_count() != 0);
        limbs[0] = 1;
        unchecked_set_limb_count(1);
    } else {
        unchecked_trim_magnitude();
    }
    return borrow_in;
}

// Zero stays zero. The result is sized exactly: the extra top limb exists only when the top limb's
// leading zeros cannot absorb the bit shift.
template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::shift_left(const shift_type s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    const size_type n = limb_count();
    if (s == 0 || limb_ptr()[n - 1] == 0) {
        return;
    }

    const size_type whole = s / bits_per_limb;
    const unsigned  bits  = static_cast<unsigned>(s % bits_per_limb);
    const bool      extra = bits != 0 && static_cast<unsigned>(std::countl_zero(limb_ptr()[n - 1])) < bits;
    const size_type new_n = n + whole + static_cast<size_type>(extra);

    grow(new_n);
    limb_type* const limbs = limb_ptr();
    if (bits != 0) {
        const limb_type out = detail::lshift_copy(limbs + whole, limbs, n, bits);
        if (extra) {
            limbs[n + whole] = out;
        }
    } else {
        std::copy_backward(limbs, limbs + n, limbs + n + whole);
    }
    std::fill_n(limbs, whole, limb_type{0});
    unchecked_set_limb_count(static_cast<std::uint32_t>(new_n));
}

// Floor division by `2^s`: a negative value whose discarded bits are not all zero gets its magnitude
// incremented after the (truncating) magnitude shift.
template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::shift_right(const shift_type s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    if (s == 0) {
        return;
    }

    const size_type n     = limb_count();
    const size_type whole = s / bits_per_limb;
    const unsigned  bits  = static_cast<unsigned>(s % bits_per_limb);
    const bool      neg   = is_negative();

    if (whole >= n) {
        // Everything is discarded: zero, or -1 for a negative value (which is nonzero, so bits were lost).
        set_zero();
        if (neg) {
            limb_ptr()[0]   = 1;
            m_size_and_sign = 0x8000'0001U;
        }
        return;
    }

    limb_type* const limbs   = limb_ptr();
    bool             inexact = false;
    if (neg) {
        for (size_type i = 0; i < whole && !inexact; ++i) {
            inexact = limbs[i] != 0;
        }
    }

    const size_type new_n = n - whole;
    if (bits != 0) {
        inexact |= detail::rshift_copy(limbs, limbs + whole, new_n, bits) != 0;
    } else if (whole != 0) {
        std::copy(limbs + whole, limbs + n, limbs);
    }

    size_type k = new_n;
    while (k > 1 && limbs[k - 1] == 0) {
        --k;
    }
    m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | static_cast<std::uint32_t>(k);
    clear_inline_tail(n);
    if (neg && inexact) {
        // A magnitude that shifted down to zero is a negative zero here; incrementing makes it -1.
        unchecked_increment_magnitude();
        BEMAN_BIG_INT_DEBUG_ASSERT(is_negative());
    }
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::assign_shifted_left(const std::span<const uint_multiprecision_t> src,
                                                           const bool                                   neg,
                                                           const shift_type                             s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    const size_type n = src.size();
    if (src[n - 1] == 0) {
        set_zero();
        return;
    }

    const size_type old_count = limb_count();
    const size_type whole     = s / bits_per_limb;
    const unsigned  bits      = static_cast<unsigned>(s % bits_per_limb);
    const bool      extra     = bits != 0 && static_cast<unsigned>(std::countl_zero(src[n - 1])) < bits;
    const size_type new_n     = n + whole + static_cast<size_type>(extra);

    limb_type* const limbs = storage_for_overwrite(new_n);
    if (bits != 0) {
        const limb_type out = detail::lshift_copy(limbs + whole, src.data(), n, bits);
        if (extra) {
            limbs[n + whole] = out;
        }
    } else {
        std::copy_n(src.data(), n, limbs + whole);
    }
    std::fill_n(limbs, whole, limb_type{0});
    m_size_and_sign = (static_cast<std::uint32_t>(neg) << 31) | static_cast<std::uint32_t>(new_n);
    clear_inline_tail(old_count);
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::assign_shifted_right(const std::span<const uint_multiprecision_t> src,
                                                            const bool                                   neg,
                                                            const shift_type                             s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    const size_type n     = src.size();
    const size_type whole = s / bits_per_limb;
    const unsigned  bits  = static_cast<unsigned>(s % bits_per_limb);

    if (whole >= n) {
        set_zero();
        if (neg) {
            limb_ptr()[0]   = 1;
            m_size_and_sign = 0x8000'0001U;
        }
        return;
    }

    bool inexact = false;
    if (neg) {
        for (size_type i = 0; i < whole && !inexact; ++i) {
            inexact = src[i] != 0;
        }
    }

    // Size the result exactly (the source's top limb may shift away entirely) so a heap source whose
    // shifted value fits inline does not allocate.
    const size_type  old_count = limb_count();
    const limb_type* from      = src.data() + whole;
    size_type        k         = n - whole;
    const bool       top_dies  = k > 1 && bits != 0 && (from[k - 1] >> bits) == 0;
    k -= static_cast<size_type>(top_dies);
    limb_type* const limbs = storage_for_overwrite(k);
    if (bits != 0) {
        inexact |= detail::rshift_copy(limbs, from, k, bits) != 0;
        if (top_dies) {
            // The vanished top limb still contributes its low bits to limb k - 1.
            limbs[k - 1] |= static_cast<limb_type>(from[k] << (bits_per_limb - bits));
        }
    } else {
        std::copy_n(from, k, limbs);
    }

    while (k > 1 && limbs[k - 1] == 0) {
        --k;
    }
    m_size_and_sign = (static_cast<std::uint32_t>(neg) << 31) | static_cast<std::uint32_t>(k);
    clear_inline_tail(old_count);
    if (neg && inexact) {
        unchecked_increment_magnitude();
        BEMAN_BIG_INT_DEBUG_ASSERT(is_negative());
    }
}

// [big.int.ops]

template <std::size_t b, class L, class A>
constexpr std::span<const uint_multiprecision_t> basic_big_int<b, L, A>::representation() const noexcept {
    return {limb_ptr(), limb_count()};
}

template <std::size_t b, class L, class A>
constexpr std::size_t basic_big_int<b, L, A>::representation_size() const noexcept {
    // Number of limbs spanned by the magnitude: a single limb for a zero value,
    // otherwise ceil(size() / bits_per_limb). Equals representation().size().
    return is_zero() ? size_type{1} : detail::div_to_pos_inf(size(), bits_per_limb);
}

template <std::size_t b, class L, class A>
constexpr typename basic_big_int<b, L, A>::allocator_type basic_big_int<b, L, A>::get_allocator() const noexcept {
    return m_alloc;
}

template <std::size_t b, class L, class A>
constexpr std::size_t basic_big_int<b, L, A>::size() const noexcept {
    // Significant bits in the magnitude, ignoring the sign; zero has no significant bits.
    const auto count = limb_count();
    BEMAN_BIG_INT_DEBUG_ASSERT(count != 0); // Would be a class invariant violation, but good to check
    const auto top = limb_ptr()[count - 1];
    if (top == 0) {
        return 0;
    }
    return (count - 1) * bits_per_limb + (bits_per_limb - static_cast<size_type>(std::countl_zero(top)));
}

template <std::size_t b, class L, class A>
constexpr std::size_t basic_big_int<b, L, A>::max_size() const noexcept {
    // Maximum number of bits the magnitude may occupy.
    return max_bits;
}

template <std::size_t b, class L, class A>
constexpr std::size_t basic_big_int<b, L, A>::max_representation_size() const noexcept {
    // Maximum number of limbs the representation may occupy.
    return max_limbs;
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::reserve(const size_type n) {
    // n is a bit count; reserve enough limbs to hold it.
    reserve_representation(detail::div_to_pos_inf(n, bits_per_limb));
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::reserve_representation(const size_type n) {
    // Reserve room for at least n representation limbs.
    grow(n);
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::capacity() const noexcept -> size_type {
    // Number of bits of storage currently available without reallocating.
    return representation_capacity() * bits_per_limb;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::representation_capacity() const noexcept -> size_type {
    // Usable limb capacity: the in-place limb count, or the dynamic allocation size once
    // the value has spilled to the heap. Never drops below inplace_capacity.
    return std::max<size_type>(inplace_capacity, m_capacity);
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::shrink_to_fit() {
    const auto count = limb_count();

    if (is_representation_inplace() || m_capacity <= count) {
        return;
    }

    if (count <= inplace_capacity) {
        // Move back to inline storage
        // We need a manual loop to switch the active union member in consteval context
        // At runtime this should become equivalent to std::uninitialized_copy_n
        pointer    old_data = m_storage.data;
        const auto old_cap  = m_capacity;
        m_capacity          = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            m_storage.limbs[i] = old_data[i];
        }
        for (size_type i = count; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = 0;
        }
        free_limbs(old_data, old_cap);
    } else {
        // Reallocate to a smaller heap buffer
        const alloc_result allocation = alloc_limbs(count);
        copy_n_to_allocation(m_storage.data, count, allocation);
        free_limbs(m_storage.data, m_capacity);
        m_storage.data = allocation.ptr;
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
    }
}

// [big.int.unary]
// The const& overloads copy, so the result's allocator comes from `select_on_container_copy_construction`

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator+() const& -> basic_big_int {
    return *this;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator+() && noexcept -> basic_big_int {
    return std::move(*this);
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator-() const& -> basic_big_int {
    auto copy = *this;
    copy.negate();
    return copy;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator-() && noexcept -> basic_big_int {
    auto copy = std::move(*this);
    copy.negate();
    return copy;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator~() const& -> basic_big_int {
    // Bitwise operations emulate two's complement behavior,
    // where ~x is mathematically (-x - 1).
    auto copy = *this;
    copy.negate();
    --copy;
    return copy;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator~() && -> basic_big_int {
    // See also the const& overload.
    // Unlike operator-, we cannot make this noexcept because the decrement may reallocate.
    auto copy = std::move(*this);
    copy.negate();
    --copy;
    return copy;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator++() -> basic_big_int& {
    if (is_negative()) {
        unchecked_decrement_magnitude();
        if (limb_count() == 1 && limb_ptr()[0] == 0) {
            unchecked_set_sign(false);
        }
    } else {
        unchecked_increment_magnitude();
    }
    return *this;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator++(int) -> basic_big_int {
    auto copy = *this;
    ++(*this);
    return copy;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator--() -> basic_big_int& {
    if (is_negative()) {
        unchecked_increment_magnitude();
    } else {
        if (unchecked_decrement_magnitude()) {
            unchecked_set_sign(true);
        }
    }
    return *this;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::operator--(int) -> basic_big_int {
    auto copy = *this;
    --(*this);
    return copy;
}

// [big.int.cmp]
BEMAN_BIG_INT_EXPORT template <class L, detail::common_big_int_type_with<L> R>
constexpr bool operator==(const L& lhs, const R& rhs) noexcept {
    if constexpr (detail::is_basic_big_int_v<L>) {
        if constexpr (detail::is_basic_big_int_v<R>) {
            return lhs.equals_big_int(rhs);
        } else {
            return lhs.equals_integer(rhs);
        }
    } else {
        static_assert(detail::is_basic_big_int_v<R>);
        return rhs.equals_integer(lhs);
    }
}

BEMAN_BIG_INT_EXPORT template <class L, detail::common_big_int_type_with<L> R>
constexpr std::strong_ordering operator<=>(const L& lhs, const R& rhs) noexcept {
    if constexpr (detail::is_basic_big_int_v<L>) {
        if constexpr (detail::is_basic_big_int_v<R>) {
            return lhs.compare_big_int(rhs);
        } else {
            return lhs.compare_integer(rhs);
        }
    } else {
        static_assert(detail::is_basic_big_int_v<R>);
        return detail::invert(rhs.compare_integer(lhs));
    }
}

template <std::size_t b, class L, class A>
template <detail::cv_unqualified_arithmetic T, bool ignore_sign>
constexpr T basic_big_int<b, L, A>::to() const noexcept {
    if constexpr (std::is_same_v<T, bool>) {
        return !is_zero();
    } else if constexpr (std::is_floating_point_v<T>) {
        const bool negative = !ignore_sign && is_negative();
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return detail::constexpr_copysign(static_cast<T>(inplace_to_bit_uint()), negative ? T{-1} : T{1});
            }
        }
        return detail::compose_float<T>(representation(), negative);
    } else {
        if constexpr (ignore_sign && has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return static_cast<T>(inplace_to_bit_uint());
            }
        } else if constexpr (has_inplace_to_bit_sint) {
            if (is_representation_inplace()) {
                return static_cast<T>(inplace_to_sbit_int());
            }
        }
        using U = detail::make_unsigned_t<T>;
        U                 mag{0};
        constexpr auto    n     = detail::div_to_pos_inf(sizeof(U), sizeof(limb_type));
        const auto* const limbs = limb_ptr();
        for (std::size_t i = 0; i < std::min(n, static_cast<std::size_t>(limb_count())); ++i) {
            mag |= static_cast<U>(limbs[i]) << (i * bits_per_limb);
        }
        if constexpr (ignore_sign) {
            return mag;
        } else {
            return static_cast<T>(is_negative() ? ~mag + U{1} : mag);
        }
    }
}

namespace detail {

// Converts a given unsigned integer to a `std::array<uint_multiprecision_t, N>`,
// where `N` is sufficiently large to store the value of `x`.
template <unsigned_integer T>
[[nodiscard]] constexpr auto to_limbs(const T x) noexcept {
    constexpr std::size_t bits_per_limb = width_v<uint_multiprecision_t>;
    constexpr std::size_t limb_count    = div_to_pos_inf(width_v<T>, bits_per_limb);
    static_assert(limb_count != 0);
    using Result                         = std::array<uint_multiprecision_t, limb_count>;
    constexpr bool eligible_for_bit_cast = sizeof(Result) == sizeof(T) && //
                                           width_v<T> % bits_per_limb == 0 &&
                                           std::endian::native == std::endian::little;
    if constexpr (eligible_for_bit_cast) {
        // While `std::bit_cast` should be the fastest form of conversion
        // (especially in constant evaluation and on debug builds),
        // many conditions must be met.
        // For example, `_BitInt(100)` is not eligible due to padding bits,
        // (this could change with a `std::bit_cast_clear_padding` function in the future)
        // `_BitInt` is too small to be eligible on 64-bit, and
        // `_BitInt` is eligible, but (as in all other cases),
        // only on little-endian architectures.
        return std::bit_cast<Result>(x);
    } else {
        Result result;
        for (std::size_t i = 0; i < limb_count; ++i) {
            result[i] = static_cast<uint_multiprecision_t>(x >> (i * bits_per_limb));
        }
        return result;
    }
}

// Creates a span with fixed extent rather than dynamic extent,
// while deducing the size from the argument.
// This is useful for all kinds of binary operations between `big_int` and integers,
// where the integer is converted to a fixed amount of limbs.
// That fixed amount is often just `1`,
// which can be special-cased using `if constexpr` in various algorithms,
// and even without special casing, a fixed extent assists in constant folding/propagation.
template <class T, std::size_t N>
[[nodiscard]] constexpr std::span<const T, N> to_fixed_span(const std::array<T, N>& arr) noexcept {
    return std::span<const T, N>(arr);
}

// Three-way compares the magnitudes represented by two limb spans (little-endian, zero-padded).
// Treats both as non-negative; callers layer sign handling on top.
// Split on which side is longer so each branch carries only one high-tail scan.
// When an extent is not `dynamic_extent`, `.size()` is a compile-time constant,
// so the loops are easier to unroll.
template <std::size_t extent_a, std::size_t extent_b>
[[nodiscard]] constexpr std::strong_ordering
compare_limb_magnitudes(const std::span<const uint_multiprecision_t, extent_a> a,
                        const std::span<const uint_multiprecision_t, extent_b> b) noexcept {
    if (a.size() > b.size()) {
        // If there are more significant nonzero digits in `a`, it is greater.
        // Decimal example: 123 > 23
        for (std::size_t i = a.size(); i-- > b.size();) {
            if (a[i] != 0) {
                return std::strong_ordering::greater;
            }
        }
        // Compare the common digits from most to least significant.
        for (std::size_t i = b.size(); i-- > 0;) {
            const auto result = a[i] <=> b[i];
            if (std::is_neq(result)) {
                return result;
            }
        }
    } else {
        for (std::size_t i = b.size(); i-- > a.size();) {
            if (b[i] != 0) {
                return std::strong_ordering::less;
            }
        }
        for (std::size_t i = a.size(); i-- > 0;) {
            const auto result = a[i] <=> b[i];
            if (std::is_neq(result)) {
                return result;
            }
        }
    }
    return std::strong_ordering::equal;
}

// Convenience macro that describes one of eight forms of binary operation
// that any binary operation for `big_int` can take.
enum struct binary_op_form : unsigned char {
    // Both sides are movable.
    // Typically, the operations is performed by mutating the integer
    // with the most capacity.
    move_move,
    // Only the left side is movable, and its allocation is reused.
    // The left side is also a `basic_big_int`, but not movable.
    move_copy,
    // Only the right side is movable, and its allocation is reused.
    // The right side is also a `basic_big_int`, but not movable.
    copy_move,
    // Neither side is movable, so a fresh `basic_big_int` is created.
    copy_copy,
    // The left side is movable, and its allocation is reused.
    // The right side is a fundamental integer (of any cvref qualification).
    move_int,
    // The right side is movable, and its allocation is reused.
    // The left side is a fundamental integer (of any cvref qualification).
    int_move,
    // A fresh `basic_big_int` is created because the left side is not movable.
    // The right side is a fundamental integer (of any cvref qualification).
    copy_int,
    // A fresh `basic_big_int` is created because the left side is not movable.
    // The left side is a fundamental integer (of any cvref qualification).
    int_copy,
};

// Variable template that classifies the form of binary operation
// into one of the `binary_op_form` enumerators,
// using the `L` and `R` template parameters deduced from the forwarding references
// in one of the binary operations.
template <class L, class R>
inline constexpr binary_op_form classify_form_v = [] {
    using LT                                   = std::remove_cvref_t<L>;
    using RT                                   = std::remove_cvref_t<R>;
    [[maybe_unused]] constexpr bool copy_left  = std::is_reference_v<L>;
    [[maybe_unused]] constexpr bool copy_right = std::is_reference_v<R>;

    if constexpr (is_basic_big_int_v<LT> && is_basic_big_int_v<RT>) {
        return copy_left && copy_right    ? binary_op_form::copy_copy
               : !copy_left && copy_right ? binary_op_form::move_copy
               : copy_left && !copy_right ? binary_op_form::copy_move
                                          : binary_op_form::move_move;
    } else if constexpr (is_basic_big_int_v<LT>) {
        return copy_left ? binary_op_form::copy_int : binary_op_form::move_int;
    } else if constexpr (is_basic_big_int_v<RT>) {
        return copy_right ? binary_op_form::int_copy : binary_op_form::int_move;
    } else {
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("Invalid case");
    }
}();

// The allocator a binary operator's result is built with: the `basic_big_int`
// operand's allocator run through `select_on_container_copy_construction`, taking
// the left operand when both sides are `basic_big_int`.
template <class Result, class L, class R>
[[nodiscard]] constexpr typename Result::allocator_type result_allocator(const L& x, const R& y) noexcept {
    using traits = std::allocator_traits<typename Result::allocator_type>;
    if constexpr (is_basic_big_int_v<std::remove_cvref_t<L>>) {
        return traits::select_on_container_copy_construction(x.get_allocator());
    } else {
        return traits::select_on_container_copy_construction(y.get_allocator());
    }
}

} // namespace detail

template <std::size_t b, class L, class A>
template <detail::bitwise_op op, class T>
constexpr auto basic_big_int<b, L, A>::bitwise_assign_impl(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, basic_big_int>) {
            if (std::addressof(rhs) == this) {
                if constexpr (op == detail::bitwise_op::xor_) {
                    set_zero();
                }
                // & and | are identity on self so value needs to be unchanged.
                return *this;
            }
        }
        bitwise_in_place<op>(rhs.representation(), rhs.is_negative());
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        bitwise_in_place<op>(detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs));
    }
    return *this;
}

// [big.int.binary]
//
// The shared pattern for `operator+` and `operator-` is: build `Result r` from one
// operand (moving an rvalue `basic_big_int`'s storage, copying an lvalue, or
// constructing from a primitive), then fold the other operand in via `add_in_place`.
// `add_in_place` handles carry, borrow, trim, and sign normalization uniformly.
//
// Destination priority:
//   * both `basic_big_int` rvalues  -> move the one with more limbs (no subsequent grow)
//   * one  `basic_big_int` rvalue   -> move it
//   * both `basic_big_int` lvalues  -> copy the one with more limbs
//   * one  `basic_big_int` lvalue   -> copy it (the other side is a primitive)
//
// `common_big_int_type` only yields a type when both `basic_big_int` operands are the
// exact same `basic_big_int<b, L, A>` instantiation, so the operand type already matches
// `Result` and no explicit type-equality guard is needed.
BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator+(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    // In each of these branches we try to take the largest storage available
    // In the case that we do have to allocate, we automatically add in an extra limb,
    // otherwise we run the risk of a second allocation occurring a the end of addition
    // In the case that we are using inline storage we do not request an extra limb,
    // we defer that decision till as late as possible in case the addition result fits
    // into the static storage rather than having to allocate for no reason
    if constexpr (form == detail::binary_op_form::move_move) {
        if (x.limb_count() >= y.limb_count()) {
            Result r = std::move(x);
            r.add_in_place(y.representation(), y.is_negative());
            return r;
        }
        Result r = std::move(y);
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_copy) {
        Result r = std::move(x);
        r.add_in_place(y.representation(), y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_move) {
        Result r = std::move(y);
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_copy) {
        // `add_into` writes the sum straight into the fresh result: one allocation, one pass.
        Result r{detail::result_allocator<Result>(x, y)};
        r.add_into(x.representation(), x.is_negative(), y.representation(), y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        Result     r       = std::move(x);
        r.add_in_place(detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_move) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        Result     r       = std::move(y);
        r.add_in_place(detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        Result     r{detail::result_allocator<Result>(x, y)};
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.add_into(x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_copy) {
        Result     r{detail::result_allocator<Result>(x, y)};
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.add_into(y.representation(), y.is_negative(), detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    }
}

// `x - y` is implemented as `x + (-y)`: we flip the sign of the right-hand operand
// (without materializing a negated value) and dispatch through the same magnitude
// add/subtract core as `operator+`. Destination priority matches `operator+`:
//   * lhs-destination paths pass the other side's span with sign `!rhs_neg`
//   * rhs-destination paths `r.negate()` first (cheap XOR on the sign word),
//     then add the lhs side with its own sign, yielding `(-y) + x = x - y`
BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator-(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    // See `operator+` description of logic, as it is the same
    if constexpr (form == detail::binary_op_form::move_move) {
        if (x.limb_count() >= y.limb_count()) {
            Result r = std::move(x);
            r.add_in_place(y.representation(), !y.is_negative()); // r + (-y)
            return r;
        }
        Result r = std::move(y);
        r.negate();                                          // r = -y
        r.add_in_place(x.representation(), x.is_negative()); // (-y) + x = x - y
        return r;
    } else if constexpr (form == detail::binary_op_form::move_copy) {
        Result r = std::move(x);
        r.add_in_place(y.representation(), !y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_move) {
        // `r = -y; r += x` gives `x - y`.
        Result r = -std::move(y);
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_copy) {
        // Both lvalue `basic_big_int`s: fold `x + (-y)` into a fresh buffer in a
        // single pass via `add_into`, fusing the allocation with the subtract.
        Result r{detail::result_allocator<Result>(x, y)};
        r.add_into(x.representation(), x.is_negative(), y.representation(), !y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        Result     r       = std::move(x);
        r.add_in_place(detail::to_fixed_span(y_limbs), !detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_move) {
        // `r = -y; r += x` gives `x - y`.
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        Result     r       = -std::move(y);
        r.add_in_place(detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        Result     r{detail::result_allocator<Result>(x, y)};
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.add_into(x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), !detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_copy) {
        Result     r{detail::result_allocator<Result>(x, y)};
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.add_into(y.representation(), !y.is_negative(), detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    }
}

// The operand parameters are spelled `Lhs`/`Rhs` here rather than `L`/`R` as in the class body:
// a member template may not redeclare `L` from the enclosing template-parameter-list ([temp.local]).
template <std::size_t b, class L, class A>
template <detail::bitwise_op op, class Lhs, class Rhs>
constexpr detail::common_big_int_type<Lhs, Rhs> basic_big_int<b, L, A>::bitwise_impl(Lhs&& x, Rhs&& y) {
    using Result        = detail::common_big_int_type<Lhs, Rhs>;
    constexpr auto form = detail::classify_form_v<Lhs, Rhs>;

    // Every form builds a fresh result, so the allocator is selected rather than
    // taken from an operand.
    const auto alloc = detail::result_allocator<Result>(x, y);

    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        return Result::template dispatch_bitwise<op>(
            x.representation(), x.is_negative(), y.representation(), y.is_negative(), alloc);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        return Result::template dispatch_bitwise<op>(
            x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y), alloc);
    } else {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        return Result::template dispatch_bitwise<op>(
            detail::to_fixed_span(x_limbs), detail::integer_signbit(x), y.representation(), y.is_negative(), alloc);
    }
}

template <std::size_t b, class L, class A>
template <detail::bitwise_op op, std::size_t extent>
constexpr void basic_big_int<b, L, A>::bitwise_in_place(const std::span<const uint_multiprecision_t, extent> other,
                                                        const bool other_neg) {
    const bool this_neg = is_negative();

    const auto run = [&]<bool NL, bool NR>() {
        constexpr bool    res_neg   = detail::eval_bitwise<op>(NL, NR);
        const std::size_t old_count = limb_count();

        const std::size_t n = detail::bitwise_result_limb_count<op, NL, NR>(old_count, other.size());

        grow(n + static_cast<std::size_t>(res_neg));
        // Zero any newly-grown limbs that the old value didn't cover.
        {
            limb_type* const p = limb_ptr();
            for (std::size_t i = old_count; i < n + static_cast<std::size_t>(res_neg); ++i) {
                p[i] = limb_type{0};
            }
        }

        const bool extra = detail::eval_bitwise_into_spans<op, NL, NR>(
            std::span<const uint_multiprecision_t>{limb_ptr(), old_count},
            other,
            std::span<uint_multiprecision_t>{limb_ptr(), n + static_cast<std::size_t>(res_neg)});

        unchecked_set_limb_count(static_cast<std::uint32_t>(extra ? n + 1 : n));
        unchecked_trim_magnitude();
        unchecked_set_sign(res_neg && !unchecked_is_magnitude_zero());
    };

    if (!this_neg && !other_neg)
        run.template operator()<false, false>();
    else if (this_neg && !other_neg)
        run.template operator()<true, false>();
    else if (!this_neg && other_neg)
        run.template operator()<false, true>();
    else
        run.template operator()<true, true>();
}

BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator&(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    return Result::template bitwise_impl<detail::bitwise_op::and_>(std::forward<L>(x), std::forward<R>(y));
}

BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator|(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    return Result::template bitwise_impl<detail::bitwise_op::or_>(std::forward<L>(x), std::forward<R>(y));
}

BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator^(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    return Result::template bitwise_impl<detail::bitwise_op::xor_>(std::forward<L>(x), std::forward<R>(y));
}

BEMAN_BIG_INT_EXPORT template <class T, detail::signed_or_unsigned S>
    requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
constexpr std::remove_cvref_t<T> operator<<(T&& x, const S s) {
    using Result        = std::remove_cvref_t<T>;
    using shift_type    = typename Result::shift_type;
    constexpr auto form = detail::classify_form_v<T, S>;

    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<detail::make_unsigned_t<S>>(s) <= Result::shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= Result::shift_max);
    }
    const auto shift = static_cast<shift_type>(s);

    if constexpr (form == detail::binary_op_form::move_int) {
        // rvalue: shift in place, no copy needed.
        Result r = std::move(x);
        r.shift_left(shift);
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        // lvalue: build the shifted value straight into a fresh result, one pass.
        Result r{detail::result_allocator<Result>(x, s)};
        r.assign_shifted_left(x.representation(), x.is_negative(), shift);
        return r;
    }
}

BEMAN_BIG_INT_EXPORT template <class T, detail::signed_or_unsigned S>
    requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
constexpr std::remove_cvref_t<T> operator>>(T&& x, const S s) {
    using Result        = std::remove_cvref_t<T>;
    using shift_type    = typename Result::shift_type;
    constexpr auto form = detail::classify_form_v<T, S>;

    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<detail::make_unsigned_t<S>>(s) <= Result::shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= Result::shift_max);
    }
    const auto shift = static_cast<shift_type>(s);

    if constexpr (form == detail::binary_op_form::move_int) {
        // rvalue: shift in place, no copy needed.
        Result r = std::move(x);
        r.shift_right(shift);
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        // lvalue: shift straight from the source into a fresh result, one pass.
        // Shifting out every bit leaves just the sign (-1 or 0), without touching the limbs.
        const auto alloc = detail::result_allocator<Result>(x, s);
        if (shift >= static_cast<shift_type>(x.size())) {
            if (x.is_negative()) {
                return Result{-1, alloc};
            }
            return Result{alloc};
        }
        Result r{alloc};
        r.assign_shifted_right(x.representation(), x.is_negative(), shift);
        return r;
    }
}

template <std::size_t b, class L, class A>
template <detail::signed_or_unsigned Integer>
constexpr bool basic_big_int<b, L, A>::equals_integer(const Integer x) const noexcept {
    if constexpr (std::is_unsigned_v<Integer>) {
        if (is_negative()) {
            return false;
        }
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return inplace_to_bit_uint() == x;
            }
        }
        return equals_limbs(detail::to_fixed_span(detail::to_limbs(x)), false);
    } else {
        const auto limbs = detail::to_limbs(detail::uabs(x));
        return equals_limbs(detail::to_fixed_span(limbs), x < 0);
    }
}

template <std::size_t b, class L, class A>
constexpr bool basic_big_int<b, L, A>::equals_big_int(const basic_big_int& x) const noexcept {
    // We can do fancier things in the future, but this works for now.
    return equals_limbs(x.representation(), x.is_negative());
}

template <std::size_t b, class L, class A>
template <std::size_t extent>
constexpr bool basic_big_int<b, L, A>::equals_limbs(const std::span<const uint_multiprecision_t, extent> limbs,
                                                    const bool limbs_negative) const noexcept {
    if (is_negative() != limbs_negative) {
        return false;
    }

    const auto* const   self = limb_ptr();
    const std::uint32_t lc   = limb_count();

    // Split on which side is longer so each branch carries only one tail-zero loop.
    // When extent != dynamic_extent, `limbs.size()` is a compile-time constant,
    // so these loops can be more easily unrolled.
    // We also don't need to do all three scans, just two for any given case.
    if (lc >= limbs.size()) {
        for (std::size_t i = 0; i < limbs.size(); ++i) {
            if (self[i] != limbs[i]) {
                return false;
            }
        }
        // Our own limbs can have additional ignored zeroes.
        for (std::size_t i = limbs.size(); i < lc; ++i) {
            if (self[i] != 0) {
                return false;
            }
        }
    } else {
        for (std::size_t i = 0; i < lc; ++i) {
            if (self[i] != limbs[i]) {
                return false;
            }
        }
        // The provided limbs can have additional ignored zeroes.
        for (std::size_t i = lc; i < limbs.size(); ++i) {
            if (limbs[i] != 0) {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t b, class L, class A>
template <detail::signed_or_unsigned Integer>
constexpr std::strong_ordering basic_big_int<b, L, A>::compare_integer(const Integer x) const noexcept {
    if constexpr (std::is_unsigned_v<Integer>) {
        if (is_negative()) {
            return std::strong_ordering::less;
        }
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return inplace_to_bit_uint() <=> x;
            }
        }
        return compare_limbs(detail::to_fixed_span(detail::to_limbs(x)), false);
    } else {
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                const auto sign_compare = (x < 0) <=> is_negative();
                if (std::is_neq(sign_compare)) {
                    return sign_compare;
                }
                // For two-negative operands, bigger magnitude means a smaller
                // value, so swap the operand order of the magnitude compare.
                return is_negative() ? detail::uabs(x) <=> inplace_to_bit_uint()
                                     : inplace_to_bit_uint() <=> detail::uabs(x);
            }
        }
        const auto limbs = detail::to_limbs(detail::uabs(x));
        return compare_limbs(detail::to_fixed_span(limbs), x < 0);
    }
}

template <std::size_t b, class L, class A>
constexpr std::strong_ordering basic_big_int<b, L, A>::compare_big_int(const basic_big_int& x) const noexcept {
    // We can do fancier things in the future, but this works for now.
    return compare_limbs(x.representation(), x.is_negative());
}

template <std::size_t b, class L, class A>
template <std::size_t extent>
constexpr std::strong_ordering
basic_big_int<b, L, A>::compare_limbs(const std::span<const uint_multiprecision_t, extent> limbs,
                                      const bool limbs_negative) const noexcept {
    // A mismatch between signs lets us short-circuit without comparing the magnitudes.
    const auto sign_compare = limbs_negative <=> is_negative();
    if (std::is_neq(sign_compare)) {
        return sign_compare;
    }

    // Compute the ordering as if both operands were non-negative. For two-negative
    // operands we invert the result at the end, because a larger magnitude means a
    // smaller value.
    const auto magnitude_ordering = detail::compare_limb_magnitudes(representation(), limbs);

    return is_negative() ? detail::invert(magnitude_ordering) : magnitude_ordering;
}

// Adds `(other, other_neg)` into `*this` in place. Shared core for `operator+` and
// `operator-`: the caller chooses the destination (an rvalue operand's storage or a
// copy of an lvalue operand) and supplies the other side as a limb span + sign.
// `other` may alias our own limbs (`x += x`, `x -= x`, `x += -x`): such an operand is never longer than
// `*this`, so no growth happens before it has been read.
template <std::size_t b, class L, class A>
template <std::size_t extent_other>
constexpr void
basic_big_int<b, L, A>::add_in_place(const std::span<const uint_multiprecision_t, extent_other> other_in,
                                     const bool                                                 other_neg) {
    using span_t                 = std::span<const uint_multiprecision_t>;
    const span_t        other    = other_in.first(detail::trimmed_size_span(other_in));
    const bool          this_neg = is_negative();
    const std::size_t   n        = limb_count();
    const std::size_t   m        = other.size();
    const std::uint32_t sign_bit = m_size_and_sign & 0x8000'0000U;

    if (n == 1 && m == 1) {
        // Single-limb operands: one add or subtract, no comparison of spans.
        const limb_type x = limb_ptr()[0];
        const limb_type y = other[0];
        if (this_neg == other_neg) {
            const auto [sum, carry] = detail::carrying_add(x, y, false);
            limb_ptr()[0]           = sum;
            if (carry) {
                grow(2);
                limb_ptr()[1]   = limb_type{1};
                m_size_and_sign = sign_bit | 2U;
            }
        } else if (x == y) {
            set_zero();
        } else {
            limb_ptr()[0]   = x > y ? x - y : y - x;
            m_size_and_sign = (static_cast<std::uint32_t>(x > y ? this_neg : other_neg) << 31) | 1U;
        }
        return;
    }

    if (this_neg == other_neg) {
        // Same sign: the magnitude grows, the sign stays.
        if (m <= n) {
            limb_type* limbs = limb_ptr();
            const bool carry = detail::add_n_tail({limbs, n}, {limbs, n}, other);
            if (carry) {
                grow(n + 1);
                limbs           = limb_ptr();
                limbs[n]        = limb_type{1};
                m_size_and_sign = sign_bit | static_cast<std::uint32_t>(n + 1);
            }
            return;
        }
        // `other` is longer, so it cannot alias us and growing first is safe.
        grow(m);
        limb_type* limbs = limb_ptr();
        const bool carry = detail::add_n_tail({limbs, m}, other, {limbs, n});
        m_size_and_sign  = sign_bit | static_cast<std::uint32_t>(m);
        if (carry) {
            // `grow` only keeps the limbs the count covers, so the count goes first.
            grow(m + 1);
            limb_ptr()[m]   = limb_type{1};
            m_size_and_sign = sign_bit | static_cast<std::uint32_t>(m + 1);
        }
        return;
    }

    // Differing signs: subtract the smaller magnitude from the larger; the larger one's sign wins.
    const auto order = detail::compare_limb_magnitudes(span_t{limb_ptr(), n}, other);
    if (std::is_eq(order)) {
        set_zero();
        return;
    }

    std::size_t           k;
    bool                  result_neg;
    limb_type*            limbs;
    [[maybe_unused]] bool borrow;
    if (std::is_gt(order)) {
        // `|*this| > |other|`: subtract in place, nothing past `other` is touched once the borrow dies.
        limbs      = limb_ptr();
        borrow     = detail::sub_n_tail({limbs, n}, {limbs, n}, other);
        k          = n;
        result_neg = this_neg;
    } else {
        // `|other| > |*this|` (so `m >= n`): `other - *this` over our own limbs, reading each of ours first.
        grow(m);
        limbs      = limb_ptr();
        borrow     = detail::sub_n_tail({limbs, m}, other, {limbs, n});
        k          = m;
        result_neg = other_neg;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    while (k > 1 && limbs[k - 1] == 0) {
        --k;
    }
    // Trimmed limbs are zero, so the inline tail needs no clearing.
    m_size_and_sign = (static_cast<std::uint32_t>(result_neg) << 31) | static_cast<std::uint32_t>(k);
}

// Computes `(a, a_neg) + (b, b_neg)` directly into `*this`.
// Precondition: `*this` shares no storage with `a` or `b`.
// Either operand order is accepted, and operands may carry untrimmed high zero limbs.
template <std::size_t b, class L, class A>
template <std::size_t extent_a, std::size_t extent_b>
constexpr void basic_big_int<b, L, A>::add_into(const std::span<const uint_multiprecision_t, extent_a> a_in,
                                                bool                                                   a_neg,
                                                const std::span<const uint_multiprecision_t, extent_b> b_in,
                                                bool                                                   b_neg) {
    using span_t = std::span<const uint_multiprecision_t>;
    span_t a     = a_in.first(detail::trimmed_size_span(a_in));
    span_t bs    = b_in.first(detail::trimmed_size_span(b_in));
    if (a.size() < bs.size()) {
        std::swap(a, bs);
        std::swap(a_neg, b_neg);
    }
    if (a.size() == 1) { // both single limbs (a is the longer operand)
        const limb_type x         = a[0];
        const limb_type y         = bs[0];
        const size_type old_count = limb_count();
        if (a_neg == b_neg) {
            const auto [sum, carry] = detail::carrying_add(x, y, false);
            limb_type* const limbs  = storage_for_overwrite(carry ? 2 : 1);
            limbs[0]                = sum;
            if (carry) {
                limbs[1] = limb_type{1};
            }
            m_size_and_sign = (static_cast<std::uint32_t>(a_neg) << 31) | (carry ? 2U : 1U);
        } else if (x == y) {
            set_zero();
            return;
        } else {
            storage_for_overwrite(1)[0] = x > y ? x - y : y - x;
            m_size_and_sign             = (static_cast<std::uint32_t>(x > y ? a_neg : b_neg) << 31) | 1U;
        }
        clear_inline_tail(old_count);
        return;
    }

    const std::size_t old_count = limb_count();
    const std::size_t big       = a.size();
    const std::size_t cap       = is_representation_inplace() ? inplace_capacity : m_capacity;

    if (a_neg == b_neg) {
        // Same sign: the sign of the result is `a_neg` (the magnitude is nonzero when that is negative).
        // Ask for the carry limb only when the body alone would force an allocation anyway.
        limb_type* const limbs = storage_for_overwrite(big > cap ? big + 1 : big);
        const bool       carry = detail::add_n_tail({limbs, big}, a, bs);
        const auto       sign  = static_cast<std::uint32_t>(a_neg) << 31;
        m_size_and_sign        = sign | static_cast<std::uint32_t>(big);
        if (carry) {
            // `grow` only keeps the limbs the count covers, so the count goes first.
            grow(big + 1);
            limb_ptr()[big] = limb_type{1};
            m_size_and_sign = sign | static_cast<std::uint32_t>(big + 1);
        }
        clear_inline_tail(old_count);
        return;
    }

    // Differing signs: subtract the smaller magnitude from the larger.
    const auto order = detail::compare_limb_magnitudes(a, bs);
    if (std::is_eq(order)) {
        set_zero();
        return;
    }
    const bool   a_larger   = std::is_gt(order);
    const span_t larger     = a_larger ? a : bs;
    const span_t smaller    = a_larger ? bs : a;
    const bool   result_neg = a_larger ? a_neg : b_neg;

    std::size_t                 k      = larger.size();
    limb_type* const            limbs  = storage_for_overwrite(k);
    [[maybe_unused]] const bool borrow = detail::sub_n_tail({limbs, k}, larger, smaller);
    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    while (k > 1 && limbs[k - 1] == 0) {
        --k;
    }
    m_size_and_sign = (static_cast<std::uint32_t>(result_neg) << 31) | static_cast<std::uint32_t>(k);
    clear_inline_tail(old_count);
}

// Computes `a * b` and stores the result into `*this`. The operands must not alias `*this`.
// The product is written straight into `*this`'s storage (the multiply dispatchers need no pre-zeroed
// result); a product one limb past the inline capacity is formed on the stack at run time, so that a
// value that trims to fit inline never allocates.
template <std::size_t b, class L, class A>
template <std::size_t extent_a, std::size_t extent_b>
constexpr void basic_big_int<b, L, A>::multiply_into(const std::span<const uint_multiprecision_t, extent_a> a,
                                                     const bool                                             a_neg,
                                                     const std::span<const uint_multiprecision_t, extent_b> b_span,
                                                     const bool                                             b_neg) {
    const auto a_trimmed = a.first(detail::trimmed_size_span(a));
    const auto b_trimmed = b_span.first(detail::trimmed_size_span(b_span));

    // Zero * anything = positive 0
    if (detail::is_span_zero(a_trimmed) || detail::is_span_zero(b_trimmed)) {
        set_zero();
        return;
    }

    const std::size_t result_size = a_trimmed.size() + b_trimmed.size();
    const size_type   old_count   = limb_count();

    // Both operands are nonzero, so the product is nonzero and its sign is the xor of the operand signs.
    if constexpr (inplace_capacity + 1 <= 64) {
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if (result_size == inplace_capacity + 1) {
                limb_type         stack_buf[inplace_capacity + 1];
                const std::size_t sig = detail::multiply_dispatch(
                    std::span<limb_type>{stack_buf, result_size}, a_trimmed, b_trimmed, m_alloc);
                limb_type* const dst = storage_for_overwrite(sig);
                std::copy_n(stack_buf, sig, dst);
                unchecked_set_limb_count(static_cast<std::uint32_t>(sig));
                clear_inline_tail(old_count);
                unchecked_set_sign(a_neg != b_neg);
                return;
            }
        }
    }

    limb_type* const  dst = storage_for_overwrite(result_size);
    const std::size_t sig =
        detail::multiply_dispatch(std::span<limb_type>{dst, result_size}, a_trimmed, b_trimmed, m_alloc);
    unchecked_set_limb_count(static_cast<std::uint32_t>(sig));
    clear_inline_tail(old_count);
    unchecked_set_sign(a_neg != b_neg);
}

template <std::size_t b, class L, class A>
template <detail::bitwise_op op, bool neg_left, bool neg_right, std::size_t extent_a, std::size_t extent_b>
constexpr basic_big_int<b, L, A>
basic_big_int<b, L, A>::make_bitwise_of_limbs(const std::span<const uint_multiprecision_t, extent_a> lhs,
                                              const std::span<const uint_multiprecision_t, extent_b> rhs,
                                              const allocator_type&                                  alloc) {
    constexpr bool res_neg = detail::eval_bitwise<op>(neg_left, neg_right);

    const std::size_t n = [&]() -> std::size_t {
        if constexpr (op == detail::bitwise_op::and_) {
            if constexpr (!neg_left && !neg_right)
                return std::min(lhs.size(), rhs.size());
            else if constexpr (!neg_left)
                return lhs.size();
            else if constexpr (!neg_right)
                return rhs.size();
            else
                return std::max(lhs.size(), rhs.size());
        } else {
            return std::max(lhs.size(), rhs.size());
        }
    }();

    basic_big_int result{alloc};
    result.grow(n + static_cast<std::size_t>(res_neg));

    const bool extra = detail::eval_bitwise_into_spans<op, neg_left, neg_right>(
        lhs, rhs, std::span<uint_multiprecision_t>{result.limb_ptr(), n + static_cast<std::size_t>(res_neg)});

    result.unchecked_set_limb_count(static_cast<std::uint32_t>(extra ? n + 1 : n));
    result.unchecked_trim_magnitude();
    result.unchecked_set_sign(res_neg && !result.unchecked_is_magnitude_zero());
    return result;
}

template <std::size_t b, class L, class A>
template <detail::bitwise_op op, std::size_t extent_a, std::size_t extent_b>
constexpr basic_big_int<b, L, A>
basic_big_int<b, L, A>::dispatch_bitwise(const std::span<const uint_multiprecision_t, extent_a> lhs,
                                         const bool                                             lhs_neg,
                                         const std::span<const uint_multiprecision_t, extent_b> rhs,
                                         const bool                                             rhs_neg,
                                         const allocator_type&                                  alloc) {
    if (!lhs_neg && !rhs_neg) {
        return make_bitwise_of_limbs<op, false, false>(lhs, rhs, alloc);
    }
    if (lhs_neg && !rhs_neg) {
        return make_bitwise_of_limbs<op, true, false>(lhs, rhs, alloc);
    }
    if (!lhs_neg && rhs_neg) {
        return make_bitwise_of_limbs<op, false, true>(lhs, rhs, alloc);
    }
    return make_bitwise_of_limbs<op, true, true>(lhs, rhs, alloc);
}

// Since multiplication needs a fresh output buffer (the result has up to
// a_size + b_size limbs and cannot overlap either input), every path creates a new
// `Result` and calls `multiply_into`.
//
// TODO : This is a member function instead of a free function like add_in_place,
// because maybe this is a pessimistic view on our allocation requirements?
BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator*(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        Result r{detail::result_allocator<Result>(x, y)};
        if constexpr (Result::has_inplace_to_wide_bit_uint) {
            if (x.is_representation_inplace() && y.is_representation_inplace()) {
                const auto product = x.inplace_to_wide_bit_uint() * y.inplace_to_wide_bit_uint();
                r.assign_magnitude(product);
                if (product != 0 && x.is_negative() != y.is_negative()) {
                    r.unchecked_set_sign(true);
                }
                return r;
            }
        }
        r.multiply_into(x.representation(), x.is_negative(), y.representation(), y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        Result r{detail::result_allocator<Result>(x, y)};
        if constexpr (Result::has_inplace_to_wide_bit_uint) {
            if constexpr (detail::width_v<std::remove_cvref_t<R>> <= Result::inplace_bits) {
                if (x.is_representation_inplace()) {
                    const auto product = x.inplace_to_wide_bit_uint() * detail::uabs(y);
                    r.assign_magnitude(product);
                    if (product != 0 && x.is_negative() != detail::integer_signbit(y)) {
                        r.unchecked_set_sign(true);
                    }
                    return r;
                }
            }
        }
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.multiply_into(
            x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        // Multiplication is commutative, so in the `int * big_int` case,
        // we delegate to `big_int * int`.
        // The unary plus operator produces a prvalue, which reduces template instantiations.
        return std::forward<R>(y) * +x;
    } else {
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("Unknown form of multiplication.");
    }
}

// Compound addition and subtraction
//
// `*this` is always the destination, so we skip the copy/move step that the free
// `operator+` / `operator-` need and fold `rhs` directly into our own storage via
// `add_in_place`.
template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator+=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        add_in_place(rhs.representation(), rhs.is_negative());
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        add_in_place(detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs));
    }
    return *this;
}

template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator-=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if (std::addressof(rhs) == this) {
            set_zero();
        } else {
            add_in_place(rhs.representation(), !rhs.is_negative());
        }
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        add_in_place(detail::to_fixed_span(rhs_limbs), !detail::integer_signbit(rhs));
    }
    return *this;
}

// Multiplies in place, keeping the allocator and, below the large-product threshold, the capacity.
template <std::size_t b, class L, class A>
template <std::size_t extent_b>
constexpr void basic_big_int<b, L, A>::multiply_in_place(const std::span<const uint_multiprecision_t, extent_b> b_span,
                                                         const bool b_neg) {
    const std::span<const uint_multiprecision_t> a_all{limb_ptr(), limb_count()};
    const auto                                   a_trimmed = a_all.first(detail::trimmed_size_span(a_all));
    const auto                                   b_trimmed = b_span.first(detail::trimmed_size_span(b_span));

    if (detail::is_span_zero(a_trimmed) || detail::is_span_zero(b_trimmed)) {
        set_zero();
        return;
    }

    const bool        result_neg = is_negative() != b_neg;
    const std::size_t la         = a_trimmed.size();
    const std::size_t lb         = b_trimmed.size();

    if (lb == 1) {
        // `b` may alias our limbs, so take its value before anything moves.
        const limb_type mul   = b_trimmed[0];
        limb_type*      limbs = limb_ptr();
        limb_type       carry = 0;
        for (std::size_t i = 0; i < la; ++i) {
            const auto [lo, hi] = detail::widening_mul(limbs[i], mul);
            const auto [sum, c] = detail::carrying_add(lo, carry);
            limbs[i]            = sum;
            carry               = hi + static_cast<limb_type>(c);
        }
        if (carry != 0) {
            grow(la + 1);
            limb_ptr()[la] = carry;
            unchecked_set_limb_count(static_cast<std::uint32_t>(la + 1));
        }
        unchecked_set_sign(result_neg);
        return;
    }

    const std::size_t   result_size = la + lb;
    constexpr size_type stack_limbs = 64;
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (result_size <= stack_limbs) {
            limb_type         stack_buf[stack_limbs];
            const std::size_t sig =
                detail::multiply_dispatch(std::span<limb_type>{stack_buf, result_size}, a_trimmed, b_trimmed, m_alloc);
            // The operands are dead now, so growth may release the old buffer.
            const size_type  old_count = limb_count();
            limb_type* const dst       = storage_for_overwrite(sig);
            std::copy_n(stack_buf, sig, dst);
            unchecked_set_limb_count(static_cast<std::uint32_t>(sig));
            clear_inline_tail(old_count);
            unchecked_set_sign(result_neg);
            return;
        }
    }

    // Large product: build it in a fresh buffer, then adopt that buffer. `*this` is untouched if this throws.
    basic_big_int product{m_alloc};
    product.multiply_into(a_trimmed, is_negative(), b_trimmed, b_neg);
    *this = std::move(product);
}

// Compound multiplication assignment.
template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator*=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        // `rhs` may be `*this`: multiply_in_place reads both operands before writing.
        multiply_in_place(rhs.representation(), rhs.is_negative());
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        multiply_in_place(detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs));
    }
    return *this;
}

template <std::size_t b, class L, class A>
template <std::size_t extent_b>
constexpr bool
basic_big_int<b, L, A>::divide_in_place_small(const std::span<const uint_multiprecision_t, extent_b> divisor,
                                              const bool                                             divisor_neg,
                                              const detail::division_op                              op) {
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        const std::span<const uint_multiprecision_t> dividend_all{limb_ptr(), limb_count()};
        const auto        dividend = dividend_all.first(detail::trimmed_size_span(dividend_all));
        const auto        d        = divisor.first(detail::trimmed_size_span(divisor));
        const std::size_t n        = dividend.size();
        const std::size_t m        = d.size();
        if (m < 2 || n < m || detail::is_span_zero(dividend) || !detail::divide_takes_schoolbook(n, m)) {
            return false;
        }
        const std::size_t q_cap = n - m + 1;
        const std::size_t r_cap = n + 1;
        const std::size_t need =
            detail::divide_schoolbook_storage_size(n, m, true) + (op == detail::division_op::rem ? q_cap : 0);
        if (need > detail::small_division_stack_limbs) {
            return false;
        }

        limb_type                      stack_buf[detail::small_division_stack_limbs];
        detail::scratch_allocator_base scratch(stack_buf, detail::small_division_stack_limbs);
        limb_type* const               limbs     = limb_ptr();
        const size_type                old_count = limb_count();
        if (op == detail::division_op::div) {
            // The schoolbook kernel copies the dividend into scratch before its first quotient write.
            const std::span<uint_multiprecision_t> quot{limbs, q_cap};
            detail::divide_dispatch_q(quot, dividend, d, scratch, m_alloc);
            const std::size_t qsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{limbs, q_cap});
            unchecked_set_limb_count(static_cast<std::uint32_t>(qsize));
            clear_inline_tail(old_count);
            unchecked_set_sign(is_negative() != divisor_neg && !unchecked_is_magnitude_zero());
        } else {
            const bool                             neg       = is_negative();
            const std::span<uint_multiprecision_t> quot_span = scratch.allocate(q_cap);
            const std::span<uint_multiprecision_t> rem_span  = scratch.allocate(r_cap);
            detail::divide_dispatch(quot_span, rem_span, dividend, d, scratch, m_alloc);
            const std::size_t rsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{rem_span});
            std::copy_n(rem_span.data(), rsize, limbs);
            unchecked_set_limb_count(static_cast<std::uint32_t>(rsize));
            clear_inline_tail(old_count);
            unchecked_set_sign(neg && !unchecked_is_magnitude_zero());
        }
        return true;
    }
    return false;
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::make_from_magnitude(const std::span<const uint_multiprecision_t> mag,
                                                           const bool                                   neg,
                                                           const allocator_type& a) -> basic_big_int {
    basic_big_int r{a};
    limb_type*    dst = r.storage_for_overwrite(mag.size());
    std::copy(mag.begin(), mag.end(), dst);
    r.unchecked_set_limb_count(static_cast<std::uint32_t>(mag.size()));
    r.unchecked_set_sign(neg && !r.unchecked_is_magnitude_zero());
    return r;
}

// Single-limb divisor fast path from Knuth
// Avoids a scratch remainder buffer and a `t` buffer. The quotient streams through `*this`'s limbs when they
// already have room; otherwise a run-time stack buffer takes it, so a quotient that trims to fit inline never
// allocates. The remainder-only form needs no quotient at all.
template <std::size_t b, class L, class A>
template <std::size_t extent_a>
constexpr uint_multiprecision_t
basic_big_int<b, L, A>::divmod_into_short(const std::span<const uint_multiprecision_t, extent_a> dividend,
                                          const bool                                             dividend_neg,
                                          const uint_multiprecision_t                            divisor,
                                          const bool                                             divisor_neg,
                                          const detail::division_op                              op) {
    BEMAN_BIG_INT_ASSERT(divisor != 0);

    const bool      want_quotient = op != detail::division_op::rem;
    const auto      dividend_trim = dividend.first(detail::trimmed_size_span(dividend));
    const bool      result_neg    = want_quotient ? (dividend_neg != divisor_neg) : dividend_neg;
    const size_type old_count     = limb_count();

    if (detail::is_span_zero(dividend_trim)) {
        limb_ptr()[0] = 0;
        unchecked_set_limb_count(1);
        clear_inline_tail(old_count);
        unchecked_set_sign(false);
        return {};
    }

    // Single-limb dividend: native divide/modulo.
    if (dividend_trim.size() == 1) {
        const uint_multiprecision_t d = dividend_trim[0];
        limb_ptr()[0]                 = want_quotient ? (d / divisor) : (d % divisor);
        unchecked_set_limb_count(1);
        clear_inline_tail(old_count);
        unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
        return op != detail::division_op::div ? d % divisor : 0;
    }

    const size_type n = dividend_trim.size();

    if (!want_quotient) {
        const uint_multiprecision_t remainder = detail::mod_unsigned_short(dividend_trim, divisor);
        limb_ptr()[0]                         = remainder;
        unchecked_set_limb_count(1);
        clear_inline_tail(old_count);
        unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
        return remainder;
    }

    const size_type     capacity    = is_representation_inplace() ? inplace_capacity : m_capacity;
    constexpr size_type stack_limbs = 64;
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (n > capacity && n <= stack_limbs) {
            limb_type                   stack_buf[stack_limbs];
            const uint_multiprecision_t remainder =
                detail::divide_unsigned_short(std::span<uint_multiprecision_t>{stack_buf, n}, dividend_trim, divisor);
            const size_type  qsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{stack_buf, n});
            limb_type* const dst   = storage_for_overwrite(qsize);
            std::copy_n(stack_buf, qsize, dst);
            unchecked_set_limb_count(static_cast<std::uint32_t>(qsize));
            clear_inline_tail(old_count);
            unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
            return remainder;
        }
    }

    limb_type* const            dst = storage_for_overwrite(n);
    const uint_multiprecision_t remainder =
        detail::divide_unsigned_short(std::span<uint_multiprecision_t>{dst, n}, dividend_trim, divisor);
    const size_type qsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{dst, n});
    unchecked_set_limb_count(static_cast<std::uint32_t>(qsize));
    clear_inline_tail(old_count);
    unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
    return remainder;
}

template <std::size_t b, class L, class A>
constexpr uint_multiprecision_t basic_big_int<b, L, A>::divmod_in_place_short(const uint_multiprecision_t divisor,
                                                                              const bool                  divisor_neg,
                                                                              const detail::division_op   op) {
    BEMAN_BIG_INT_ASSERT(divisor != 0);

    const bool                  want_quotient = op != detail::division_op::rem;
    const bool                  result_neg    = want_quotient ? is_negative() != divisor_neg : is_negative();
    const auto                  limbs         = limb_span();
    const uint_multiprecision_t remainder     = detail::divide_unsigned_short(limbs, limbs, divisor);
    if (want_quotient) {
        unchecked_trim_magnitude();
    } else {
        const auto old_count = limb_count();
        limb_ptr()[0]        = remainder;
        unchecked_set_limb_count(1);
        clear_inline_tail(old_count);
    }
    unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
    return remainder;
}

// Generic divide/modulus dispatcher.
// Handles the four special cases:
//   1) zero divisor,
//   2) zero dividend,
//   3) |dividend| < |divisor|,
//   4) single-limb divisor
// before falling through to `detail::divide_dispatch`.
template <std::size_t b, class L, class A>
template <std::size_t extent_a, std::size_t extent_b>
constexpr auto basic_big_int<b, L, A>::divmod_into(const std::span<const uint_multiprecision_t, extent_a> dividend,
                                                   const bool                                             dividend_neg,
                                                   const std::span<const uint_multiprecision_t, extent_b> divisor,
                                                   const bool                                             divisor_neg,
                                                   const detail::division_op op) -> basic_big_int {
    const auto dividend_trim = dividend.first(detail::trimmed_size_span(dividend));
    const auto divisor_trim  = divisor.first(detail::trimmed_size_span(divisor));

    BEMAN_BIG_INT_ASSERT(!detail::is_span_zero(divisor_trim));

    const bool want_quotient          = op != detail::division_op::rem;
    const bool unrounded_quotient_neg = want_quotient ? (dividend_neg != divisor_neg) : dividend_neg;

    if (detail::is_span_zero(dividend_trim)) {
        set_zero();
        return {};
    }

    const std::strong_ordering mag_cmp = detail::compare_unsigned_spans(dividend_trim, divisor_trim);
    if (mag_cmp == std::strong_ordering::less) {
        // |dividend| < |divisor|: quotient is 0, remainder is dividend.
        if (op == detail::division_op::div) {
            set_zero();
            return {};
        }
        if (op == detail::division_op::div_rem) {
            set_zero();
            return make_from_magnitude(dividend_trim, dividend_neg, m_alloc);
        }

        const size_type  old_count = limb_count();
        limb_type* const dst       = storage_for_overwrite(dividend_trim.size());
        std::copy(dividend_trim.begin(), dividend_trim.end(), dst);
        unchecked_set_limb_count(static_cast<std::uint32_t>(dividend_trim.size()));
        clear_inline_tail(old_count);
        unchecked_set_sign(dividend_neg && !unchecked_is_magnitude_zero());
        return {};
    }

    // Single-limb divisor fast path.
    if (divisor_trim.size() == 1) {
        const uint_multiprecision_t rem_limb =
            divmod_into_short(dividend_trim, dividend_neg, divisor_trim[0], divisor_neg, op);
        if (op != detail::division_op::div_rem || rem_limb == 0) {
            return {};
        }
        basic_big_int remainder{rem_limb, m_alloc};
        remainder.unchecked_set_sign(dividend_neg);
        return remainder;
    }

    // Multi-limb long division. The quotient lands in `*this` (div, div_rem) or the remainder does (rem);
    // everything else, including the div_rem remainder, lives in scratch and is copied out at its exact size.
    const std::size_t n_div = dividend_trim.size();
    const std::size_t m_div = divisor_trim.size();
    const std::size_t q_cap = n_div - m_div + 1;
    const std::size_t r_cap = n_div + 1;
    const std::size_t scratch_limbs =
        detail::divide_schoolbook_storage_size(n_div, m_div, true) + (op == detail::division_op::rem ? q_cap : 0);
    const size_type old_count = limb_count();

    const auto divide_with = [&](detail::scratch_allocator_base& scratch) -> basic_big_int {
        if (op == detail::division_op::rem) {
            // Quotient and remainder both in scratch; only the remainder is kept.
            const std::span<uint_multiprecision_t> quot_span = scratch.allocate(q_cap);
            const std::span<uint_multiprecision_t> rem_span  = scratch.allocate(r_cap);
            detail::divide_dispatch(quot_span, rem_span, dividend_trim, divisor_trim, scratch, m_alloc);
            const std::size_t rsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{rem_span});
            limb_type* const  dst   = storage_for_overwrite(rsize);
            std::copy_n(rem_span.data(), rsize, dst);
            unchecked_set_limb_count(static_cast<std::uint32_t>(rsize));
            clear_inline_tail(old_count);
            unchecked_set_sign(dividend_neg && !unchecked_is_magnitude_zero());
            return {};
        }

        // Quotient only: the divide-and-conquer band can skip the remainder work through the
        // approximate-quotient path. With div_rem the remainder is produced into scratch.
        limb_type* const                       dst      = storage_for_overwrite(q_cap);
        const std::span<uint_multiprecision_t> quot     = {dst, q_cap};
        std::span<uint_multiprecision_t>       rem_span = {};
        if (op == detail::division_op::div) {
            detail::divide_dispatch_q(quot, dividend_trim, divisor_trim, scratch, m_alloc);
        } else {
            rem_span = scratch.allocate(r_cap);
            detail::divide_dispatch(quot, rem_span, dividend_trim, divisor_trim, scratch, m_alloc);
        }
        const std::size_t qsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{dst, q_cap});
        unchecked_set_limb_count(static_cast<std::uint32_t>(qsize));
        clear_inline_tail(old_count);
        unchecked_set_sign(unrounded_quotient_neg && !unchecked_is_magnitude_zero());
        if (op == detail::division_op::div) {
            return {};
        }
        const std::size_t rsize = detail::trimmed_size_span(std::span<const uint_multiprecision_t>{rem_span});
        return make_from_magnitude(
            std::span<const uint_multiprecision_t>{rem_span.data(), rsize}, dividend_neg, m_alloc);
    };

    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (scratch_limbs <= detail::small_division_stack_limbs) {
            limb_type                      stack_buf[detail::small_division_stack_limbs];
            detail::scratch_allocator_base scratch(stack_buf, detail::small_division_stack_limbs);
            return divide_with(scratch);
        }
    }
    detail::scratch_allocator<allocator_type> scratch(scratch_limbs, m_alloc);
    return divide_with(scratch);
}

// Simultaneously computes the quotient and remainder of a division,
// rounded towards zero.
BEMAN_BIG_INT_EXPORT template <class L, class R>
[[nodiscard]] constexpr div_result<detail::common_big_int_type<L, R>> div_rem_to_zero(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;
    constexpr auto op   = detail::division_op::div_rem;

    // Both results take their allocator from the big_int operand (see the
    // operator/ note).
    using result_alloc_traits = std::allocator_traits<typename Result::allocator_type>;
    const auto result_alloc   = [&] {
        if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
            return result_alloc_traits::select_on_container_copy_construction(y.get_allocator());
        } else {
            return result_alloc_traits::select_on_container_copy_construction(x.get_allocator());
        }
    }();
    Result quo{0, result_alloc};
    Result rem{0, result_alloc};
    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        rem = quo.divmod_into(x.representation(), x.is_negative(), y.representation(), y.is_negative(), op);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        rem                = quo.divmod_into(
            x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y), op);
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        rem                = quo.divmod_into(
            detail::to_fixed_span(x_limbs), detail::integer_signbit(x), y.representation(), y.is_negative(), op);
    }
    return {std::move(quo), std::move(rem)};
}

BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator/(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;
    // The result takes its allocator from the big_int operand through
    // select_on_container_copy_construction: stateful allocators propagate
    // into expression results, while pmr's convention (results on the
    // default resource) is preserved.
    using result_alloc_traits = std::allocator_traits<typename Result::allocator_type>;
    Result r                  = [&] {
        if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
            return Result{0, result_alloc_traits::select_on_container_copy_construction(y.get_allocator())};
        } else {
            return Result{0, result_alloc_traits::select_on_container_copy_construction(x.get_allocator())};
        }
    }();
    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        r.divmod_into(
            x.representation(), x.is_negative(), y.representation(), y.is_negative(), detail::division_op::div);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.divmod_into(x.representation(),
                      x.is_negative(),
                      detail::to_fixed_span(y_limbs),
                      detail::integer_signbit(y),
                      detail::division_op::div);
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.divmod_into(detail::to_fixed_span(x_limbs),
                      detail::integer_signbit(x),
                      y.representation(),
                      y.is_negative(),
                      detail::division_op::div);
    }
    return r;
}

BEMAN_BIG_INT_EXPORT template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator%(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;
    // The result takes its allocator from the big_int operand through
    // select_on_container_copy_construction: stateful allocators propagate
    // into expression results, while pmr's convention (results on the
    // default resource) is preserved.
    using result_alloc_traits = std::allocator_traits<typename Result::allocator_type>;
    Result r                  = [&] {
        if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
            return Result{0, result_alloc_traits::select_on_container_copy_construction(y.get_allocator())};
        } else {
            return Result{0, result_alloc_traits::select_on_container_copy_construction(x.get_allocator())};
        }
    }();
    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        r.divmod_into(
            x.representation(), x.is_negative(), y.representation(), y.is_negative(), detail::division_op::rem);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.divmod_into(x.representation(),
                      x.is_negative(),
                      detail::to_fixed_span(y_limbs),
                      detail::integer_signbit(y),
                      detail::division_op::rem);
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.divmod_into(detail::to_fixed_span(x_limbs),
                      detail::integer_signbit(x),
                      y.representation(),
                      y.is_negative(),
                      detail::division_op::rem);
    }
    return r;
}

// Compound division and modulus assignments.
template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator/=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, basic_big_int>) {
            // Self-division: the multi-limb slow path below would zero `rhs`'s
            // storage when `&rhs == this`.
            if (&rhs == this) {
                BEMAN_BIG_INT_ASSERT(!is_zero());
                set_zero();
                limb_ptr()[0] = 1;
                return *this;
            }
        }
        if (rhs.limb_count() == 1) {
            static_cast<void>(divmod_in_place_short(rhs.limb_ptr()[0], rhs.is_negative(), detail::division_op::div));
        } else if (!divide_in_place_small(rhs.representation(), rhs.is_negative(), detail::division_op::div)) {
            const basic_big_int temp(std::move(*this), m_alloc);
            set_zero();
            divmod_into(temp.representation(),
                        temp.is_negative(),
                        rhs.representation(),
                        rhs.is_negative(),
                        detail::division_op::div);
        }
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        if (detail::trimmed_size_span(rhs_limbs) == 1) {
            static_cast<void>(
                divmod_in_place_short(rhs_limbs[0], detail::integer_signbit(rhs), detail::division_op::div));
        } else if (!divide_in_place_small(
                       detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs), detail::division_op::div)) {
            const basic_big_int temp(std::move(*this), m_alloc);
            set_zero();
            divmod_into(temp.representation(),
                        temp.is_negative(),
                        detail::to_fixed_span(rhs_limbs),
                        detail::integer_signbit(rhs),
                        detail::division_op::div);
        }
    }
    return *this;
}

template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator%=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, basic_big_int>) {
            // Self-modulus: `std::move(*this)` below would zero `rhs` when &rhs == this.
            if (&rhs == this) {
                BEMAN_BIG_INT_ASSERT(!is_zero());
                set_zero();
                return *this;
            }
        }
        if (rhs.limb_count() == 1) {
            static_cast<void>(divmod_in_place_short(rhs.limb_ptr()[0], rhs.is_negative(), detail::division_op::rem));
        } else if (!divide_in_place_small(rhs.representation(), rhs.is_negative(), detail::division_op::rem)) {
            const basic_big_int temp(std::move(*this), m_alloc);
            set_zero();
            divmod_into(temp.representation(),
                        temp.is_negative(),
                        rhs.representation(),
                        rhs.is_negative(),
                        detail::division_op::rem);
        }
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        if (detail::trimmed_size_span(rhs_limbs) == 1) {
            static_cast<void>(
                divmod_in_place_short(rhs_limbs[0], detail::integer_signbit(rhs), detail::division_op::rem));
        } else if (!divide_in_place_small(
                       detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs), detail::division_op::rem)) {
            const basic_big_int temp(std::move(*this), m_alloc);
            set_zero();
            divmod_into(temp.representation(),
                        temp.is_negative(),
                        detail::to_fixed_span(rhs_limbs),
                        detail::integer_signbit(rhs),
                        detail::division_op::rem);
        }
    }
    return *this;
}

template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator&=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    return bitwise_assign_impl<detail::bitwise_op::and_>(std::forward<T>(rhs));
}

template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator|=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    return bitwise_assign_impl<detail::bitwise_op::or_>(std::forward<T>(rhs));
}

template <std::size_t b, class L, class A>
template <class T>
constexpr auto basic_big_int<b, L, A>::operator^=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    return bitwise_assign_impl<detail::bitwise_op::xor_>(std::forward<T>(rhs));
}

// private helpers

template <std::size_t b, class L, class A>
template <detail::unsigned_integer T>
constexpr void basic_big_int<b, L, A>::assign_magnitude(T value) noexcept {
    constexpr size_type value_limbs = detail::div_to_pos_inf(detail::width_v<T>, bits_per_limb);
    if constexpr (value_limbs == 1) {
        limb_ptr()[0] = static_cast<limb_type>(value);
        unchecked_set_limb_count(1);
    } else {
        if constexpr (value_limbs > inplace_capacity) {
            // Check to see if we can fit inplace if the result is sufficiently small
            if (is_representation_inplace() && (value >> inplace_bits) == 0) {
                auto* const dst = limb_ptr();
                for (size_type i = 0; i < inplace_capacity; ++i) {
                    dst[i] = static_cast<limb_type>(value);
                    value >>= bits_per_limb;
                }
                unchecked_set_limb_count(static_cast<std::uint32_t>(inplace_capacity));
                unchecked_trim_magnitude();
                return;
            }
            grow(value_limbs);
        }
        auto* const dst = limb_ptr();
        for (size_type i = 0; i < value_limbs; ++i) {
            dst[i] = static_cast<limb_type>(value);
            value >>= bits_per_limb;
        }
        unchecked_set_limb_count(static_cast<std::uint32_t>(value_limbs));
        unchecked_trim_magnitude();
    }
}

template <std::size_t b, class L, class A>
template <detail::cv_unqualified_floating_point F>
constexpr void basic_big_int<b, L, A>::assign_from_float(const F value) noexcept {
    using traits = detail::ieee_traits<F>;
#ifdef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
    static_assert(!std::is_same_v<F, long double>, "long double is not supported on this platform");
#endif
    BEMAN_BIG_INT_ASSERT(detail::constexpr_isfinite(value));

    // In the happiest case, we can use the intrinsic conversion from binary32 to uint128_t.
    // Compilers have optimized routines for this,
    // and this approach is very fast during constant evaluation.
#ifdef BEMAN_BIG_INT_HAS_INT128
    if constexpr (traits::width == 32 && std::is_convertible_v<F, detail::uint128_t>) {
        assign_magnitude(static_cast<detail::uint128_t>(detail::constexpr_fabs(value)));
        unchecked_set_sign(value < 0);
    }
#endif

    // In all other cases, some decomposition is necessary.
    const auto [sign, exponent, mantissa] = detail::decompose_float(value);

    // There are only fractional bits, and since we truncate, the value is zero.
    if (exponent < -traits::mantissa_bits) {
        set_zero();
        return;
    }

    // The exponent is slightly negative, which can be emulated using a right shift.
    if (exponent < 0) {
        assign_magnitude(mantissa >> -exponent);
        unchecked_set_sign(sign);
        return;
    }

    assign_magnitude(mantissa);
    shift_left(static_cast<shift_type>(exponent));
    unchecked_set_sign(sign);
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::check_length(const size_type limbs_needed) {
    if (limbs_needed > max_limbs) {
        detail::throw_length_error();
    }
}

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4702)

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::alloc_limbs_from(allocator_type& a, const size_type n) -> alloc_result {
    BEMAN_BIG_INT_ASSERT(n != 0);
    check_length(n);
#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST
    if constexpr (detail::traits_has_allocate_at_least<alloc_traits, A>) {
        return alloc_traits::allocate_at_least(a, n);
    } else {
        return {.ptr = alloc_traits::allocate(a, n), .count = n};
    }
#else
    return {.ptr = alloc_traits::allocate(a, n), .count = n};
#endif
}

template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::alloc_limbs(const size_type n) -> alloc_result {
    return alloc_limbs_from(m_alloc, n);
}

BEMAN_BIG_INT_DIAGNOSTIC_POP()

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::free_limbs(pointer p, const size_type n) {
    BEMAN_BIG_INT_ASSERT(p != nullptr);
    BEMAN_BIG_INT_ASSERT(n != 0);
    // Need to suppress known false positive warning.
    // See also https://github.com/llvm/llvm-project/issues/53007
    BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfree-nonheap-object")
    alloc_traits::deallocate(m_alloc, p, n);
    BEMAN_BIG_INT_DIAGNOSTIC_POP()
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::free_storage() {
    if (!is_representation_inplace()) {
        free_limbs(m_storage.data, m_capacity);
    }
}

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4702)

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::grow(const size_type limbs_needed) {
    const size_type current_cap = is_representation_inplace() ? inplace_capacity : m_capacity;
    if (limbs_needed <= current_cap) {
        return;
    }

    if (limbs_needed > max_limbs) {
        detail::throw_length_error();
    }

    // libstdc++ and libc++ normally double storage each allocation
    // MSVC does 1.5x instead of 2x
    // Only the doubling is clamped, so the capacity itself stays representable.
    const size_type    new_cap    = std::min(std::max(limbs_needed, 2 * current_cap), max_limbs);
    const alloc_result allocation = alloc_limbs(new_cap);
    copy_n_to_allocation(limb_ptr(), limb_count(), allocation);

    free_storage();

    m_storage.data = allocation.ptr;
    m_capacity     = static_cast<std::uint32_t>(allocation.count);
}

// Same growth policy as `grow`. The new block is allocated before the old one is released.
// Runtime blocks are left uninitialized (poisoned in debug builds); constant evaluation
// must construct every limb before use.
template <std::size_t b, class L, class A>
constexpr auto basic_big_int<b, L, A>::storage_for_overwrite(const size_type n) -> limb_type* {
    const size_type current_cap = is_representation_inplace() ? inplace_capacity : m_capacity;
    if (n > current_cap) {
        if (n > max_limbs) {
            detail::throw_length_error();
        }
        const size_type    new_cap    = std::min(std::max(n, 2 * current_cap), max_limbs);
        const alloc_result allocation = alloc_limbs(new_cap);
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
#ifndef NDEBUG
            std::uninitialized_fill_n(allocation.ptr, allocation.count, limb_type{~limb_type{0} / 0xFF * 0xA5});
#endif
        } else {
            for (size_type i = 0; i < allocation.count; ++i) {
                std::construct_at(allocation.ptr + i);
            }
        }
        free_storage();
        m_storage.data = allocation.ptr;
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
    }
    return limb_ptr();
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::clear_inline_tail(const size_type old_count) noexcept {
    if constexpr (inplace_capacity != 1) {
        if (is_representation_inplace()) {
            limb_type* const limbs = m_storage.limbs;
            for (size_type i = limb_count(); i < old_count; ++i) {
                limbs[i] = limb_type{0};
            }
        }
    }
}

BEMAN_BIG_INT_DIAGNOSTIC_POP()

template <std::size_t b, class L, class A>
constexpr void
basic_big_int<b, L, A>::copy_n_to_allocation(const limb_type* const p, const size_type n, const alloc_result out) {
    BEMAN_BIG_INT_ASSERT(p != nullptr);
    BEMAN_BIG_INT_ASSERT(out.ptr != nullptr);
    BEMAN_BIG_INT_ASSERT(n <= out.count);
// If the constexpr raw memory algorithms are available,
// we don't need to differentiate between constant evaluation and runtime.
// Even when we need this fallback case,
// it is always important that all elements in the allocation are initialized
// because we don't keep track of "requested" vs "received" capacity
// (these may not be the same with allocate_at_least).
#ifndef BEMAN_BIG_INT_HAS_CPP_LIB_RAW_MEMORY_ALGORITHMS
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
#endif
        std::uninitialized_copy_n(p, n, out.ptr);
        std::uninitialized_value_construct_n(out.ptr + n, out.count - n);
#ifndef BEMAN_BIG_INT_HAS_CPP_LIB_RAW_MEMORY_ALGORITHMS
    } else {
        for (size_type i = 0; i < n; ++i) {
            std::construct_at(out.ptr + i, p[i]);
        }
        for (size_type i = n; i < out.count; ++i) {
            std::construct_at(out.ptr + i);
        }
    }
#endif
}

// A value that fits in place is stored in place even when `x` holds it on the
// heap, e.g. after `x.reserve_representation(100)`; only a larger value allocates.
template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::copy_limbs_from(const basic_big_int& x) {
    BEMAN_BIG_INT_ASSERT(is_representation_inplace());
    if (x.limb_count() <= inplace_capacity) {
        if (x.is_representation_inplace()) {
            for (size_type i = 0; i < inplace_capacity; ++i) {
                m_storage.limbs[i] = x.m_storage.limbs[i];
            }
        } else {
            for (size_type i = 0; i < x.limb_count(); ++i) {
                m_storage.limbs[i] = x.m_storage.data[i];
            }
            for (size_type i = x.limb_count(); i < inplace_capacity; ++i) {
                m_storage.limbs[i] = {};
            }
        }
    } else {
        const alloc_result allocation = alloc_limbs(x.limb_count());
        copy_n_to_allocation(x.m_storage.data, x.limb_count(), allocation);
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
        m_storage.data = allocation.ptr;
    }
}

template <std::size_t b, class L, class A>
constexpr void basic_big_int<b, L, A>::push_back_limb(limb_type limb) {
    const auto count = limb_count();
    if (count >= (is_representation_inplace() ? inplace_capacity : m_capacity)) {
        grow(count + 1); // exponential growth
    }
    limb_ptr()[count] = limb;
    unchecked_set_limb_count(static_cast<std::uint32_t>(count + 1));
}

// Snapshot the value produced by a stateless `Generator` callable into a
// `basic_big_int<N, Limb, A>` whose inline storage is large enough to hold the value
// without any heap allocation.
// Bridges consteval-computed values to runtime.
// The result preserves the source's allocator and limb types, and the allocator instance.
BEMAN_BIG_INT_EXPORT template <typename Generator>
[[nodiscard]] consteval auto copy_to_runtime() {
    using source_type = std::remove_cvref_t<decltype(Generator{}())>;
    using limb_type   = detail::limb_type_of_t<source_type>;

    constexpr std::size_t bits_per_limb = detail::width_v<limb_type>;

    constexpr std::size_t target_bits = []() consteval {
        const auto v = Generator{}();
        const auto w = v.size();
        return w == 0 ? bits_per_limb : ((w + bits_per_limb - 1) / bits_per_limb) * bits_per_limb;
    }();

    using result_type = basic_big_int<target_bits, limb_type, typename source_type::allocator_type>;

    auto src = Generator{}();
    return result_type{src, src.get_allocator()};
}

// Standard public alias for defaulted type
BEMAN_BIG_INT_EXPORT using big_int = basic_big_int<64, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>;

BEMAN_BIG_INT_EXPORT namespace pmr {

    template <std::size_t b, class L = uint_multiprecision_t>
    using basic_big_int = BEMAN_BIG_INT_NAMESPACE::basic_big_int<b, L, std::pmr::polymorphic_allocator<L>>;

    using big_int = basic_big_int<BEMAN_BIG_INT_NAMESPACE::big_int::inplace_bits>;

} // namespace pmr

BEMAN_BIG_INT_END_NAMESPACE

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Occupies a rung of the hash ladder whose width the target's `_BitInt` cannot reach.
// `std::hash` is disabled for it, so that rung is skipped.
struct absent_hash_rung {};

// One rung per object size rather than per width: a `_BitInt` past a word occupies
inline constexpr std::size_t hash_rung_step = BEMAN_BIG_INT_WORD_BITS;

// The rung `i` object sizes up from the narrowest.
#if BEMAN_BIG_INT_BITINT_MAXWIDTH > 0
template <std::size_t i>
using hash_rung_t = bit_int<static_cast<int>((i + 1) * hash_rung_step)>;
#else
template <std::size_t i>
using hash_rung_t = absent_hash_rung;
#endif

// How many rungs there are to try: as many object sizes as both the target's `_BitInt` and
// the reach of the standard library's own hash allow.
inline constexpr std::size_t hash_rung_count =
    std::min<std::size_t>(BEMAN_BIG_INT_HASH_MAX_OBJECT_WORDS, BEMAN_BIG_INT_BITINT_MAXWIDTH / hash_rung_step);

using hash_rung_indices = std::make_index_sequence<hash_rung_count>;

// Whether the standard library gives `T` a digest. The size test is not an optimization:
// past the size its hash covers libc++ fails to compile rather than failing this
// requirement, so the size has to be asked first.
template <class T>
concept std_hashable = sizeof(T) <= BEMAN_BIG_INT_HASH_MAX_OBJECT_WORDS * sizeof(std::size_t) && requires(const T& v) {
    { std::hash<T>{}(v) } -> std::convertible_to<std::size_t>;
};

// The width of a rung, or zero where the target lacks the type or the implementation does not hash it.
template <class T>
[[nodiscard]] consteval std::size_t hash_rung_bits() {
    if constexpr (std_hashable<T>) {
        return width_v<T>;
    } else {
        return 0;
    }
}

// The widest rung the standard library will hash, or zero where it hashes none.
template <std::size_t... i>
[[nodiscard]] consteval std::size_t widest_hash_rung(std::index_sequence<i...>) {
    std::size_t widest = 0;
    ((widest = std::max(widest, hash_rung_bits<hash_rung_t<i>>())), ...);
    return widest;
}

// Past this width no rung can hold the value, so the ladder is skipped outright.
inline constexpr std::size_t widest_hash_rung_bits = widest_hash_rung(hash_rung_indices{});

// Hashes `x` as `T` where `T` can represent it, reporting whether it did.
// A magnitude narrower than `T` is in range whatever its sign, and one of exactly `T`'s
// width only for the most negative value of `T`, whose magnitude is a single bit. The
// width therefore selects the rung, and only the rung that wins pays for a conversion.
template <class T, class BigInt>
[[nodiscard]] bool
hash_as_bit_int(const BigInt& x, const std::size_t width, const bool negative, std::size_t& digest) noexcept {
    if constexpr (std_hashable<T>) {
        if (width < width_v<T> || (width == width_v<T> && negative && is_power_of_two_span(x.representation()))) {
            digest = std::hash<T>{}(static_cast<T>(x));
            return true;
        }
    }
    return false;
}

// Hashes `x` on the narrowest rung that can represent it, reporting whether one could. The
// fold short-circuits, so the rungs are tried narrowest first and stop at the one that wins.
template <class BigInt, std::size_t... i>
[[nodiscard]] bool hash_on_rung_ladder(const BigInt&     x,
                                       const std::size_t width,
                                       const bool        negative,
                                       std::size_t&      digest,
                                       std::index_sequence<i...>) noexcept {
    return (hash_as_bit_int<hash_rung_t<i>>(x, width, negative, digest) || ...);
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

// [big.int.hash], hash support
template <std::size_t b, class L, class A>
struct std::hash<BEMAN_BIG_INT_NAMESPACE::basic_big_int<b, L, A>> {

    std::size_t operator()(const BEMAN_BIG_INT_NAMESPACE::basic_big_int<b, L, A>& x) const noexcept {
        namespace detail = BEMAN_BIG_INT_NAMESPACE::detail;

        const bool negative = x.is_negative();

        // A value a signed bit-precise integer type can represent is hashed as the narrowest such type
        if constexpr (detail::widest_hash_rung_bits != 0) {
            const auto width = x.size();
            if (width <= detail::widest_hash_rung_bits) {
                std::size_t digest{};
                if (detail::hash_on_rung_ladder(x, width, negative, digest, detail::hash_rung_indices{})) {
                    return digest;
                }
            }
        }

        return detail::siphash(x.representation(), negative);
    }
};

#include <beman/big_int/copy_to_runtime.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_POP() // For string and array bounds at the top of this file

#endif // BEMAN_BIG_INT_BASIC_BIG_INT_HPP
