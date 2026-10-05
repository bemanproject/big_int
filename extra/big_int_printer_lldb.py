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
# Usage (inside LLDB):
#   command script import extra/big_int_printer_lldb.py

import lldb


def _compute_decimal(valobj):
    val = valobj.GetNonSyntheticValue()

    capacity = val.GetChildMemberWithName("m_capacity").GetValueAsUnsigned() & 0xFFFFFFFF
    size_and_sign = val.GetChildMemberWithName("m_size_and_sign").GetValueAsUnsigned() & 0xFFFFFFFF
    sign_bit = (size_and_sign >> 31) & 1
    limb_count = size_and_sign & 0x7FFFFFFF

    storage = val.GetChildMemberWithName("m_storage")

    if capacity == 0:
        limbs = storage.GetChildMemberWithName("limbs")
        inplace_capacity = limbs.GetNumChildren()
        if limb_count > inplace_capacity:
            raise ValueError(
                f"limb_count {limb_count} exceeds inplace capacity {inplace_capacity}"
            )
        first = limbs.GetChildAtIndex(0)
        limb_size_bytes = first.GetByteSize()

        def read(i):
            return limbs.GetChildAtIndex(i).GetValueAsUnsigned()
    else:
        data_ptr = storage.GetChildMemberWithName("data")
        pointee_type = data_ptr.GetType().GetPointeeType()
        limb_size_bytes = pointee_type.GetByteSize()

        base_addr = data_ptr.GetValueAsUnsigned()
        if base_addr == 0:
            raise ValueError("null pointer in dynamic storage")

        process = val.GetProcess()

        def read(i):
            error = lldb.SBError()
            raw = process.ReadMemory(base_addr + i * limb_size_bytes, limb_size_bytes, error)
            if not error.Success():
                raise ValueError(f"failed to read limb {i}: {error.GetCString()}")
            return int.from_bytes(raw, byteorder="little", signed=False)

    bits_per_limb = limb_size_bytes * 8
    limb_mask = (1 << bits_per_limb) - 1

    magnitude = 0
    for i in range(limb_count):
        magnitude |= (read(i) & limb_mask) << (i * bits_per_limb)

    if sign_bit and magnitude != 0:
        magnitude = -magnitude
    return magnitude


def big_int_summary(valobj, internal_dict):
    """Custom summary for beman::big_int::basic_big_int<...>. Displays as base-10."""
    try:
        return f"{_compute_decimal(valobj):,}"
    except Exception as e:
        return f"<invalid big_int: {e}>"


class BigIntSyntheticProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self._field_names = ["m_capacity", "m_size_and_sign", "m_storage", "m_alloc"]

    def update(self):
        # Filter out fields that aren't present (e.g. allocator may be elided
        # by [[no_unique_address]] in some configurations).
        present = []
        for name in ["m_capacity", "m_size_and_sign", "m_storage", "m_alloc"]:
            child = self.valobj.GetNonSyntheticValue().GetChildMemberWithName(name)
            if child.IsValid():
                present.append(name)
        self._field_names = present

    def num_children(self):
        return len(self._field_names)

    def get_child_index(self, name):
        try:
            return self._field_names.index(name)
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= len(self._field_names):
            return None
        return self.valobj.GetNonSyntheticValue().GetChildMemberWithName(
            self._field_names[index]
        )

    def has_children(self):
        return bool(self._field_names)


def __lldb_init_module(debugger, internal_dict):
    # Match both the typedef (`beman::big_int::big_int`) and any
    # `basic_big_int<...>` instantiation. LLDB's regex matcher sees the type
    # name as displayed, which may be the typedef spelling.
    pattern = (
        r"^(const )?"
        r"(beman::big_int::big_int|(\w+::)*basic_big_int<.*>)"
        r"( &| \*)?$"
    )

    debugger.HandleCommand(
        f'type summary add -x "{pattern}" -e -F big_int_printer_lldb.big_int_summary'
    )
    debugger.HandleCommand(
        f'type synthetic add -x "{pattern}" -l big_int_printer_lldb.BigIntSyntheticProvider'
    )

    print("beman::big_int::basic_big_int pretty printer loaded successfully")
