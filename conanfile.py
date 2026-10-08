# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

import os
import re

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load, rmdir
from conan.tools.scm import Version

required_conan_version = ">=2.0"


class BemanBigIntConan(ConanFile):
    name = "beman-big-int"
    description = "Reference implementation of std::big_int, the arbitrary-precision integer proposed for C++29"
    license = ("Apache-2.0 WITH LLVM-exception", "BSL-1.0")
    url = "https://github.com/bemanproject/big_int"
    homepage = "https://github.com/bemanproject/big_int"
    topics = ("beman", "big-int", "bignum", "arbitrary-precision", "multiprecision")
    # The multiplication and division kernels are compiled, and the library
    # exports no DLL interface, so it is only ever a static library.
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "fPIC": [True, False],
        "simd_mul": [True, False],
    }
    default_options = {
        "fPIC": True,
        "simd_mul": False,
    }
    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "LICENSE-BOOST",
        "include/*",
        "src/*",
        "infra/cmake/*",
        "extra/big_int.natvis",
    )

    @property
    def _min_compiler_versions(self):
        # The tested platforms in docs/modules/ROOT/pages/build_and_usage.adoc.
        return {
            "gcc": "14",
            "clang": "19",
            "apple-clang": "17",
            "msvc": "194",
        }

    def set_version(self):
        # The single source of truth is project(VERSION) in CMakeLists.txt.
        cmakelists = load(self, os.path.join(self.recipe_folder, "CMakeLists.txt"))
        self.version = self.version or re.search(r"project\([^)]*VERSION\s+([0-9.]+)", cmakelists).group(1)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self, src_folder=".")

    def validate(self):
        check_min_cppstd(self, 23)
        minimum = self._min_compiler_versions.get(str(self.settings.compiler))
        if minimum and Version(self.settings.compiler.version) < minimum:
            raise ConanInvalidConfiguration(
                f"{self.ref} requires {self.settings.compiler} {minimum} or later"
            )

    def generate(self):
        tc = CMakeToolchain(self)
        tc.cache_variables["BEMAN_BIG_INT_BUILD_TESTS"] = False
        tc.cache_variables["BEMAN_BIG_INT_BUILD_EXAMPLES"] = False
        tc.cache_variables["BEMAN_BIG_INT_BUILD_BENCHMARKS"] = False
        tc.cache_variables["BEMAN_BIG_INT_SIMD_MUL"] = bool(self.options.simd_mul)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE*", self.source_folder, os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        # CMakeDeps generates the config files from package_info() instead.
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "beman.big_int")
        self.cpp_info.set_property("cmake_target_name", "beman::big_int")
        self.cpp_info.libs = ["beman.big_int"]
