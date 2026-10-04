# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
#
# Pretty printer for beman::big_int::basic_big_int<min_inplace_bits, Limb, Allocator>.
#
# Layout (see include/beman/big_int/basic_big_int.hpp):
#   class basic_big_int {
#       std::uint32_t m_capacity;      // 0 = static storage, >0 = heap capacity
#       std::uint32_t m_size_and_sign; // bit 31 = sign, bits 0-30 = limb count
#       union {
#           pointer   data;                     // live when m_capacity >  0
#           limb_type limbs[inplace_capacity];  // live when m_capacity == 0
#       } m_storage;
#       [[no_unique_address]] Allocator m_alloc;
#   };
#
# The magnitude is stored little-endian across the limb array (limbs[0] is the
# least significant). The sign bit is never set when the magnitude is zero.
#
# Usage: source big_int_printer_gdb.py

import gdb
import gdb.printing
import re


class BigIntPrinter:
    """Pretty printer for beman::big_int::basic_big_int<...>"""

    def __init__(self, val):
        self.val = val

    def _decimal(self):
        capacity = int(self.val["m_capacity"]) & 0xFFFFFFFF
        size_and_sign = int(self.val["m_size_and_sign"]) & 0xFFFFFFFF
        sign_bit = (size_and_sign >> 31) & 1
        limb_count = size_and_sign & 0x7FFFFFFF

        storage = self.val["m_storage"]
        if capacity == 0:
            limbs_arr = storage["limbs"]
            limb_size_bytes = limbs_arr[0].type.sizeof

            def read(i):
                return int(limbs_arr[i])
        else:
            data_ptr = storage["data"]
            limb_size_bytes = data_ptr.type.target().sizeof

            def read(i):
                return int(data_ptr[i])

        bits_per_limb = limb_size_bytes * 8
        limb_mask = (1 << bits_per_limb) - 1

        magnitude = 0
        for i in range(limb_count):
            magnitude |= (read(i) & limb_mask) << (i * bits_per_limb)

        if sign_bit and magnitude != 0:
            magnitude = -magnitude
        return magnitude

    def to_string(self):
        try:
            return f"{self._decimal():,}"
        except Exception as e:
            return f"<invalid big_int: {e}>"

    def children(self):
        yield "m_capacity", self.val["m_capacity"]
        yield "m_size_and_sign", self.val["m_size_and_sign"]
        yield "m_storage", self.val["m_storage"]
        try:
            yield "m_alloc", self.val["m_alloc"]
        except gdb.error:
            # [[no_unique_address]] may make the allocator unreachable via
            # direct field access in some configurations; skip silently.
            pass

    def display_hint(self):
        return None


_BIG_INT_PATTERN = re.compile(
    r"^(beman::big_int::)?basic_big_int<.*>$"
)


def lookup_big_int_type(val):
    """Return a BigIntPrinter for basic_big_int instantiations, else None."""
    type_obj = val.type

    if type_obj.code == gdb.TYPE_CODE_REF:
        type_obj = type_obj.target()
    if type_obj.code == gdb.TYPE_CODE_PTR:
        return None

    type_obj = type_obj.unqualified()
    # Resolve typedefs like `beman::big_int::big_int` to the underlying
    # `basic_big_int<...>` spelling before matching.
    type_obj = type_obj.strip_typedefs()

    type_name = str(type_obj)
    if _BIG_INT_PATTERN.match(type_name):
        return BigIntPrinter(val)
    return None


def register_big_int_printers(objfile=None):
    """Register the basic_big_int pretty printer."""
    if objfile is None:
        objfile = gdb

    objfile.pretty_printers.append(lookup_big_int_type)


# Auto-register when the script is sourced
register_big_int_printers()
print("beman::big_int::basic_big_int pretty printer loaded successfully")
