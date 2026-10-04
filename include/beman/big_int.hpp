// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_HPP
#define BEMAN_BIG_INT_HPP

// Convenience header that pulls in the entire beman::big_int library.
// Include this when you want the arbitrary-precision integer type together
// with its text conversions, numeric limits, user-defined literals, and
// std::format support.
// Individual public headers may be included on their own for finer-grained
// dependencies.

#include <beman/big_int/basic_big_int.hpp>
#include <beman/big_int/charconv.hpp>
#include <beman/big_int/string.hpp>
#include <beman/big_int/limits.hpp>
#include <beman/big_int/literals.hpp>
#include <beman/big_int/format.hpp>
#include <beman/big_int/numeric.hpp>

#endif // BEMAN_BIG_INT_HPP
