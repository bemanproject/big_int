// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Header side of the ABI-compatibility proof. See abi_compat.test.cpp (the
// module side) for the full design note; the short version: this translation
// unit reaches beman::big_int::big_int and beman::big_int::pmr::big_int by
// #including the public headers, never by `import`. It defines five ordinary
// functions naming those types in their signatures. abi_compat.test.cpp
// declares those same five signatures itself, by hand, from a translation
// unit that only imports beman.big_int and shares no header with this one. If
// module/big_int.cppm's `extern "C++"` block did not attach every declaration
// it reaches to the global module (classic mangling), the type named
// `beman::big_int::big_int` on this side and the type of the same name reached
// through the import on the other side would be two distinct, differently
// mangled entities. The two sides' declarations of e.g. `twice` would then
// mangle differently, and the executable linking this object file against
// abi_compat.test.cpp's object file would fail with undefined-symbol errors.
// A clean link is therefore the assertion this pair of files makes; the
// runtime checks in abi_compat.test.cpp additionally confirm that values (not
// just declarations) survive the crossing intact.

#include <beman/big_int.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace beman_big_int_abi {

BEMAN_BIG_INT_NAMESPACE::big_int twice(const BEMAN_BIG_INT_NAMESPACE::big_int& x) { return x + x; }

BEMAN_BIG_INT_NAMESPACE::pmr::big_int pmr_twice(const BEMAN_BIG_INT_NAMESPACE::pmr::big_int& x) { return x + x; }

std::string decimal(const BEMAN_BIG_INT_NAMESPACE::big_int& x) { return BEMAN_BIG_INT_NAMESPACE::to_string(x); }

std::size_t hashed(const BEMAN_BIG_INT_NAMESPACE::big_int& x) {
    return std::hash<BEMAN_BIG_INT_NAMESPACE::big_int>{}(x);
}

BEMAN_BIG_INT_NAMESPACE::big_int parse(const std::string& text) {
    BEMAN_BIG_INT_NAMESPACE::big_int out;
    [[maybe_unused]] const auto      result =
        BEMAN_BIG_INT_NAMESPACE::from_chars(text.data(), text.data() + text.size(), out, 10);
    return out;
}

} // namespace beman_big_int_abi
