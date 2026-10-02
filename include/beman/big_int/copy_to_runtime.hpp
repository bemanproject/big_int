// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_COPY_TO_RUNTIME_HPP
#define BEMAN_BIG_INT_COPY_TO_RUNTIME_HPP

// Macros do not cross module boundaries, so BEMAN_BIG_INT_COPY_TO_RUNTIME lives in its
// own self-contained header (no includes, no declarations) rather than in big_int.hpp.
// A module consumer includes this header alongside `import beman.big_int;` to get it.

// Mirrors the default in detail/config.hpp, which a module consumer never sees.
#ifndef BEMAN_BIG_INT_NAMESPACE
    #define BEMAN_BIG_INT_NAMESPACE beman::big_int
#endif // BEMAN_BIG_INT_NAMESPACE

// A convenience macro rather than calling a stateless lambda
#define BEMAN_BIG_INT_COPY_TO_RUNTIME(...) \
    (::BEMAN_BIG_INT_NAMESPACE::copy_to_runtime<decltype([]() { return (__VA_ARGS__); })>())

#endif // BEMAN_BIG_INT_COPY_TO_RUNTIME_HPP
