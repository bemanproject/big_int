# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.30...4.3)

project(
    beman.big_int
    DESCRIPTION "Reference implementation of std::big_int"
    LANGUAGES CXX
    VERSION 0.1.0
)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL
        https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# [CMAKE.SKIP_TESTS]
option(
    BEMAN_BIG_INT_BUILD_TESTS
    "Enable building tests and test infrastructure. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_EXAMPLES]
option(
    BEMAN_BIG_INT_BUILD_EXAMPLES
    "Enable building examples. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_BENCHMARKS]
# Off by default: it fetches google/benchmark and is only meaningful in a release
# build. Enable with a release preset to compare big_int against the builtin
# integer types (see tests/beman/big_int/perf).
option(
    BEMAN_BIG_INT_BUILD_BENCHMARKS
    "Enable building the Google Benchmark micro-benchmarks (big_int vs builtin integer types). Default: OFF. Values: { ON, OFF }."
    OFF
)

# [CMAKE.SKIP_MODULE]
# Off by default: building the C++ module (module/big_int.cppm) requires a
# scanning generator (Ninja or Visual Studio) to discover `import`/`export`
# dependencies, which not every generator/toolchain combination provides.
# AppleClang in particular is not supported at all: CMake's own
# Modules/Compiler/AppleClang-CXX.cmake never defines CMAKE_CXX_SCANDEP_SOURCE,
# CMAKE_CXX_MODULE_MAP_FORMAT, or CMAKE_CXX_COMPILE_BMI, and cmake-cxxmodules(7)
# omits AppleClang entirely, so turning this on there fails at generate time.
# Enable it with one of the "-module" presets (see CMakePresets.json) on GCC,
# Clang, or MSVC.
option(
    BEMAN_BIG_INT_BUILD_MODULE
    "Build the beman.big_int C++ module (module/big_int.cppm) alongside the header/library form. Requires a scanning generator (Ninja or Visual Studio); not supported by AppleClang. Default: OFF. Values: { ON, OFF }."
    OFF
)

# [CMAKE.SKIP_MSVC_DEBUGGER_ADDIN]
# The Visual Studio debugger cannot evaluate arbitrary-precision arithmetic, so a
# plain .natvis can only render a big_int in decimal up to 64 bits. This builds
# extra/big_int_printer_msvc.cpp into an expression-evaluator add-in DLL that reads
# the limbs out of the debuggee and runs to_chars over them, giving exact decimal at
# any width. Off by default: it is a developer tool, and it only means anything on
# MSVC.
option(
    BEMAN_BIG_INT_BUILD_MSVC_DEBUGGER_ADDIN
    "Build and install the Visual Studio debugger expression-evaluator add-in DLL (extra/big_int_printer_msvc.cpp). MSVC only. Default: OFF. Values: { ON, OFF }."
    OFF
)

# for find of beman-install-library
include(infra/cmake/beman-install-library.cmake)

add_library(
    beman.big_int
    src/divide.cpp
    src/karatsuba.cpp
    src/mul_dispatch.cpp
    src/mulmod_bnm1.cpp
    src/toom_cook_3.cpp
    src/toom_cook_4.cpp
    src/toom_cook_6_5.cpp
    src/toom_cook_8_5.cpp
)
add_library(beman::big_int ALIAS beman.big_int)
target_compile_features(beman.big_int PUBLIC cxx_std_23)

# Only enable assembly and add the assembly source when using GCC, Clang, or
# MSVC on x86_64 or AArch64. This avoids trying to compile .s/.S files with
# toolchains that may not support the same assembler syntax or where we don't
# want the assembly unit. The MSVC branches key on
# CMAKE_CXX_COMPILER_ARCHITECTURE_ID rather than CMAKE_SYSTEM_PROCESSOR: a
# cross build (e.g. -A ARM64 from an x86_64 host) leaves CMAKE_SYSTEM_PROCESSOR
# at the host value, which would otherwise pick the wrong MASM files.
# A build forcing 32-bit limbs (BEMAN_BIG_INT_FORCED_LIMB_WIDTH=32) still
# assembles these, but never calls them: see detail/config.hpp.
if(
    (
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
    )
    AND (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
)
    enable_language(ASM)
    target_sources(
        beman.big_int
        PRIVATE
            src/multiply_long_runtime_x86_64_generic.s
            src/square_long_runtime_x86_64_generic.s
    )
elseif(
    (
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
    )
    AND (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
)
    enable_language(ASM)
    target_sources(
        beman.big_int
        PRIVATE
            src/multiply_long_runtime_aarch64_generic.S
            src/square_long_runtime_aarch64_generic.S
    )
elseif(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    enable_language(ASM_MASM)
    target_sources(
        beman.big_int
        PRIVATE
            src/multiply_long_runtime_x86_64_generic.asm
            src/square_long_runtime_x86_64_generic.asm
    )
elseif(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
    # ASM_MARMASM requires CMake >= 3.26; the project minimum (3.30) already covers it.
    enable_language(ASM_MARMASM)
    target_sources(
        beman.big_int
        PRIVATE
            src/multiply_long_runtime_aarch64_generic.asm
            src/square_long_runtime_aarch64_generic.asm
    )
endif()

# Multiplication's FFT tier. By default it is an integer Montgomery NTT
# (src/ntt.cpp, src/fft_mul.cpp): pure integer arithmetic, exact on every
# conforming compiler with no floating-point-environment assumptions.
#
# BEMAN_BIG_INT_SIMD_MUL switches it to the faster double-precision FP NTT with
# hand-written NEON/AVX2 SIMD kernels (src/ntt_fp_*.cpp, src/fft_mul_fp.cpp). That
# path is exact ONLY under round-to-nearest with no fast-math / FMA-contraction;
# this build forbids those per TU below, but if you build with another system you
# must guarantee that floating-point environment yourself. Opt in only then.
option(
    BEMAN_BIG_INT_SIMD_MUL
    "Use the double-precision FP NTT with SIMD (NEON/AVX2) for FFT multiplication. Requires round-to-nearest and no fast-math / FMA contraction. Default: OFF (the integer NTT)."
    OFF
)

if(BEMAN_BIG_INT_SIMD_MUL)
    # The dispatch lives in a header (mul_impl.hpp), so the library and every
    # consumer must agree on the path: propagate the macro PUBLICly.
    target_compile_definitions(beman.big_int PUBLIC BEMAN_BIG_INT_SIMD_MUL)

    target_sources(
        beman.big_int
        PRIVATE src/ntt_fp_scalar.cpp src/ntt_fp_dispatch.cpp src/fft_mul_fp.cpp
    )

    # The FP NTT relies on round-to-nearest and on its explicit FMAs being the ONLY
    # fused operations (the exactness proof needs h = round(a*b) and l = fma(a,b,-h)
    # as distinct roundings). Forbid fast-math / FP contraction on every FP TU.
    set(_bigint_fp_strict
        "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off;-fno-fast-math>$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>"
    )
    set_source_files_properties(
        src/ntt_fp_scalar.cpp
        src/ntt_fp_dispatch.cpp
        src/fft_mul_fp.cpp
        PROPERTIES COMPILE_OPTIONS "${_bigint_fp_strict}"
    )

    # Hand-written SIMD kernels, one ISA per TU, gated by target architecture.
    # AArch64: NEON is mandatory baseline (no runtime dispatch, no extra ISA flags).
    # x86-64: the AVX2+FMA kernel TU gets -mavx2 -mfma (or /arch:AVX2); the dispatcher
    # (baseline ISA) selects it only after a runtime AVX2 check, so AVX instructions
    # stay confined to this one TU and never run on a CPU lacking them.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_sources(beman.big_int PRIVATE src/ntt_fp_avx2.cpp)
        if(MSVC)
            set_source_files_properties(
                src/ntt_fp_avx2.cpp
                PROPERTIES COMPILE_OPTIONS "/arch:AVX2;${_bigint_fp_strict}"
            )
        else()
            set_source_files_properties(
                src/ntt_fp_avx2.cpp
                PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;${_bigint_fp_strict}"
            )
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        target_sources(beman.big_int PRIVATE src/ntt_fp_neon.cpp)
        set_source_files_properties(
            src/ntt_fp_neon.cpp
            PROPERTIES COMPILE_OPTIONS "${_bigint_fp_strict}"
        )
    endif()
else()
    # Default: the integer Montgomery NTT. No floating-point-environment flags.
    target_sources(beman.big_int PRIVATE src/ntt.cpp src/fft_mul.cpp)
endif()

target_sources(
    beman.big_int
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/beman/big_int.hpp"
)

set_target_properties(
    beman.big_int
    PROPERTIES VERIFY_INTERFACE_HEADER_SETS ${PROJECT_IS_TOP_LEVEL}
)

add_subdirectory(include/beman/big_int)

# MSVC debugger visualizers. See the "Debugger Visualizers" documentation page.
#
# extra/big_int.natvis needs no DLL but, being limited to the debugger's own
# arithmetic, only renders decimal up to 64 bits. `/NATVIS:` embeds it into the PDB
# of whatever binary links this library, so it applies with no per-user setup. Only
# the link of the final executable or DLL can embed a visualizer, and only
# configurations that emit a PDB accept the switch, hence INTERFACE and the config
# guard. Consumers of an installed package pass the switch themselves or copy the
# file into their Visualizers directory.
if(MSVC)
    target_link_options(
        beman.big_int
        INTERFACE
            "$<BUILD_INTERFACE:$<$<CONFIG:Debug,RelWithDebInfo>:/NATVIS:${CMAKE_CURRENT_SOURCE_DIR}/extra/big_int.natvis>>"
    )
    install(
        FILES extra/big_int.natvis
        DESTINATION "${CMAKE_INSTALL_DATADIR}/beman.big_int"
        COMPONENT big_int_Development
    )
endif()

# The add-in DLL and its companion .natvis, which together render full decimal at
# any width. The DLL is loaded by the debugger but reads the debuggee's memory and
# links this library's kernels, so it must be built for the same architecture and
# the same options as the program being debugged.
if(MSVC AND BEMAN_BIG_INT_BUILD_MSVC_DEBUGGER_ADDIN)
    add_library(
        beman.big_int.msvc_debugger_addin
        MODULE
        extra/big_int_printer_msvc.cpp
    )
    # The Export names in the .natvis name this DLL, so it must not be decorated.
    set_target_properties(
        beman.big_int.msvc_debugger_addin
        PROPERTIES OUTPUT_NAME big_int_printer_msvc PREFIX ""
    )
    target_link_libraries(
        beman.big_int.msvc_debugger_addin
        PRIVATE beman.big_int
    )

    install(
        TARGETS beman.big_int.msvc_debugger_addin
        LIBRARY
            DESTINATION "${CMAKE_INSTALL_DATADIR}/beman.big_int"
            COMPONENT big_int_Development
    )
    install(
        FILES extra/big_int_printer_msvc.natvis
        DESTINATION "${CMAKE_INSTALL_DATADIR}/beman.big_int"
        COMPONENT big_int_Development
    )

    # Convenience: drop both files where the debugger looks for them. The default
    # is the per-user Visualizers directory of Visual Studio 2022; override it for
    # another version or for the machine-wide directory.
    set(BEMAN_BIG_INT_VISUALIZERS_DIR
        "$ENV{USERPROFILE}/Documents/Visual Studio 2022/Visualizers"
        CACHE PATH
        "Directory the `big_int_install_visualizers` target copies big_int_printer_msvc.{dll,natvis} into."
    )
    add_custom_target(
        big_int_install_visualizers
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            "${BEMAN_BIG_INT_VISUALIZERS_DIR}"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:beman.big_int.msvc_debugger_addin>"
            "${CMAKE_CURRENT_SOURCE_DIR}/extra/big_int_printer_msvc.natvis"
            "${BEMAN_BIG_INT_VISUALIZERS_DIR}"
        DEPENDS beman.big_int.msvc_debugger_addin
        COMMENT
            "Copying the big_int debugger add-in into ${BEMAN_BIG_INT_VISUALIZERS_DIR}"
        VERBATIM
    )
endif()

set(BEMAN_BIG_INT_INSTALL_TARGETS beman.big_int)

# The module target must exist before the beman_install_library() call below so
# it joins the same export set and can resolve its beman::big_int dependency
# from within it.
if(BEMAN_BIG_INT_BUILD_MODULE)
    add_subdirectory(module)
    list(APPEND BEMAN_BIG_INT_INSTALL_TARGETS beman.big_int_module)
endif()

beman_install_library(beman.big_int TARGETS ${BEMAN_BIG_INT_INSTALL_TARGETS})

if(BEMAN_BIG_INT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/beman/big_int)

    # Kept out of tests/beman/big_int/ itself: that directory globs *.test.cpp
    # and would build module consumers unconditionally. The EXISTS check lets
    # this land ahead of the wave that adds the directory.
    if(
        BEMAN_BIG_INT_BUILD_MODULE
        AND EXISTS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/beman/big_int/module/CMakeLists.txt"
    )
        add_subdirectory(tests/beman/big_int/module)
    endif()
endif()

if(BEMAN_BIG_INT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Benchmarks live under the tests tree but do not depend on the test target, so
# they are added directly here and can be built with tests disabled.
if(BEMAN_BIG_INT_BUILD_BENCHMARKS)
    add_subdirectory(tests/beman/big_int/perf)
endif()
