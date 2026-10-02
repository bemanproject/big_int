// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "fuzz_common.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return ::BEMAN_BIG_INT_NAMESPACE::fuzz::run(std::divides<>{}, data, size, /*skip_zero_rhs=*/true);
}
