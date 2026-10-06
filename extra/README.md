<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

# Debugger visualizers

Visualizers that render `beman::big_int::basic_big_int<...>` (and the
`beman::big_int::big_int` and `beman::big_int::pmr::big_int` aliases) as their
value rather than as packed fields, regardless of whether the value is in the
in-place static buffer or on the heap.

| Debugger | Files | Summary |
| --- | --- | --- |
| Visual Studio, VS Code (`cppvsdbg`) | `big_int.natvis` | decimal to 64 bits, hexadecimal beyond |
| Visual Studio, VS Code (`cppvsdbg`) | `big_int_printer_msvc.natvis` + `big_int_printer_msvc.cpp` | full decimal, any width |
| LLDB | `big_int_printer_lldb.py` | full decimal, any width |
| GDB | `big_int_printer_gdb.py` | full decimal, any width |

The full write-up, including every way to load the Natvis file and what it
displays, is on the
[Debugger Visualizers](https://bemanproject.github.io/big_int/debugging.html)
documentation page.

## Loading the visualizers

### Visual Studio (Natvis)

`big_int.natvis` is self-contained but, being limited to the debugger's own
arithmetic, can only render decimal up to 64 bits. When `beman.big_int` is
consumed from its CMake build tree and the compiler is MSVC, it is embedded into
your binary's PDB automatically for the `Debug` and `RelWithDebInfo`
configurations, and no setup is needed. Otherwise pick one of:

- add `big_int.natvis` to your C++ project,
- pass `/NATVIS:big_int.natvis` when linking with `/DEBUG`,
- copy it into `%USERPROFILE%\Documents\Visual Studio 2022\Visualizers\`.

### Visual Studio, full decimal (Natvis + add-in DLL)

For exact decimal at any width, build `big_int_printer_msvc.cpp` into an
expression-evaluator add-in that the debugger calls to format the value, and use
`big_int_printer_msvc.natvis` **instead of** `big_int.natvis`:

```bash
cmake --preset msvc-release -DBEMAN_BIG_INT_BUILD_MSVC_DEBUGGER_ADDIN=ON
cmake --build build/msvc-release --target big_int_install_visualizers
```

That builds the DLL and copies it, with its `.natvis`, into
`%USERPROFILE%\Documents\Visual Studio 2022\Visualizers\`; override the
destination with `-DBEMAN_BIG_INT_VISUALIZERS_DIR=<path>`. Without CMake, use
`big_int_printer_msvc.bat` with a prebuilt `beman.big_int.lib`.

The DLL must be built for the same architecture and options as the program you
debug.

### LLDB

```lldb
(lldb) command script import extra/big_int_printer_lldb.py
```

To load automatically on every session, add the same line to `~/.lldbinit`
with an absolute path.

### GDB

```gdb
(gdb) source extra/big_int_printer_gdb.py
```

To load automatically on every session, add the same line to `~/.gdbinit`
with an absolute path.

## Formatting

### Natvis

Natvis has no arbitrary-precision arithmetic, so `big_int.natvis` gives exact
decimal only while the magnitude fits in 64 bits (one limb by default, two in a
32-bit-limb build) and zero-padded hexadecimal above that: all limbs up to four,
abbreviated to the most and least significant limb beyond. The add-in DLL has no
such limit -- it renders the full value, in decimal by default and in hexadecimal
under the `,hex` view.

Either way, expanding the value shows `[negative]`, `[limb_count]`,
`[limb_bits]`, `[storage]`, `[limbs]`, and `[allocator]`, with `[Raw View]` for
the packed fields.

### GDB and LLDB

The summary is always decimal with thousands separators. The packed fields
(`m_capacity`, `m_size_and_sign`, `m_storage`, `m_alloc`) remain expandable
underneath for raw inspection.

### Static (in-place) storage — `m_capacity == 0`

```
(beman::big_int::big_int) b = 12,345 {
  m_capacity = 0
  m_size_and_sign = 1
  m_storage = {
    limbs = ([0] = 12345)
  }
  m_alloc = {}
}

(beman::big_int::big_int) c = -12,345 {
  m_capacity = 0
  m_size_and_sign = 2147483649   # 0x80000001 = sign bit set, limb_count = 1
  m_storage = {
    limbs = ([0] = 12345)
  }
  m_alloc = {}
}
```

### Dynamic (heap) storage — `m_capacity > 0`

A value whose magnitude still fits in-place but whose storage was reserved to
the heap (e.g. after `reserve(100)`) — the summary is identical:

```
(beman::big_int::big_int) f = 7 {
  m_capacity = 100
  m_size_and_sign = 1
  m_storage = {
    data = 0x00000001005bd6e0
  }
  m_alloc = {}
}
```

A genuinely large value (four 64-bit limbs) — the printer follows the pointer
and concatenates the limbs little-endian:

```
(beman::big_int::big_int) g = 30,877,890,463,284,318,779,200,455,886,624,330,043,260,257,791,495,576,751,883,077,282,378,867,085,585 {
  m_capacity = 4
  m_size_and_sign = 4
  m_storage = {
    data = 0x00000001005bdb70
  }
  m_alloc = {}
}
```

### Edge cases

- Zero prints as `0` — the sign bit is never set for zero magnitude, so there
  is no `-0`.
- `INT64_MIN` (`-9,223,372,036,854,775,808`) is represented as sign bit set
  plus magnitude `0x8000000000000000`, and the printer reconstructs the signed
  decimal correctly.

## Caveats

- Only allocators whose `std::allocator_traits<A>::pointer` is a raw pointer
  are supported, in all three visualizers. Fancy-pointer allocators fall
  through to the error string in the Python printers, and to the raw field view
  under Natvis.
- The Python scripts are not auto-loaded by the build; load them manually or
  from your `.lldbinit` / `.gdbinit`.
