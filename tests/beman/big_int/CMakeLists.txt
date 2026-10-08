# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

include(GoogleTest)
include(FetchContent)

option(
    BEMAN_BIG_INT_FUZZ
    "Build LibFuzzer-based parity fuzzers for beman::big_int (requires Clang). Default: OFF."
    OFF
)

file(GLOB TEST_SOURCES "*.test.cpp")

# Pull in Boost.Multiprecision for parity tests that cross-check beman::big_int
# against boost::cpp_int.
# The standalone release of this library is self-contained

FetchContent_Declare(
    boost_multiprecision
    URL
        https://github.com/boostorg/multiprecision/archive/refs/tags/Boost_1_89_0.tar.gz
    URL_HASH
        SHA256=d6000cc58594fead142eee4bf3a1aec39bf144dcb6accd8f4b8e6d780d258482
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
FetchContent_MakeAvailable(boost_multiprecision)

# The project's test warning set.
function(beman_big_int_test_warnings test_target)
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
    )
        # These options work with both GCC and Clang
        target_compile_options(
            ${test_target}
            PRIVATE
                -Werror
                -Wall
                -Wextra
                -Wpedantic
                -Wsign-conversion
                -Wconversion
                -Wundef
                -Wold-style-cast
                -Wfloat-equal
                -Wzero-as-null-pointer-constant
                -Wshadow
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC specific options that are a little bit more aggressive
        target_compile_options(
            ${test_target}
            PRIVATE
                -Wundef
                -Wduplicated-branches
                -Wshadow
                -Wextra-semi
                -Wuseless-cast
                -Wpadded
        )
    endif()
    if(MSVC)
        # Use W4 instead of Wall because it includes noise from system headers
        # /WX = -Werror
        target_compile_options(${test_target} PRIVATE /WX /W4)
    endif()
endfunction()

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(test_target beman.big_int.tests.${test_name})

    add_executable(${test_target})
    target_sources(${test_target} PRIVATE ${test_source})
    target_link_libraries(
        ${test_target}
        PRIVATE beman::big_int GTest::gtest_main Boost::multiprecision
    )
    beman_big_int_test_warnings(${test_target})
    gtest_discover_tests(${test_target} DISCOVERY_TIMEOUT 60)
endforeach()

# What CMake resolved for config_generated.hpp, for config.test.cpp to check
# against what the headers actually see.
target_compile_definitions(
    beman.big_int.tests.config
    PRIVATE
        "BEMAN_BIG_INT_TEST_EXPECTED_NAMESPACE=\"${BEMAN_BIG_INT_NAMESPACE}\""
        BEMAN_BIG_INT_TEST_EXPECTED_BMI2_ADX=${_bigint_config_bmi2_adx}
        BEMAN_BIG_INT_TEST_EXPECTED_AVX512_IFMA=${_bigint_config_avx512_ifma}
        BEMAN_BIG_INT_TEST_EXPECTED_SIMD_MUL=$<BOOL:${BEMAN_BIG_INT_SIMD_MUL}>
)

# detail/config.hpp's defaults for a build that does not use CMake. These targets
# deliberately do not link beman::big_int, so config_generated.hpp is not on
# their include path; config_defaults.cpp only checks macros, so nothing needs
# the compiled library.
function(beman_big_int_config_defaults_test name)
    cmake_parse_arguments(arg "" "" "DEFINITIONS;OPTIONS" ${ARGN})
    set(test_target beman.big_int.tests.config_defaults_${name})
    add_executable(${test_target} config_defaults.cpp)
    target_include_directories(
        ${test_target}
        PRIVATE "${PROJECT_SOURCE_DIR}/include"
    )
    target_compile_features(${test_target} PRIVATE cxx_std_23)
    target_compile_definitions(${test_target} PRIVATE ${arg_DEFINITIONS})
    target_compile_options(${test_target} PRIVATE ${arg_OPTIONS})
    target_link_libraries(${test_target} PRIVATE GTest::gtest_main)
    beman_big_int_test_warnings(${test_target})
    gtest_discover_tests(${test_target} DISCOVERY_TIMEOUT 60)
endfunction()

beman_big_int_config_defaults_test(compiler)
beman_big_int_config_defaults_test(
    forced_ifma
    DEFINITIONS
        BEMAN_BIG_INT_X86_64_AVX512_IFMA=1
        BEMAN_BIG_INT_TEST_FORCED_IFMA
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" AND NOT MSVC)
    beman_big_int_config_defaults_test(bmi2_adx OPTIONS -mbmi2 -madx)
endif()

if(BEMAN_BIG_INT_FUZZ)
    add_subdirectory(fuzzing)
endif()
