// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// The GoogleTest-free smoke test for the named module.
//
// GoogleTest's headers are textual and pull in a large part of the standard library.
// libstdc++ and the MSVC standard library cannot mix textual standard headers with
// `import std` in one translation unit, so this is the only module test built in the
// `import std` configuration. It is built in both configurations so it cannot go
// stale, and it is deliberately broad: under `import std` it is the whole coverage.
//
// Nothing here includes a beman header or uses a BEMAN_BIG_INT_* macro other than
// BEMAN_BIG_INT_NAMESPACE, which CMake passes on the command line. Macros do not
// cross a module boundary, and under BEMAN_BIG_INT_BUILD_MODULE the public
// declarations are marked `export`, which is ill-formed outside a module purview.

#ifdef BEMAN_BIG_INT_USE_STD_MODULE
import std;
#else
    #include <format>
    #include <functional>
    #include <iostream>
    #include <string>
    #include <string_view>
    #include <system_error>
#endif

import beman.big_int;

namespace {

int failure_count = 0;

// Reports through std::cerr rather than std::fprintf(stderr, ...): stderr is a macro,
// and macros do not cross a module boundary, so it is unavailable under `import std`.
void record(const bool ok, const char* const expression, const int line) {
    if (!ok) {
        ++failure_count;
        std::cerr << "quick.test.cpp:" << line << ": FAILED: " << expression << '\n';
    }
}

} // namespace

#define BEMAN_BIG_INT_QUICK_CHECK(...) record(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__, __LINE__)

int main() {
    using BEMAN_BIG_INT_NAMESPACE::big_int;
    using BEMAN_BIG_INT_NAMESPACE::to_string;
    using namespace BEMAN_BIG_INT_NAMESPACE::literals;

    // The operators, on values no builtin integer can hold.
    const big_int a = 12'345'678'901'234'567'890_n;
    const big_int b = 98'765'432'109'876'543'210_n;
    BEMAN_BIG_INT_QUICK_CHECK(to_string(a + b) == "111111111011111111100");
    BEMAN_BIG_INT_QUICK_CHECK(to_string(b - a) == "86419753208641975320");
    BEMAN_BIG_INT_QUICK_CHECK(to_string(a * b) == "1219326311370217952237463801111263526900");
    BEMAN_BIG_INT_QUICK_CHECK(to_string(b / a) == "8");
    BEMAN_BIG_INT_QUICK_CHECK(to_string(b % a) == "900000000090");
    BEMAN_BIG_INT_QUICK_CHECK(a < b && b > a && a != b);
    BEMAN_BIG_INT_QUICK_CHECK(to_string(-a) == "-12345678901234567890");
    BEMAN_BIG_INT_QUICK_CHECK((a & b) + (a | b) == a + b);

    // Wide enough to leave the inline paths and enter the kernels compiled into
    // libbeman.big_int from src/. This is what proves the module target actually
    // links the static library rather than getting everything from inline templates.
    const big_int mersenne = (big_int{1} << 4096) - 1_n;
    const big_int squared  = mersenne * mersenne;
    BEMAN_BIG_INT_QUICK_CHECK(squared == (big_int{1} << 8192) - (big_int{1} << 4097) + 1_n);
    BEMAN_BIG_INT_QUICK_CHECK(squared / mersenne == mersenne);
    BEMAN_BIG_INT_QUICK_CHECK(squared % mersenne == 0);

    // The standard-library integrations, which have to cross the module boundary.
    BEMAN_BIG_INT_QUICK_CHECK(std::hash<big_int>{}(a) == std::hash<big_int>{}(big_int{a}));
    BEMAN_BIG_INT_QUICK_CHECK(std::format("{:#x}", big_int{255}) == "0xff");
    BEMAN_BIG_INT_QUICK_CHECK(std::format("{:>8}", big_int{42}) == "      42");
    BEMAN_BIG_INT_QUICK_CHECK(std::format("{}", big_int{1} << 128) == "340282366920938463463374607431768211456");

    if (failure_count == 0) {
        std::cout << "beman.big_int module quick test: all checks passed\n";
        return 0;
    }
    std::cerr << "beman.big_int module quick test: " << failure_count << " check(s) failed\n";
    return 1;
}
