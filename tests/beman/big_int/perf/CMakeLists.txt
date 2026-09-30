# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

# End-to-end shape sweep for tuning (see shape_sweep.cpp). It needs only the
# library, not Google Benchmark. BEMAN_BIG_INT_SWEEP_GMP adds a GMP column and
# needs libgmp (no gmp.h: the harness declares the few mpn entry points itself).
option(
    BEMAN_BIG_INT_SWEEP_GMP
    "Link libgmp into shape_sweep for its --gmp column. Default: OFF. Values: { ON, OFF }."
    OFF
)

add_executable(beman.big_int.benchmarks.shape_sweep shape_sweep.cpp)
target_link_libraries(
    beman.big_int.benchmarks.shape_sweep
    PRIVATE beman::big_int
)
# The harness prints the build configuration in its #const line; refuse manual
# builds that cannot say which flags they used (see shape_sweep.cpp).
string(TOUPPER "${CMAKE_BUILD_TYPE}" _sweep_build_type_upper)
set(_sweep_flags
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_sweep_build_type_upper}}"
)
string(REPLACE "\\" "/" _sweep_flags "${_sweep_flags}")
string(REPLACE "\"" "'" _sweep_flags "${_sweep_flags}")
string(STRIP "${_sweep_flags}" _sweep_flags)
file(
    GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shape_sweep_build_info.hpp"
    CONTENT
        "#define BEMAN_BIG_INT_SWEEP_BUILD_TYPE \"$<CONFIG>\"
#define BEMAN_BIG_INT_SWEEP_CXX_FLAGS \"${_sweep_flags} defs:$<JOIN:$<TARGET_PROPERTY:beman.big_int,INTERFACE_COMPILE_DEFINITIONS>, >\"
"
)
target_compile_definitions(
    beman.big_int.benchmarks.shape_sweep
    PRIVATE BEMAN_BIG_INT_SWEEP_VIA_CMAKE
)
target_include_directories(
    beman.big_int.benchmarks.shape_sweep
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
)
if(BEMAN_BIG_INT_SWEEP_GMP)
    find_library(
        BEMAN_BIG_INT_GMP_LIBRARY
        gmp
        HINTS $ENV{HOME}/tools/gmp/lib /opt/homebrew/lib /usr/local/lib
        REQUIRED
    )
    get_filename_component(
        _sweep_gmp_dir
        "${BEMAN_BIG_INT_GMP_LIBRARY}"
        DIRECTORY
    )
    target_compile_definitions(
        beman.big_int.benchmarks.shape_sweep
        PRIVATE BEMAN_BIG_INT_SWEEP_GMP
    )
    target_link_libraries(
        beman.big_int.benchmarks.shape_sweep
        PRIVATE ${BEMAN_BIG_INT_GMP_LIBRARY}
    )
    # Load the GMP that was linked (e.g. ~/tools/gmp), not a system libgmp.
    set_target_properties(
        beman.big_int.benchmarks.shape_sweep
        PROPERTIES BUILD_RPATH "${_sweep_gmp_dir}"
    )
endif()
if(
    CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
)
    target_compile_options(
        beman.big_int.benchmarks.shape_sweep
        PRIVATE -Wall -Wextra
    )
elseif(MSVC)
    target_compile_options(beman.big_int.benchmarks.shape_sweep PRIVATE /W4)
endif()

# Scratch variant builds (build/tune/bin/variant.sh) need only shape_sweep and
# must not fetch Google Benchmark.
option(
    BEMAN_BIG_INT_SHAPE_SWEEP_ONLY
    "Build only shape_sweep from this directory (skips Google Benchmark). Default: OFF. Values: { ON, OFF }."
    OFF
)
if(BEMAN_BIG_INT_SHAPE_SWEEP_ONLY)
    return()
endif()

include(FetchContent)

# Pull in Google Benchmark for the big_int-vs-builtin micro-benchmarks, mirroring
# how the top-level build fetches GoogleTest. Only fetched when
# BEMAN_BIG_INT_BUILD_BENCHMARKS is enabled (this directory is added from the
# top-level CMakeLists under that option).
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
# Google Benchmark builds itself with -Werror by default, which can break on
# newer compilers than its release was tested against; turn it off.
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
    URL_HASH
        SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
FetchContent_MakeAvailable(googlebenchmark)

# Benchmark sources use the `.gbench.cpp` suffix so they are never confused with
# the standalone GMP/boost comparison programs (`*.perf.cpp`, `*.limbs.cpp`) that
# also live in this directory and are compiled by hand.
file(GLOB BENCHMARK_SOURCES "*.gbench.cpp")

foreach(benchmark_source ${BENCHMARK_SOURCES})
    get_filename_component(benchmark_name ${benchmark_source} NAME_WE)
    set(benchmark_target beman.big_int.benchmarks.${benchmark_name})

    add_executable(${benchmark_target})
    target_sources(${benchmark_target} PRIVATE ${benchmark_source})
    target_link_libraries(
        ${benchmark_target}
        PRIVATE beman::big_int benchmark::benchmark
    )
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
    )
        target_compile_options(${benchmark_target} PRIVATE -Wall -Wextra)
    elseif(MSVC)
        target_compile_options(${benchmark_target} PRIVATE /W4)
    endif()
endforeach()
