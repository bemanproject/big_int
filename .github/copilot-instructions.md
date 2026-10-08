# Copilot Instructions For beman.big_int

Use this file as the primary source of truth for working in this repository.
Trust these instructions first,
and only search the repo when information here is incomplete or has become incorrect.

## Repository Summary

- Project: `beman.big_int`, a header-first C++ library implementing proposed `std::big_int` ([P4444R0](https://isocpp.org/files/papers/D4444R0.html)) extension to the C++ standard library.
- Project Goal: Provide reference implementation of `std::big_int` to support standardization efforts and serve as a resource for users and implementers.
- Type: CMake-based C++ library with tests and examples.
- Main language: C++23 (supports C++23/C++26 in CI matrix).
- Build system: CMake presets + Ninja.
- Test framework: GoogleTest via `find_package(GTest)`.
- Additional test dependency: Boost.Multiprecision via `find_package(boost_multiprecision)`.
- Dependencies come from the environment, or from `lockfile.json` through
  `-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=./infra/cmake/use-fetch-content.cmake` (set by the presets).
- Lint/format: pre-commit hooks (`clang-format`, `gersemi`, `codespell`, YAML/whitespace checks).
- CI: GitHub Actions workflows in `.github/workflows/`, mostly through reusable workflows from `bemanproject/infra-workflows`.

## High-Level Layout (Where To Look First)

- `include/beman/big_int/basic_big_int.hpp`: core public API and most implementation.
- `include/beman/big_int/detail/`: low-level helpers and platform-specific code.
- `tests/beman/big_int/*.test.cpp`: unit and regression tests.
- `tests/beman/big_int/CMakeLists.txt`: test target generation and warning policy.
- `examples/`: sample executables.
- `CMakeLists.txt`: top-level options/targets and subdirectories.
- `CMakePresets.json`: canonical configure/build/test/workflow presets.
- `.pre-commit-config.yaml`, `.clang-format`, `.gersemirc`: lint/format policy.
- `.github/workflows/ci_tests.yml`: CI build/test matrix.
- `.github/workflows/pre-commit-check.yml`: lint gate in CI.

## Tool Versions

`cmake` 3.30+ and C++23-capable compiler are required to build and test the project.

## Bootstrap, Build, Test, Run, Lint (Validated Command Sequences)

Always run commands from repository root.

### Recommended Fast Path (Most Reliable)

1. List presets (optional):
   - `cmake --list-presets=workflow`
2. Configure + build + test in one command; one of:
   - `cmake --workflow --preset gcc-debug`
   - `cmake --workflow --preset llvm-debug`

Validated result: success; all tests passed.

### Manual Configure/Build/Test (When Not Using Workflow Presets)

1. Configure:
   - `cmake -B build/manual-debug -S . -DCMAKE_CXX_STANDARD=23`
2. Build:
   - `cmake --build build/manual-debug -j`
3. Test:
   - `ctest --test-dir build/manual-debug`

Validated result: success.

Important ordering rule:

- Always build before `ctest`.
- Running `ctest` immediately after configure (before build) failed with 23 `*_NOT_BUILT` tests and exit code 8.

### Run Example Executable

Pointless for now.

### Lint/Format/Static Checks

- `pre-commit run --all-files`

Validated result: all hooks passed (`clang-format`, `gersemi`, `codespell`, whitespace/YAML checks).

## Known Command Pitfalls And Mitigations

- Missing `rg` (`ripgrep`) in some environments.
  - Observed: `rg` command not found.
  - Mitigation: use `grep -RIn ...` fallback for text search, or install `ripgrep`.

- Artificially short command timeout can terminate orchestration early.
  - Observed: `cmake --workflow --preset gcc-release` timed out at 1 second tool timeout.
  - Mitigation: for workflow builds/tests, use >=120s timeout in constrained runners; ~10-11s was observed for full configure/build/test in this environment.

- The googletest pinned in `lockfile.json` emits CMake deprecation warnings for its old minimum version.
  - Observed during configure; not fatal.
  - Mitigation: treat as upstream dependency warning unless CI policy changes.

## CI And Pre-Checkin Parity

Before proposing a PR, always run:

1. `cmake --workflow --preset gcc-debug`
2. `cmake --workflow --preset llvm-debug`
3. `pre-commit run --all-files`

Why these two:

- Mirrors local compile+test and lint gates that most commonly reject PRs.
- CI matrix in `.github/workflows/ci_tests.yml` spans GCC/Clang/AppleClang/MSVC and multiple standards/sanitizer modes; local replication of every axis is not expected, but `gcc-debug` plus lint catches most immediate regressions.

Referenced CI workflows:

- `.github/workflows/ci_tests.yml`
- `.github/workflows/pre-commit-check.yml`
- `.github/workflows/pre-commit-update.yml`

## Project Architecture Notes For Faster Edits

- Core implementation is concentrated in one large header (`basic_big_int.hpp`) and helper headers under `detail/`.
- Tests are automatically globbed from `tests/beman/big_int/*.test.cpp`; adding a new `*.test.cpp` file generally auto-adds a new test executable through `tests/beman/big_int/CMakeLists.txt`.
- Tests compile with strict warnings (`-Werror` and many extra warning flags on GCC/Clang). Warning-clean code is required.
- Top-level options:
  - `BEMAN_BIG_INT_BUILD_TESTS` (default ON when top-level)
  - `BEMAN_BIG_INT_BUILD_EXAMPLES` (default ON when top-level)

## Root Inventory (Quick Orientation)

Root files and directories:

- `.clang-format`, `.gersemirc`, `.markdownlint.yaml`, `.pre-commit-config.yaml`
- `CMakeLists.txt`, `CMakePresets.json`, `lockfile.json`
- `README.md`, `CONTRIBUTING.md`, `LICENSE*`
- `.github/`, `include/`, `tests/`, `examples/`, `infra/`, `extra/`, `build/`

Notes on less-obvious directories:

- `infra/`: Beman infrastructure submodule-like content (toolchain/modules). Do not modify.
- `extra/`: debugger pretty-printers (`gdb`/`lldb`) for big_int and int128 helper types.

## Agent Operating Rules For This Repo

- Prefer VSCode CMake tools over terminal commands.
- Prefer CMake workflow presets over ad-hoc commands.
- Always run build before test.
- Always run `pre-commit run --all-files` before finishing.
- Prefer editing within existing architecture (core header + detail helpers + tests) rather than adding new top-level structure.
- Trust this file first; only perform repo-wide search when this guidance is insufficient or contradicted by current files.
