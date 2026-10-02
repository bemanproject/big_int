// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_FORMAT_HPP
#define BEMAN_BIG_INT_FORMAT_HPP

#include <beman/big_int/big_int.hpp>

// Guarding this include is safe only because the .cppm supplies <version> in its
// global module fragment before the purview #if below tests __cpp_lib_format.
#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <version>
#endif

#if __has_include(<format>) && defined(__cpp_lib_format) && __cpp_lib_format >= 201907L

    #ifndef BEMAN_BIG_INT_BUILD_MODULE
        #include <algorithm>
        #include <array>
        #include <climits>
        #include <cstddef>
        #include <cstdint>
        #include <format>
        #include <limits>
        #include <locale>
        #include <string>
        #include <string_view>
        #include <type_traits>
    #endif

    #include <beman/big_int/string.hpp> // for beman::big_int::to_string

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// How the field is aligned. `defaulted` records that the user wrote no alignment, so the
// presentation type can pick its own default (right for numbers, left for the `c` type).
enum class align_kind : std::uint8_t { defaulted, left, right, center };

// The sign option. `minus` (only negatives get a sign) is the default.
enum class sign_kind : std::uint8_t { minus, plus, space };

// The fill is one character of the format string's encoding, which UTF-8 spells in up to four
// code units and UTF-16 in up to two.
inline constexpr std::size_t max_fill_units = 4;

// Padding is expected and acceptable.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

// A fully parsed std-format-spec for a big_int argument.
template <class charT>
struct format_spec {
    // The fill character, held as the code units that spell it.
    std::array<charT, max_fill_units> fill{static_cast<charT>(' ')};
    std::size_t                       fill_units = 1;

    align_kind  align        = align_kind::defaulted;
    sign_kind   sign         = sign_kind::minus;
    bool        alt          = false; // '#'
    bool        zero_pad     = false; // '0'
    bool        has_width    = false;
    bool        width_is_arg = false; // width given as a nested {} replacement field
    std::size_t width        = 0;     // static width, or arg-id when width_is_arg
    bool        localized    = false; // 'L'
    int         base         = 10;    // 2, 8, 10, or 16, derived from the type char
    bool        uppercase    = false; // 'X' / 'B'
    bool        as_char      = false; // 'c'
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

[[noreturn]] inline void throw_format_error([[maybe_unused]] const char* why) {
    #ifdef BEMAN_BIG_INT_ALLOW_EXCEPTIONS
    throw std::format_error(why);
    #else
    std::abort();
    #endif
}

template <class charT>
[[nodiscard]] constexpr bool is_align(const charT c) noexcept {
    return c == charT('<') || c == charT('>') || c == charT('^');
}

template <class charT>
[[nodiscard]] constexpr align_kind to_align(const charT c) noexcept {
    return c == charT('<') ? align_kind::left : c == charT('>') ? align_kind::right : align_kind::center;
}

// Number of code units in the character beginning at `it`, or 0 when the units there do not
// form one. `char` is taken to be UTF-8, and `wchar_t` to be UTF-16 or UTF-32 by its width, so
// a multi-byte fill character in a UTF-8 format string, or a surrogate pair in a UTF-16 one,
// counts as the single character it is to the standard formatters.
template <class charT, class It>
[[nodiscard]] constexpr std::size_t char_units(It it, const It end) {
    if (it == end) {
        return 0;
    }
    const auto unit = [](const charT c) {
        return static_cast<std::uint32_t>(static_cast<std::make_unsigned_t<charT>>(c));
    };
    const std::uint32_t lead = unit(*it);

    if constexpr (sizeof(charT) == 1) {
        std::size_t   units = 0;
        std::uint32_t low   = 0x80; // range the next continuation unit may take, narrowed for
        std::uint32_t high  = 0xBF; // the leads that would otherwise admit an overlong form,
                                    // a surrogate, or a value above U+10FFFF
        if (lead < 0x80) {
            return 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            units = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            units = 3;
            low   = lead == 0xE0 ? 0xA0 : low;
            high  = lead == 0xED ? 0x9F : high;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            units = 4;
            low   = lead == 0xF0 ? 0x90 : low;
            high  = lead == 0xF4 ? 0x8F : high;
        } else {
            return 0;
        }
        for (std::size_t i = 1; i < units; ++i) {
            if (++it == end) {
                return 0;
            }
            const std::uint32_t continuation = unit(*it);
            if (continuation < low || continuation > high) {
                return 0;
            }
            low  = 0x80;
            high = 0xBF;
        }
        return units;
    } else if constexpr (sizeof(charT) == 2) {
        if (lead >= 0xD800 && lead <= 0xDBFF) { // a high surrogate needs its low surrogate
            if (++it == end) {
                return 0;
            }
            const std::uint32_t trail = unit(*it);
            return (trail >= 0xDC00 && trail <= 0xDFFF) ? 2 : 0;
        }
        return (lead >= 0xDC00 && lead <= 0xDFFF) ? 0 : 1;
    } else {
        return (lead <= 0x10FFFF && (lead < 0xD800 || lead > 0xDFFF)) ? 1 : 0;
    }
}

// Uppercases an ASCII letter; leaves everything else untouched (locale-independent).
[[nodiscard]] constexpr char ascii_upper(const char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// Parses a non-negative decimal integer, advancing `it`. Throws on overflow of size_t.
template <class charT, class It>
constexpr std::size_t parse_uint(It& it, const It end) {
    constexpr std::size_t maxv = std::numeric_limits<std::size_t>::max();
    constexpr std::size_t cap  = maxv / 10u;
    std::size_t           v    = 0;
    while (it != end) {
        const charT c = *it;
        if (c < charT('0') || c > charT('9')) {
            break;
        }
        const std::size_t dig = static_cast<std::size_t>(c - charT('0'));
        if (v > cap || (v == cap && dig > maxv % 10u)) {
            throw_format_error("big_int format: width or argument index is too large");
        }
        v = v * 10u + dig;
        ++it;
    }
    return v;
}

// Widens an ASCII digit string to charT (digits and base prefixes are always ASCII).
template <class charT>
[[nodiscard]] std::basic_string<charT> widen(const std::string_view ascii) {
    std::basic_string<charT> out;
    out.reserve(ascii.size());
    for (const char c : ascii) {
        out.push_back(static_cast<charT>(static_cast<unsigned char>(c)));
    }
    return out;
}

// Widens an ASCII digit string to charT, inserting locale group separators per numpunct.
template <class charT>
[[nodiscard]] std::basic_string<charT> group_digits(const std::string_view digits, const std::locale& loc) {
    const std::numpunct<charT>& np       = std::use_facet<std::numpunct<charT>>(loc);
    const std::string           grouping = np.grouping();
    if (grouping.empty() || grouping.front() <= 0 || grouping.front() >= CHAR_MAX) {
        return widen<charT>(digits);
    }

    const charT              sep = np.thousands_sep();
    std::basic_string<charT> out;
    out.reserve(digits.size() + digits.size() / 2 + 1);

    std::size_t gi        = 0;
    int         remaining = grouping[gi];
    bool        no_more   = false;
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (!no_more && remaining == 0) {
            out.push_back(sep);
            if (gi + 1 < grouping.size()) {
                ++gi;
            }
            const int g = grouping[gi];
            if (g <= 0 || g >= CHAR_MAX) {
                no_more = true;
            } else {
                remaining = g;
            }
        }
        out.push_back(static_cast<charT>(static_cast<unsigned char>(digits[i])));
        if (!no_more) {
            --remaining;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// Emits sign + base prefix + digit body padded to `width`, honoring sign-aware zero-padding
// (zeros go after the sign and prefix) or fill/alignment (default right for numbers).
template <class charT, class OutputIt>
OutputIt emit_number(OutputIt                        out,
                     const std::string_view          sign,
                     const std::string_view          prefix,
                     const std::basic_string<charT>& body,
                     const format_spec<charT>&       spec,
                     const std::size_t               width) {
    const std::size_t content   = sign.size() + prefix.size() + body.size();
    const auto        put_ascii = [&out](const std::string_view s) {
        for (const char c : s) {
            *out = static_cast<charT>(static_cast<unsigned char>(c));
            ++out;
        }
    };
    const auto put_body = [&out, &body]() {
        for (const charT c : body) {
            *out = c;
            ++out;
        }
    };
    const auto put_zeros = [&out](const std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            *out = static_cast<charT>('0');
            ++out;
        }
    };
    const auto put_fill = [&out, &spec](const std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t u = 0; u < spec.fill_units; ++u) {
                *out = spec.fill[u];
                ++out;
            }
        }
    };

    if (width <= content) {
        put_ascii(sign);
        put_ascii(prefix);
        put_body();
        return out;
    }

    const std::size_t pad = width - content;
    if (spec.zero_pad && spec.align == align_kind::defaulted) {
        put_ascii(sign);
        put_ascii(prefix);
        put_zeros(pad);
        put_body();
        return out;
    }

    const align_kind a     = spec.align == align_kind::defaulted ? align_kind::right : spec.align;
    std::size_t      left  = 0;
    std::size_t      right = 0;
    if (a == align_kind::left) {
        right = pad;
    } else if (a == align_kind::center) {
        left  = pad / 2;
        right = pad - left;
    } else {
        left = pad;
    }
    put_fill(left);
    put_ascii(sign);
    put_ascii(prefix);
    put_body();
    put_fill(right);
    return out;
}

// Emits a single character padded to `width`. The `c` type aligns left by default.
template <class charT, class OutputIt>
OutputIt emit_char_field(OutputIt out, const charT ch, const format_spec<charT>& spec, const std::size_t width) {
    const auto put_fill = [&out, &spec](const std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t u = 0; u < spec.fill_units; ++u) {
                *out = spec.fill[u];
                ++out;
            }
        }
    };

    if (width <= 1) {
        *out = ch;
        ++out;
        return out;
    }

    const std::size_t pad   = width - 1;
    const align_kind  a     = spec.align == align_kind::defaulted ? align_kind::left : spec.align;
    std::size_t       left  = 0;
    std::size_t       right = 0;
    if (a == align_kind::right) {
        left = pad;
    } else if (a == align_kind::center) {
        left  = pad / 2;
        right = pad - left;
    } else {
        right = pad;
    }
    put_fill(left);
    *out = ch;
    ++out;
    put_fill(right);
    return out;
}

// Reads a dynamic width argument and validates it is a non-negative, non-bool integer.
template <class FormatContext>
[[nodiscard]] std::size_t resolve_width_arg(FormatContext& fc, const std::size_t arg_id) {
    const auto arg     = fc.arg(arg_id);
    const auto visitor = [](auto value) -> std::size_t {
        using V = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<V, bool> || !std::is_integral_v<V>) {
            throw_format_error("big_int format: width argument is not a non-bool integer");
        } else {
            if constexpr (std::is_signed_v<V>) {
                if (value < V{0}) {
                    throw_format_error("big_int format: width argument is negative");
                }
            }
            return static_cast<std::size_t>(value);
        }
    };
    if constexpr (requires { arg.visit(visitor); }) {
        return arg.visit(visitor); // C++26 member visit
    } else {
        return std::visit_format_arg(visitor, arg); // C++23 free function
    }
}

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

namespace std {

// Standards-conformant formatter for beman::big_int, mirroring the standard integer
// formatters: fill/align, sign, '#', sign-aware '0', static and dynamic width, the
// 'b'/'B'/'o'/'d'/'x'/'X'/'c' types, and the 'L' locale option, for both char and wchar_t.
template <std::size_t B, class L, class A, class charT>
struct formatter<BEMAN_BIG_INT_NAMESPACE::basic_big_int<B, L, A>, charT> {
    BEMAN_BIG_INT_NAMESPACE::detail::format_spec<charT> spec_;

    template <class ParseContext>
    constexpr typename ParseContext::iterator parse(ParseContext& ctx) {
        namespace d    = BEMAN_BIG_INT_NAMESPACE::detail;
        auto       it  = ctx.begin();
        const auto end = ctx.end();
        spec_          = d::format_spec<charT>{};

        if (it == end || *it == charT('}')) {
            return it;
        }

        // fill-and-align: if an alignment follows the first character, that character is the
        // fill. The fill may span several code units, so its length is measured, not assumed.
        {
            const std::size_t units = d::char_units<charT>(it, end);
            auto              next  = it;
            for (std::size_t i = 0; i < units; ++i) {
                ++next;
            }
            if (units != 0 && next != end && d::is_align(*next) && *it != charT('{') && *it != charT('}')) {
                for (std::size_t i = 0; i < units; ++i) {
                    spec_.fill[i] = *it;
                    ++it;
                }
                spec_.fill_units = units;
                spec_.align      = d::to_align(*next);
                ++it; // `it` reached `next`, so this steps past the alignment character
            } else if (d::is_align(*it)) {
                spec_.align = d::to_align(*it);
                ++it;
            }
        }

        bool sign_set = false;
        if (it != end && (*it == charT('+') || *it == charT('-') || *it == charT(' '))) {
            spec_.sign = (*it == charT('+'))   ? d::sign_kind::plus
                         : (*it == charT(' ')) ? d::sign_kind::space
                                               : d::sign_kind::minus;
            sign_set   = true;
            ++it;
        }

        if (it != end && *it == charT('#')) {
            spec_.alt = true;
            ++it;
        }

        if (it != end && *it == charT('0')) {
            spec_.zero_pad = true;
            ++it;
        }

        // width: a nested {} replacement field, or a positive decimal (a leading 0 was
        // already consumed as the zero-pad flag above).
        if (it != end && *it == charT('{')) {
            ++it;
            spec_.has_width    = true;
            spec_.width_is_arg = true;
            if (it != end && *it == charT('}')) {
                spec_.width = ctx.next_arg_id();
            } else {
                const std::size_t id = d::parse_uint<charT>(it, end);
                ctx.check_arg_id(id);
                spec_.width = id;
            }
            if (it == end || *it != charT('}')) {
                d::throw_format_error("big_int format: invalid dynamic width");
            }
            ++it;
        } else if (it != end && *it >= charT('1') && *it <= charT('9')) {
            spec_.has_width = true;
            spec_.width     = d::parse_uint<charT>(it, end);
        }

        // Precision is never valid for an integer or the 'c' type.
        if (it != end && *it == charT('.')) {
            d::throw_format_error("big_int format: precision is not allowed");
        }

        if (it != end && *it == charT('L')) {
            spec_.localized = true;
            ++it;
        }

        if (it != end && *it != charT('}')) {
            switch (*it) {
            case charT('b'):
                spec_.base = 2;
                break;
            case charT('B'):
                spec_.base      = 2;
                spec_.uppercase = true;
                break;
            case charT('o'):
                spec_.base = 8;
                break;
            case charT('d'):
                spec_.base = 10;
                break;
            case charT('x'):
                spec_.base = 16;
                break;
            case charT('X'):
                spec_.base      = 16;
                spec_.uppercase = true;
                break;
            case charT('c'):
                spec_.as_char = true;
                break;
            default:
                d::throw_format_error("big_int format: invalid type");
            }
            ++it;
        }

        // LWG 3644: 'c' is not an integer presentation type, so sign, '#', and '0' are
        // ill-formed with it. ('L' is accepted and is a no-op on a single character.)
        if (spec_.as_char) {
            if (sign_set) {
                d::throw_format_error("big_int format: the sign option is invalid with the 'c' type");
            }
            if (spec_.alt) {
                d::throw_format_error("big_int format: the '#' option is invalid with the 'c' type");
            }
            if (spec_.zero_pad) {
                d::throw_format_error("big_int format: the '0' option is invalid with the 'c' type");
            }
        }

        if (it != end && *it != charT('}')) {
            d::throw_format_error("big_int format: unmatched characters in format spec");
        }
        return it;
    }

    template <class FormatContext>
    typename FormatContext::iterator format(const BEMAN_BIG_INT_NAMESPACE::basic_big_int<B, L, A>& value,
                                            FormatContext&                                         fc) const {
        namespace d    = BEMAN_BIG_INT_NAMESPACE::detail;
        using big_type = BEMAN_BIG_INT_NAMESPACE::basic_big_int<B, L, A>;

        std::size_t width = spec_.has_width ? spec_.width : std::size_t{0};
        if (spec_.width_is_arg) {
            width = d::resolve_width_arg(fc, spec_.width);
        }

        if (spec_.as_char) {
            const long long lo = static_cast<long long>((std::numeric_limits<charT>::min)());
            const long long hi = static_cast<long long>((std::numeric_limits<charT>::max)());
            if (value < big_type{lo} || value > big_type{hi}) {
                d::throw_format_error("big_int format: value is out of range for the target character type");
            }
            const charT ch = static_cast<charT>(static_cast<long long>(value));
            return d::emit_char_field<charT>(fc.out(), ch, spec_, width);
        }

        // Reuse the sub-quadratic to_chars/to_string: it returns the lowercase magnitude
        // with a leading '-' for negatives, from which we derive the sign.
        std::string       ascii    = BEMAN_BIG_INT_NAMESPACE::to_string(value, spec_.base);
        const bool        negative = !ascii.empty() && ascii.front() == '-';
        const std::size_t mag_off  = negative ? 1u : 0u;
        if (spec_.uppercase) {
            for (std::size_t i = mag_off; i < ascii.size(); ++i) {
                ascii[i] = d::ascii_upper(ascii[i]);
            }
        }
        const std::string_view digits{ascii.data() + mag_off, ascii.size() - mag_off};
        const bool             is_zero = digits == std::string_view{"0"};

        std::basic_string<charT> body =
            spec_.localized ? d::group_digits<charT>(digits, fc.locale()) : d::widen<charT>(digits);

        std::string_view sign;
        if (negative) {
            sign = "-";
        } else if (spec_.sign == d::sign_kind::plus) {
            sign = "+";
        } else if (spec_.sign == d::sign_kind::space) {
            sign = " ";
        }

        std::string_view prefix;
        if (spec_.alt) {
            switch (spec_.base) {
            case 2:
                prefix = spec_.uppercase ? "0B" : "0b";
                break;
            case 16:
                prefix = spec_.uppercase ? "0X" : "0x";
                break;
            case 8:
                if (!is_zero) {
                    prefix = "0";
                }
                break;
            default:
                break;
            }
        }

        return d::emit_number<charT>(fc.out(), sign, prefix, body, spec_, width);
    }
};

} // namespace std

#endif // <format> available

#endif // BEMAN_BIG_INT_FORMAT_HPP
