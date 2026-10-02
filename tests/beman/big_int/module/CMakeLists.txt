# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

# Tests that consume beman.big_int through `import beman.big_int;`.
#
# These cannot live in the parent directory: it globs *.test.cpp and builds every
# match against the headers. A translation unit here carries
# BEMAN_BIG_INT_BUILD_MODULE, which the module target propagates PUBLIC, and under
# that macro BEMAN_BIG_INT_EXPORT expands to `export`. Including a public header in
# such a translation unit is therefore ill-formed, so everything here imports.
#
# Nothing here links Boost::multiprecision. It is textual and cannot be mixed with
# `import std` in one translation unit, and the differential arithmetic it exists
# for is already covered by the header tests in the parent directory.

include(GoogleTest)

# The sources here name the library's namespace through BEMAN_BIG_INT_NAMESPACE, which
# cannot cross the module boundary from detail/config.hpp, so it is passed on the
# command line. A non-default namespace already arrives PUBLIC from beman.big_int.
if(BEMAN_BIG_INT_NAMESPACE STREQUAL "beman::big_int")
    add_compile_definitions(BEMAN_BIG_INT_NAMESPACE=beman::big_int)
endif()

# The project's test warning set, so a diagnostic only the module path produces is
# still an error.
function(beman_big_int_module_test_warnings target)
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
    )
        target_compile_options(
            ${target}
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
    if(MSVC)
        target_compile_options(${target} PRIVATE /WX /W4)
    endif()
endfunction()

# The GoogleTest-free smoke test. It is the only module test built in the
# `import std` configuration, because GoogleTest's headers are textual and
# libstdc++ and the MSVC standard library cannot mix those with `import std` in one
# translation unit.
add_executable(beman.big_int.tests.module.quick)
target_sources(beman.big_int.tests.module.quick PRIVATE quick.test.cpp)
beman_big_int_add_module_consumer(beman.big_int.tests.module.quick)
beman_big_int_module_test_warnings(beman.big_int.tests.module.quick)
add_test(
    NAME beman.big_int.tests.module.quick
    COMMAND beman.big_int.tests.module.quick
)

if(BEMAN_BIG_INT_USE_STD_MODULE)
    message(
        STATUS
        "beman.big_int module tests: 'import std' build, GoogleTest consumers skipped"
    )
    return()
endif()

# Listed rather than globbed: the quick test above is handled separately, and the
# ABI test needs a companion translation unit that must NOT be compiled in module
# mode, so a glob would do the wrong thing for two of the sources.
set(BEMAN_BIG_INT_MODULE_TESTS
    exports
    arithmetic
    literals
    charconv_string
    numeric
    std_integration
    pmr
)

foreach(test_name IN LISTS BEMAN_BIG_INT_MODULE_TESTS)
    set(test_target beman.big_int.tests.module.${test_name})

    add_executable(${test_target})
    target_sources(${test_target} PRIVATE ${test_name}.test.cpp)
    beman_big_int_add_module_consumer(${test_target})
    target_link_libraries(${test_target} PRIVATE GTest::gtest_main)
    beman_big_int_module_test_warnings(${test_target})
    gtest_discover_tests(${test_target} DISCOVERY_TIMEOUT 60)
endforeach()

# The ABI test. `extern "C++"` in the interface unit attaches the library's
# declarations to the global module, so a value built by a translation unit that
# included the headers and one built by a translation unit that imported the module
# are the same type with the same mangled names. The proof is the link: the two
# translation units declare the same functions, each naming the type through its own
# route, and share no header.
#
# The header side is an object library of its own so that it does not inherit
# BEMAN_BIG_INT_BUILD_MODULE, under which its includes would be ill-formed.
add_library(beman.big_int.tests.module.abi_header_tu OBJECT)
target_sources(
    beman.big_int.tests.module.abi_header_tu
    PRIVATE abi_compat_header_tu.cpp
)
target_link_libraries(
    beman.big_int.tests.module.abi_header_tu
    PRIVATE beman::big_int
)
set_target_properties(
    beman.big_int.tests.module.abi_header_tu
    PROPERTIES CXX_SCAN_FOR_MODULES OFF
)
beman_big_int_module_test_warnings(beman.big_int.tests.module.abi_header_tu)

add_executable(beman.big_int.tests.module.abi_compat)
target_sources(
    beman.big_int.tests.module.abi_compat
    PRIVATE
        abi_compat.test.cpp
        $<TARGET_OBJECTS:beman.big_int.tests.module.abi_header_tu>
)
beman_big_int_add_module_consumer(beman.big_int.tests.module.abi_compat)
target_link_libraries(
    beman.big_int.tests.module.abi_compat
    PRIVATE GTest::gtest_main
)
beman_big_int_module_test_warnings(beman.big_int.tests.module.abi_compat)
gtest_discover_tests(beman.big_int.tests.module.abi_compat DISCOVERY_TIMEOUT 60)
