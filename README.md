# beman.big_int: Reference implementation of std::big_int

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

<p align="center">
    <a href="https://github.com/bemanproject/beman/blob/main/images/badges">
        <img src="https://github.com/bemanproject/beman/blob/main/images/badges/beman_badge-beman_library_under_development.svg" alt="Library Status"></a>
    <a href="https://github.com/bemanproject/beman/blob/main/images/badges">
        <img src="https://github.com/bemanproject/beman/blob/main/images/badges/cpp29.svg" alt="Standard Target"></a>
    <a href="https://github.com/bemanproject/big_int/actions">
        <img src="https://github.com/bemanproject/big_int/actions/workflows/ci_tests.yml/badge.svg" alt="Continuous Integration Tests"></a>
    <a href="https://github.com/bemanproject/big_int/actions">
        <img src="https://github.com/bemanproject/big_int/actions/workflows/pre-commit-check.yml/badge.svg" alt="Lint Check (pre-commit)"></a>
    <a href="https://github.com/bemanproject/big_int/actions">
        <img src="https://github.com/bemanproject/big_int/actions/workflows/fuzz.yml/badge.svg" alt="fuzz"></a>
    <a href="https://github.com/bemanproject/big_int/actions">
        <img src="https://github.com/bemanproject/big_int/actions/workflows/metal.yml/badge.svg" alt="metal"></a>
    <a href="https://coveralls.io/github/bemanproject/big_int?branch=main">
        <img src="https://coveralls.io/repos/github/bemanproject/big_int/badge.svg?branch=main" alt="Coverage"></a>
    <a href="https://github.com/bemanproject/big_int/actions">
        <img src="https://github.com/bemanproject/big_int/actions/workflows/publish.yml/badge.svg" alt="publish gh-pages"></a>
</p>

**Implements**: `std::big_int` proposed in [P4444](https://isocpp.org/files/papers/D4444R0.html).

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

`beman.big_int` is a minimal C++ library conforming to [The Beman Standard](https://github.com/bemanproject/beman/blob/main/docs/beman_standard.md).
This can be used as a template for those intending to write Beman libraries.
It may also find use as a minimal and modern  C++ project structure.

## Documentation

For the full library documentation please see our [GitHub Pages Site](https://bemanproject.github.io/big_int/build_and_usage.html).

## License

`beman.big_int` is dual-licensed under Boost Software License 1.0 and the Apache License v2.0 with LLVM Exceptions.

## Building and Usage

For build requirements and supported platforms, building, the optional
SIMD-accelerated multiplication, installation, CMake integration, and basic
usage, see the
[Build and Usage](https://bemanproject.github.io/big_int/build_and_usage.html) documentation.

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## References

See the [References](https://bemanproject.github.io/big_int/references.html) documentation for the
works underpinning the algorithms used in this library.
