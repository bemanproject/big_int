# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# detail/multiply_long_runtime.hpp, detail/square_long_runtime.hpp and
# detail/simd/vec_*.hpp are deliberately absent: they are included only by
# src/*.cpp, which are compiled into the static library, so they are not part of
# the installed interface. The SIMD headers also cannot satisfy
# VERIFY_INTERFACE_HEADER_SETS, since each one is valid only on its own
# architecture.

target_sources(
    beman.big_int
    PUBLIC
        FILE_SET HEADERS
            FILES
                basic_big_int.hpp
                copy_to_runtime.hpp
                charconv.hpp
                string.hpp
                limits.hpp
                literals.hpp
                format.hpp
                numeric.hpp
                detail/base_conversion.hpp
                detail/base_tables.hpp
                detail/config.hpp
                detail/div_impl.hpp
                detail/floats.hpp
                detail/gcd_impl.hpp
                detail/mod_arith.hpp
                detail/mul_impl.hpp
                detail/ntt.hpp
                detail/ntt_fp.hpp
                detail/scratch_allocator.hpp
                detail/siphash.hpp
                detail/span_ops.hpp
                detail/wide_ops.hpp
)
