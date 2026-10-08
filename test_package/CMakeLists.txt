# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.30...4.3)

project(test_package LANGUAGES CXX)

find_package(beman.big_int REQUIRED CONFIG)

add_executable(test_package test_package.cpp)
target_link_libraries(test_package PRIVATE beman::big_int)
target_compile_features(test_package PRIVATE cxx_std_23)
