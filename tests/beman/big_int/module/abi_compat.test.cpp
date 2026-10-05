// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Module side of the ABI-compatibility proof.
//
// module/big_int.cppm wraps the entire public header graph in an
// `extern "C++" { #include <beman/big_int/big_int.hpp> ... }` block specifically so
// that every declaration it reaches -- beman::big_int::big_int,
// beman::big_int::pmr::big_int, the std::hash specialization, and so on --
// attaches to the GLOBAL module (classic Itanium/MSVC mangling) rather than
// becoming owned by the named module `beman.big_int`. This matters because
// the library also compiles ~30 kernel entry points into libbeman.big_int
// from ordinary (non-module) src/*.cpp translation units; if importing the
// module gave a consumer a module-owned `beman::big_int::big_int` distinct
// from the one those kernels were compiled against, nothing would link.
//
// This pair of files is the actual proof, not a runtime assertion. Neither
// file may see the other's declarations through a shared header:
//   - abi_compat_header_tu.cpp reaches beman::big_int::big_int and
//     beman::big_int::pmr::big_int by #including <beman/big_int/big_int.hpp>, and
//     defines five ordinary functions naming those types.
//   - This file reaches the same two types only through `import beman.big_int;`,
//     and DECLARES the same five signatures again, by hand, with no
//     definition -- the definitions are the other file's.
// If the module attached those types to itself instead of to the global
// module, the two sides' declarations of (say) `twice` would mangle
// differently: this file's declaration would have no matching definition
// anywhere, and the executable produced by linking the two object files
// together would fail with an undefined-symbol error naming `twice`,
// `pmr_twice`, `decimal`, `hashed`, or `parse`. The tests below additionally
// confirm that values -- including an allocating one, whose heap pointer,
// packed size/sign word, and allocator must all survive -- cross the boundary
// intact and compare/hash consistently on both sides.

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <string>

// The standard headers come before the import deliberately. GCC cannot merge the
// global-module declarations the module's purview makes reachable with the same
// declarations re-included textually afterwards; including first and importing
// second is the ordering both libstdc++ and libc++ support.
import beman.big_int;
// Declared by hand, not shared via a header with abi_compat_header_tu.cpp.
// Defined over there.
namespace beman_big_int_abi {

BEMAN_BIG_INT_NAMESPACE::big_int      twice(const BEMAN_BIG_INT_NAMESPACE::big_int& x);
BEMAN_BIG_INT_NAMESPACE::pmr::big_int pmr_twice(const BEMAN_BIG_INT_NAMESPACE::pmr::big_int& x);
std::string                           decimal(const BEMAN_BIG_INT_NAMESPACE::big_int& x);
std::size_t                           hashed(const BEMAN_BIG_INT_NAMESPACE::big_int& x);
BEMAN_BIG_INT_NAMESPACE::big_int      parse(const std::string& text);

} // namespace beman_big_int_abi

namespace {

using BEMAN_BIG_INT_NAMESPACE::big_int;
using namespace BEMAN_BIG_INT_NAMESPACE::literals;

} // namespace

// A value built on the module side, read by the header-side TU.
TEST(AbiCompat, ModuleValueReadByHeaderTu) {
    const big_int x = 12345;
    EXPECT_EQ(beman_big_int_abi::decimal(x), "12345");
}

// A value built on the header side (parsed from a string there), read back on
// the module side. `parse` and the value it returns are both
// beman::big_int::big_int as seen from this TU; if that were actually two
// distinct types this would already have failed to link.
TEST(AbiCompat, HeaderValueReadByModuleTu) {
    const big_int parsed = beman_big_int_abi::parse("987654321");
    EXPECT_EQ(parsed, big_int{987654321});
}

// A value crosses into the header-side `twice`, is doubled there, and crosses
// back -- exercised in both directions across the same call.
TEST(AbiCompat, TwiceRoundTripsThroughHeaderTu) {
    const big_int x        = 999999999;
    const big_int expected = x + x;
    EXPECT_EQ(beman_big_int_abi::twice(x), expected);
}

// The interesting case: a value wide enough that it cannot live in inplace
// storage, so its heap pointer, packed size/sign word, and allocator must all
// survive being handed to a function defined in a translation unit that never
// imported the module.
TEST(AbiCompat, AllocatingValueCrossesIntact) {
    const big_int mersenne = (big_int{1} << 4096) - 1_n;

    // Read intact by the header side.
    EXPECT_EQ(beman_big_int_abi::decimal(mersenne), BEMAN_BIG_INT_NAMESPACE::to_string(mersenne));

    // Doubled by the header side, read back intact on the module side.
    const big_int doubled = beman_big_int_abi::twice(mersenne);
    EXPECT_EQ(doubled, mersenne + mersenne);

    // Round-tripped through decimal text via the header side's own to_chars,
    // parsed back by the header side's own from_chars, read intact here.
    const big_int reparsed = beman_big_int_abi::parse(beman_big_int_abi::decimal(mersenne));
    EXPECT_EQ(reparsed, mersenne);
}

// std::hash<beman::big_int::big_int> agrees on both sides of the boundary,
// for both an inplace and an allocating value.
TEST(AbiCompat, HashAgreesAcrossBoundary) {
    const big_int small      = 42;
    const big_int allocating = (big_int{1} << 4096) - 1_n;

    EXPECT_EQ(beman_big_int_abi::hashed(small), (std::hash<big_int>{}(small)));
    EXPECT_EQ(beman_big_int_abi::hashed(allocating), (std::hash<big_int>{}(allocating)));
}

// The pmr specialization crosses the boundary too.
TEST(AbiCompat, PmrSpecializationCrossesBoundary) {
    const BEMAN_BIG_INT_NAMESPACE::pmr::big_int x(555);
    const BEMAN_BIG_INT_NAMESPACE::pmr::big_int doubled = beman_big_int_abi::pmr_twice(x);
    EXPECT_EQ(doubled, BEMAN_BIG_INT_NAMESPACE::pmr::big_int(1110));
}
