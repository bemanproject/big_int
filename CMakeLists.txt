# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.30...4.3)

# Must precede project(): sets CMAKE_EXPERIMENTAL_CXX_IMPORT_STD to the UUID
# matching the running CMake version, for BEMAN_BIG_INT_USE_STD_MODULE.
include(infra/cmake/enable-experimental-import-std.cmake)

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

# [CMAKE.NAMESPACE]
# The namespace the library is declared in (BEMAN_BIG_INT_NAMESPACE in
# detail/config.hpp), e.g. mylib::big_int for a project that wraps it. The
# compiled sources and every consumer must agree on it, so it is recorded in
# config_generated.hpp (see below).
set(BEMAN_BIG_INT_NAMESPACE
    "beman::big_int"
    CACHE STRING
    "Qualified namespace the library is declared in. Default: beman::big_int."
)
if(
    NOT BEMAN_BIG_INT_NAMESPACE
        MATCHES
        "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$"
)
    message(
        FATAL_ERROR
        "BEMAN_BIG_INT_NAMESPACE must be a qualified namespace name such as "
        "mylib::big_int, got '${BEMAN_BIG_INT_NAMESPACE}'."
    )
endif()

# [CMAKE.X86_64_BMI2_ADX]
# x86-64 only: both the generic and BMI2+ADX schoolbook kernels are always
# built, but which one the library calls is chosen at compile time and recorded
# in config_generated.hpp. AUTO checks the compiler's own __BMI2__/__ADX__
# feature macros under this build's flags (set by -march=native,
# -march=broadwell or newer, znver*, or -mbmi2 -madx). ON forces the BMI2/ADX kernels: the
# resulting binary requires a CPU with both BMI2 and ADX (Intel Broadwell+ or
# AMD Zen+), or it dies with an illegal instruction -- this is also how MSVC
# opts in, since it defines neither __BMI2__ nor __ADX__. OFF forces the
# generic kernels.
set(BEMAN_BIG_INT_X86_64_BMI2_ADX
    "AUTO"
    CACHE STRING
    "x86-64 only: select the BMI2+ADX schoolbook kernels at compile time. AUTO detects via compiler feature macros; ON always requires a CPU with BMI2 and ADX (Intel Broadwell+/AMD Zen+) or it dies with an illegal instruction, so MSVC needs ON since it defines no __BMI2__/__ADX__; OFF always uses the generic kernels. Default: AUTO. Values: { AUTO, ON, OFF }."
)
set_property(CACHE BEMAN_BIG_INT_X86_64_BMI2_ADX PROPERTY STRINGS AUTO ON OFF)

string(TOUPPER "${BEMAN_BIG_INT_X86_64_BMI2_ADX}" _bigint_bmi2_adx_upper)
if(_bigint_bmi2_adx_upper STREQUAL "AUTO")
    set(_bigint_bmi2_adx_mode AUTO)
elseif(_bigint_bmi2_adx_upper MATCHES "^(ON|TRUE|1)$")
    set(_bigint_bmi2_adx_mode ON)
elseif(_bigint_bmi2_adx_upper MATCHES "^(OFF|FALSE|0)$")
    set(_bigint_bmi2_adx_mode OFF)
else()
    message(
        FATAL_ERROR
        "BEMAN_BIG_INT_X86_64_BMI2_ADX must be one of AUTO, ON, OFF (got: ${BEMAN_BIG_INT_X86_64_BMI2_ADX})"
    )
endif()

# [CMAKE.X86_64_AVX512_IFMA]
# x86-64 only: an additional schoolbook multiply/square kernel pair using
# AVX-512 IFMA (vpmadd52luq/vpmadd52huq, radix 2^52)
set(BEMAN_BIG_INT_X86_64_AVX512_IFMA
    "AUTO"
    CACHE STRING
    "x86-64 only: select the AVX-512 IFMA schoolbook kernels (for large operands; smaller ones still use BMI2/ADX or the generic kernel) at compile time. AUTO detects via compiler feature macros; ON always requires a CPU with AVX-512 F/BW/VL/VBMI/IFMA plus BMI2/ADX (Intel Ice Lake/Tiger Lake/Rocket Lake/Sapphire Rapids or newer, AMD Zen 4 or newer) or it dies with an illegal instruction, so MSVC needs ON since it defines none of those macros itself; OFF never selects them. Default: AUTO. Values: { AUTO, ON, OFF }."
)
set_property(
    CACHE BEMAN_BIG_INT_X86_64_AVX512_IFMA
    PROPERTY STRINGS AUTO ON OFF
)

string(TOUPPER "${BEMAN_BIG_INT_X86_64_AVX512_IFMA}" _bigint_avx512_ifma_upper)
if(_bigint_avx512_ifma_upper STREQUAL "AUTO")
    set(_bigint_avx512_ifma_mode AUTO)
elseif(_bigint_avx512_ifma_upper MATCHES "^(ON|TRUE|1)$")
    set(_bigint_avx512_ifma_mode ON)
elseif(_bigint_avx512_ifma_upper MATCHES "^(OFF|FALSE|0)$")
    set(_bigint_avx512_ifma_mode OFF)
else()
    message(
        FATAL_ERROR
        "BEMAN_BIG_INT_X86_64_AVX512_IFMA must be one of AUTO, ON, OFF (got: ${BEMAN_BIG_INT_X86_64_AVX512_IFMA})"
    )
endif()

# When IFMA is explicitly ON and BMI2_ADX was left at AUTO, promote BMI2_ADX
# to ON too: the IFMA forwarder's small-shape fallback is the BMI2/ADX
# kernel, and MSVC (which needs IFMA=ON since it detects no feature macros)
# cannot auto-detect BMI2/ADX either. An explicit BMI2_ADX=OFF is respected,
# so small shapes then fall back to the generic kernel instead.
if(
    _bigint_avx512_ifma_mode STREQUAL "ON"
    AND _bigint_bmi2_adx_mode STREQUAL "AUTO"
)
    set(_bigint_bmi2_adx_mode ON)
endif()

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
            src/multiply_long_runtime_x86_64_bmi2_adx.s
            src/square_long_runtime_x86_64_bmi2_adx.s
            src/multiply_long_runtime_x86_64_avx512_ifma.s
            src/square_long_runtime_x86_64_avx512_ifma.s
    )
    set(_bigint_x86_64_kernels TRUE)
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
            src/multiply_long_runtime_x86_64_bmi2_adx.asm
            src/square_long_runtime_x86_64_bmi2_adx.asm
            src/multiply_long_runtime_x86_64_avx512_ifma.asm
            src/square_long_runtime_x86_64_avx512_ifma.asm
    )
    set(_bigint_x86_64_kernels TRUE)
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

# Feature detection for config_generated.hpp. The probe compiles the non-CMake
# detection in detail/config.hpp (the #else of its __has_include, whose
# BEMAN_BIG_INT_* macros are collected here) once with this build's compiler,
# standard and flags. Each macro's value is embedded as a string in the probe
# binary and read back, the way CMake itself detects compiler properties, so the
# detection logic lives only in config.hpp. NO_CACHE reruns it on every
# configure, so a changed CMAKE_CXX_FLAGS is never answered from a stale result.
set(_bigint_config_hpp
    "${CMAKE_CURRENT_SOURCE_DIR}/include/beman/big_int/detail/config.hpp"
)
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${_bigint_config_hpp}"
)
block(
    SCOPE_FOR VARIABLES
    PROPAGATE
        _bigint_config_features
        _bigint_compiler_bmi2_adx
        _bigint_compiler_avx512_ifma
)
    file(READ "${_bigint_config_hpp}" _config)
    string(
        FIND "${_config}"
        "#if __has_include(<beman/big_int/detail/config_generated.hpp>)"
        _begin
    )
    if(_begin EQUAL -1)
        message(
            FATAL_ERROR
            "beman.big_int: detail/config.hpp has no config_generated.hpp branch"
        )
    endif()
    string(SUBSTRING "${_config}" ${_begin} -1 _config)
    # Nested directives are indented, so the first unindented #endif closes it.
    string(FIND "${_config}" "\n#endif" _end)
    string(SUBSTRING "${_config}" 0 ${_end} _config)
    string(
        REGEX MATCHALL "#[ ]*define[ ]+BEMAN_BIG_INT_[A-Za-z0-9_]+"
        _defines
        "${_config}"
    )
    set(_macros "")
    foreach(_define IN LISTS _defines)
        string(REGEX REPLACE "^#[ ]*define[ ]+" "" _macro "${_define}")
        list(APPEND _macros "${_macro}")
    endforeach()
    list(REMOVE_DUPLICATES _macros)
    # Build options: set from the CMake options below instead.
    list(
        REMOVE_ITEM _macros
        BEMAN_BIG_INT_X86_64_BMI2_ADX
        BEMAN_BIG_INT_X86_64_AVX512_IFMA
    )

    set(_probe "#include <beman/big_int/detail/config.hpp>\n")
    string(APPEND _probe "#define BEMAN_BIG_INT_PROBE_STR(...) #__VA_ARGS__\n")
    string(
        APPEND _probe
        "#define BEMAN_BIG_INT_PROBE_VALUE(...) BEMAN_BIG_INT_PROBE_STR(__VA_ARGS__)\n"
    )
    string(APPEND _probe "extern const char beman_big_int_probe[];\n")
    string(
        APPEND _probe
        "const char beman_big_int_probe[] = \"BEMAN_BIG_INT_PROBE\"\n"
    )
    foreach(_macro IN LISTS _macros)
        string(
            APPEND _probe
            "#ifdef ${_macro}\n\"[${_macro}=\" BEMAN_BIG_INT_PROBE_VALUE(${_macro}) \"]\"\n#endif\n"
        )
    endforeach()
    string(
        APPEND _probe
        ";\nint main(int argc, char**) { return beman_big_int_probe[argc]; }\n"
    )

    # The library is C++23 at minimum; a newer CMAKE_CXX_STANDARD is honored.
    set(_std 23)
    if(
        DEFINED CMAKE_CXX_STANDARD
        AND NOT CMAKE_CXX_STANDARD MATCHES "^(98|11|14|17|20|23)$"
    )
        set(_std "${CMAKE_CXX_STANDARD}")
    endif()
    if(CMAKE_BUILD_TYPE)
        set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
    endif()
    set(_binary
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/beman_big_int_probe.bin"
    )
    try_compile(
        _compiled
        SOURCE_FROM_CONTENT config_probe.cpp "${_probe}"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_SOURCE_DIR}/include"
        CXX_STANDARD ${_std}
        CXX_STANDARD_REQUIRED ON
        NO_CACHE
        OUTPUT_VARIABLE _output
        COPY_FILE "${_binary}"
    )
    if(NOT _compiled)
        message(
            FATAL_ERROR
            "beman.big_int: the configuration probe failed to compile:\n${_output}"
        )
    endif()
    file(STRINGS "${_binary}" _strings REGEX "BEMAN_BIG_INT_PROBE")
    list(REMOVE_DUPLICATES _strings)
    list(LENGTH _strings _count)
    if(NOT _count EQUAL 1)
        message(
            FATAL_ERROR
            "beman.big_int: expected one configuration probe result, found ${_count}. "
            "A build for several architectures at once (e.g. CMAKE_OSX_ARCHITECTURES "
            "with more than one entry) is not supported."
        )
    endif()

    set(_bigint_config_features "")
    set(_found "")
    foreach(_macro IN LISTS _macros)
        if(_strings MATCHES "\\[${_macro}=([^]]*)\\]")
            list(APPEND _found "${_macro}")
            if(CMAKE_MATCH_1 STREQUAL "")
                string(APPEND _bigint_config_features "#define ${_macro}\n")
            else()
                string(
                    APPEND _bigint_config_features
                    "#define ${_macro} ${CMAKE_MATCH_1}\n"
                )
            endif()
        endif()
    endforeach()
    set(_bigint_compiler_bmi2_adx 0)
    set(_bigint_compiler_avx512_ifma 0)
    if("BEMAN_BIG_INT_HAS_BMI2_ADX" IN_LIST _found)
        set(_bigint_compiler_bmi2_adx 1)
        if("BEMAN_BIG_INT_HAS_AVX512_IFMA" IN_LIST _found)
            set(_bigint_compiler_avx512_ifma 1)
        endif()
    endif()
endblock()

# Resolve the x86-64 kernel choices to 0/1 for config_generated.hpp. AUTO takes
# what the probe found, i.e. the compiler's own feature macros under this
# build's flags (e.g. -march=native in CMAKE_CXX_FLAGS).
set(_bigint_config_bmi2_adx 0)
set(_bigint_config_avx512_ifma 0)
if(_bigint_x86_64_kernels)
    if(_bigint_bmi2_adx_mode STREQUAL "ON")
        set(_bigint_config_bmi2_adx 1)
    elseif(_bigint_bmi2_adx_mode STREQUAL "AUTO")
        set(_bigint_config_bmi2_adx ${_bigint_compiler_bmi2_adx})
    endif()
    if(_bigint_avx512_ifma_mode STREQUAL "ON")
        set(_bigint_config_avx512_ifma 1)
    elseif(_bigint_avx512_ifma_mode STREQUAL "AUTO")
        set(_bigint_config_avx512_ifma ${_bigint_compiler_avx512_ifma})
    endif()
    message(
        STATUS
        "beman.big_int: x86-64 BMI2/ADX kernels ${_bigint_config_bmi2_adx}, AVX-512 IFMA kernels ${_bigint_config_avx512_ifma}"
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
    # consumer must agree on the path: config_generated.hpp records it.
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

# config_generated.hpp records the options above for detail/config.hpp, so the
# compiled library, the module and every consumer, installed ones included, agree
# on them. It is generated outside the source include/ tree, so a build that does
# not use CMake never picks up a stale copy.
set(_bigint_generated_include "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    include/beman/big_int/detail/config_generated.hpp.in
    "${_bigint_generated_include}/beman/big_int/detail/config_generated.hpp"
    @ONLY
)
target_sources(
    beman.big_int
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${_bigint_generated_include}"
            FILES
                "${_bigint_generated_include}/beman/big_int/detail/config_generated.hpp"
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

# The examples are written against beman::big_int the way a user would spell it,
# so they only build with the default namespace.
if(
    BEMAN_BIG_INT_BUILD_EXAMPLES
    AND BEMAN_BIG_INT_NAMESPACE STREQUAL "beman::big_int"
)
    add_subdirectory(examples)
endif()

# Benchmarks live under the tests tree but do not depend on the test target, so
# they are added directly here and can be built with tests disabled.
if(BEMAN_BIG_INT_BUILD_BENCHMARKS)
    add_subdirectory(tests/beman/big_int/perf)
endif()
