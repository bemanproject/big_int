# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

# Glue target: builds the beman.big_int C++ module interface unit (big_int.cppm)
# on top of the beman.big_int library, and provides the helper function that
# module-aware consumers (tests, examples) use to link against it.
#
# This is kept as a separate target rather than a PUBLIC FILE_SET CXX_MODULES on
# beman.big_int itself. A PUBLIC file set would have to carry
# BEMAN_BIG_INT_BUILD_MODULE PUBLIC on beman.big_int, which would switch the
# whole GoogleTest suite, the examples, the benchmarks, and the
# VERIFY_INTERFACE_HEADER_SETS verification target onto the import path along
# with it. Keeping the module in its own target lets BEMAN_BIG_INT_BUILD_MODULE
# stay off everywhere except the code that actually opts in.
#
# The target name uses an underscore, not a third dot: beman-install-library.cmake
# splits the target name on "." and FATAL_ERRORs unless exactly 2 parts remain, so
# "beman.big_int.module" would break the install.
add_library(beman.big_int_module)
add_library(beman::big_int_module ALIAS beman.big_int_module)

target_sources(
    beman.big_int_module
    PUBLIC
        FILE_SET CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/big_int.cppm"
)

# Also carries BEMAN_BIG_INT_SIMD_MUL, which is PUBLIC on beman.big_int and
# selects the multiplication tier inside a header: the module and the library it
# wraps must agree on that choice.
target_link_libraries(beman.big_int_module PUBLIC beman::big_int)

target_compile_features(beman.big_int_module PUBLIC cxx_std_23)

# big_int.cppm defines BEMAN_BIG_INT_BUILD_MODULE itself, but consumers need the
# macro too so that including <beman/big_int/big_int.hpp> transitively (e.g. through
# another header) resolves onto the module path instead of the header path.
target_compile_definitions(
    beman.big_int_module
    PUBLIC BEMAN_BIG_INT_BUILD_MODULE
)

# The purview includes the library's angled <beman/...> headers by design.
# clang-scan-deps warns on that and ignores both the pragma above and any
# -Wno-include-angled-in-module-purview, so -w is what actually keeps the
# dependency scan clean (this glue unit's own code is fully warned on elsewhere).
target_compile_options(
    beman.big_int_module
    PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang>:-w>
)

if(BEMAN_BIG_INT_BUILD_TESTS)
    # Exports the handful of detail-namespace entities the module tests exercise
    # directly. PRIVATE definitions on a module target are still written into
    # IMPORTED_CXX_MODULES_COMPILE_DEFINITIONS on export, so an unconditional
    # definition here would follow the module into installed consumers; keeping
    # it inside this guard and PRIVATE avoids that.
    target_compile_definitions(
        beman.big_int_module
        PRIVATE BEMAN_BIG_INT_EXPORT_TESTING
    )
endif()

option(
    BEMAN_BIG_INT_USE_STD_MODULE
    "Build the beman.big_int module against the standard library module (import std) instead of textual standard headers. Default: OFF."
    OFF
)

if(BEMAN_BIG_INT_USE_STD_MODULE)
    # CMAKE_CXX_COMPILER_IMPORT_STD is only populated when
    # CMAKE_EXPERIMENTAL_CXX_IMPORT_STD was set before the project() call, which
    # the top-level CMakeLists.txt does via infra's
    # enable-experimental-import-std.cmake. A missing "23" entry therefore
    # usually means this CMake version is newer than infra knows about, or the
    # toolchain does not support import std yet, rather than a real failure.
    if("23" IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
        set_target_properties(beman.big_int_module PROPERTIES CXX_MODULE_STD ON)
        target_compile_definitions(
            beman.big_int_module
            PUBLIC BEMAN_BIG_INT_USE_STD_MODULE
        )
        message(STATUS "beman.big_int module: using 'import std;'")
    else()
        message(
            WARNING
            "BEMAN_BIG_INT_USE_STD_MODULE is ON but 'import std' (C++23) is not "
            "available from this toolchain (CMAKE_CXX_COMPILER_IMPORT_STD="
            "'${CMAKE_CXX_COMPILER_IMPORT_STD}'). Check that infra/cmake/"
            "enable-experimental-import-std.cmake knows the UUID for this CMake "
            "version (${CMAKE_VERSION}) and that the generator supports it. "
            "Falling back to textual standard headers."
        )
    endif()
endif()

# Links `target` against the beman.big_int module and configures it to scan for
# module dependencies (`import beman.big_int;`).
function(beman_big_int_add_module_consumer target)
    target_link_libraries(${target} PRIVATE beman::big_int_module)

    # Some consuming superprojects pin an older policy version (CMP0155), under
    # which sources are not scanned for module dependencies by default. Force it
    # here so `import beman.big_int;` is resolved regardless of that setting.
    set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES ON)

    get_target_property(
        _beman_big_int_module_std
        beman.big_int_module
        CXX_MODULE_STD
    )
    if(_beman_big_int_module_std)
        set_target_properties(${target} PROPERTIES CXX_MODULE_STD ON)

        # `std` is a reserved module name; Clang diagnoses `import std;` for it
        # unless told the reservation is intentional.
        target_compile_options(
            ${target}
            PRIVATE
                $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-reserved-module-identifier>
        )
    endif()
    unset(_beman_big_int_module_std)
endfunction()
