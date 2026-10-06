<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

# Item 1 results: cutting the fixed per-call front-end cost

Item 1 of [`gmp_gap_analysis.md`](gmp_gap_analysis.md) section 6 targeted the 13-18 ns that `+ - << >>` add on top of the
span kernel at <= 16 limbs, the ~21 ns for small `*`, and the 37-66 ns for small divrem. This document records what changed,
how it was measured, and where the targets were and were not met. Numbers are nanoseconds per operation unless noted;
"before" is the library at cf1cf54 and "after" is `opt_1` at b823c5f, both measured with the same harness (`shape_sweep.cpp`,
with the `floor` row added). The inline-capacity decision is left to the reader: the study in section 7 reports costs and
benefits without recommending a default.

## 1. Summary of the changes

- **Assignment.** `operator=(&&)` (`assign_value`) steals an rvalue's heap buffer whenever the destination's allocator can
  free it, whatever capacity the destination already holds, instead of copying the limbs and freeing the source. A
  propagating allocator that compares unequal no longer leaves the destination holding a buffer it cannot free. So
  `c = a op b` costs one allocation (the result) plus the kernel, with no copy.
- **No-zero-fill storage.** New `storage_for_overwrite(n)` ensures capacity and discards the contents: no copy, no
  value-initialisation at run time (a debug build poison-fills new blocks, a constant evaluation value-initialises). The
  multiply and divide dispatchers no longer require a pre-zeroed result; the zero-fill moved into the tiers that need it, so
  the basecase no longer pays two fills. `clear_inline_tail` keeps the "inline limbs above the count are zero" invariant.
- **add and sub.** `add_in_place` and `add_into` use the tail-aware `add_n_tail`/`sub_n_tail` (a plain loop under GCC, 4-way unrolled under clang and MSVC; both stop once the
  carry or borrow dies), grow only for the carry limb, set sign and size once, and write the result with direct stores into
  `storage_for_overwrite` storage: no zero re-scan, no one-step trim. Single-limb operands take a fast path.
- **Shifts.** `c = a << s` and `c = a >> s` build the result from the source in one pass (`lshift_copy`/`rshift_copy`) into an
  exactly sized buffer; `<<=` and `>>=` shift in one in-place pass without reserving a spare limb the value does not need
  (so `1 << 127` stays inline in a 128-bit `basic_big_int`). A later change that routed in-place pure bit shifts to the older
  `shift_left_n`/`shift_right_n` was reverted after it slowed M4 (sections 3.6 and 3.8); under GCC the shift primitives use
  register-carried loops. Negative right shifts keep floor rounding by detecting
  discarded bits during the pass.
- **Multiply.** `multiply_into` writes into `storage_for_overwrite` storage and drops the fill and the zero re-scan. `*=`
  keeps the allocator and capacity (one-limb rhs in place, small products through a stack buffer, larger ones into a fresh
  buffer so `x *= x` works). A header-side basecase shortcut (`multiply_basecase_runtime`, `mul_header_basecase_enabled`)
  skips the scratch hooks and the tier ladder when `min(la, lb) < 32` (section 6).
- **Divide.** `divmod_into` uses `storage_for_overwrite` and a 128-limb stack arena for small-shape scratch, builds the
  remainder from its trimmed span instead of over-reserving, and a 2x1 quotient that fits inline no longer allocates. `/=`
  and `%=` keep the object's allocator and capacity (`const basic_big_int temp(std::move(*this), m_alloc); set_zero();`).
- **Bug fixes (found while making the above safe).** Stale inline-tail limbs when the inline capacity is above one limb
  (`set_zero`, `shift_right`, `divmod_in_place_short`, the move-assign steal path) made `inplace_to_bit_uint` and so `==`
  against builtins wrong for `basic_big_int<128>`; base 8 and 32 `from_chars` into a reused heap destination OR-ed digits
  into stale limbs (this affected `big_int` itself); the move-assign fast path propagated a POCCA/POCMA allocator but kept
  the heap buffer it could no longer free. All have regression tests (`inline_tail`, `allocation`, `front_end_*`,
  `muldiv_frontend`, `dispatch_contract`).

## 2. Method

- **Harness.** `shape_sweep` (see `perf/tools/README.md`). Rows: `auto` is `c = a op b` into a reused destination, `inplace`
  is `c += b; c -= b` (shifts: `<<=` then `>>=`) halved, `kernel` is the span kernel, `floor` is the kernel plus one
  `std::allocator` allocate/deallocate pair of the result size (two for divrem: quotient and remainder), and `gmpz` is GMP
  `mpz` with the destination reused. The floor is the true API floor of `c = a op b`: no pair is charged when the result
  fits the inline capacity of the integer type under test (at 64 bits that affects shr 1x13 and the 1-limb remainder of
  divrem 2x1). `auto - floor` is what the front end costs on top of the unavoidable allocation. The shift `kernel` row
  includes the copy-in of the source, so for shifts `auto - floor` can go negative once `auto` is a single pass.
- **Machines.** x64: i9-11900K, Ubuntu 24.04, g++-14.3 `-O2 -march=native`, SIMD_MUL, IFMA multiply kernels
  (`BMI2_ADX=1 AVX512_IFMA=1`), every sweep pinned with `taskset -c 2`, box idle. M4: Apple M4 Max, appleclang 21,
  SIMD_MUL, nothing pinned, machine idle and not building during timing. GMP 6.3.0 on both.
- **Runs.** Before and after binaries were interleaved: seeds 1 and 2, two repeats each, so four runs per binary and machine
  (`gap_sweep.sh --ops add,sub,shl,shr,mul,sqr,divrem,fromchars --bands small,medium`). Reported values are the median of the
  four runs; divrem shapes of up to 32 limbs are the mean over the two seeds (the operands change the Knuth D path, see
  caveats). Large-band spot checks are 3-round runs per seed. Raw CSVs are in `perf/item1/`
  (`x64_*`, `mac_*`; `x64_gcc14_base*` and `x64_gcc14_base{64,128,256}_vecsort_*` are the earlier cf1cf54 baseline runs).
- **Binaries.** Before: harness at cf1cf54 (`base64`). After: `opt_1` (`new64`). The inline-capacity study uses `new64`,
  `new128`, `new256`, `new512` (`-DBEMAN_BIG_INT_SWEEP_INLINE_BITS`). The `#const` line of every binary was checked
  (`SIMD_MUL=1`, `ndebug=1`, `sweep_inline_bits`, `sweep_int_sizeof`, x64 `BMI2_ADX=1 AVX512_IFMA=1`, `gcc-14.3.0`), and
  `selftest.sh` passed on all of them.

## 3. Before and after

Cells read `before -> after`. `auto/gmpz` is the user-visible gap to GMP (above 1 means `big_int` is slower). The tables are
the re-measurement on `opt_1` (the add/sub, shift and single-limb follow-ups and the mul-add fix included): all rows come
from the d689f75 runs (`macr3_*`, `x64r3_*`) except the shl/shr rows, which were re-measured at b823c5f (`macr4_*`,
`x64r4_*`), the commit that changes only the shift code; one interleaved check of add/sub/mul/sqr/divrem at b823c5f found
nothing else moved. Rows are the median of four runs per binary. An earlier measurement of 9145b4e is described in
section 3.6.

### 3.1 x64 (g++-14, -march=native), small band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 1x1 | 8.2 -> 5.4 | 4.1 -> 2.6 | -1.6 -> -4.4 | 1.98 -> 1.29 | 6.3 -> 3.4 |
| add | 2x2 | 17.9 -> 15.9 | 5.9 -> 6.2 | 7.6 -> 5.7 | 4.29 -> 3.82 | 15.5 -> 13.5 |
| add | 4x4 | 18.0 -> 15.4 | 5.6 -> 5.7 | 7.1 -> 4.5 | 4.12 -> 3.53 | 15.0 -> 12.4 |
| add | 8x8 | 19.3 -> 16.9 | 7.3 -> 7.3 | 6.8 -> 4.4 | 4.12 -> 3.62 | 14.7 -> 12.4 |
| add | 16x16 | 25.3 -> 21.1 | 11.8 -> 11.2 | 9.8 -> 5.6 | 3.87 -> 3.23 | 17.7 -> 13.5 |
| sub | 1x1 | 9.0 -> 5.5 | 4.0 -> 2.5 | 7.1 -> 3.4 | 2.08 -> 1.26 | 7.5 -> 4.0 |
| sub | 2x2 | 19.1 -> 15.7 | 4.6 -> 4.8 | 9.1 -> 5.8 | 4.39 -> 3.61 | 17.0 -> 13.5 |
| sub | 4x4 | 19.0 -> 16.5 | 5.5 -> 5.6 | 8.3 -> 5.9 | 4.12 -> 3.60 | 16.2 -> 13.7 |
| sub | 8x8 | 20.6 -> 18.1 | 7.2 -> 7.3 | 8.2 -> 5.9 | 4.14 -> 3.64 | 16.2 -> 13.7 |
| sub | 16x16 | 25.7 -> 21.0 | 10.6 -> 10.0 | 10.3 -> 5.7 | 3.78 -> 3.09 | 17.7 -> 13.6 |
| shl | 1x13 | 16.9 -> 12.7 | 4.2 -> 3.5 | 7.0 -> 2.9 | 4.86 -> 3.88 | 14.8 -> 10.6 |
| shl | 4x13 | 18.7 -> 13.4 | 5.3 -> 4.4 | 7.5 -> 2.2 | 4.63 -> 3.32 | 15.2 -> 9.9 |
| shl | 16x13 | 24.6 -> 16.8 | 9.3 -> 8.6 | 9.3 -> 1.5 | 4.22 -> 2.88 | 17.1 -> 9.3 |
| shr | 1x13 | 10.3 -> 8.4 | 3.6 -> 3.1 | 8.0 -> 6.1 | 3.14 -> 2.56 | 8.6 -> 6.7 |
| shr | 4x13 | 21.0 -> 15.9 | 4.8 -> 4.2 | 9.0 -> 4.7 | 5.28 -> 4.01 | 17.5 -> 12.4 |
| shr | 16x13 | 25.9 -> 20.2 | 9.0 -> 8.2 | 8.9 -> 4.7 | 3.96 -> 3.26 | 17.7 -> 11.7 |
| mul | 2x2 | 28.2 -> 18.9 | - | 13.1 -> 3.9 | 4.61 -> 3.08 | 20.7 -> 11.3 |
| mul | 3x3 | 29.7 -> 20.3 | - | 13.1 -> 3.9 | 2.97 -> 2.03 | 20.7 -> 11.4 |
| mul | 4x4 | 32.0 -> 22.4 | - | 13.4 -> 4.2 | 2.63 -> 1.84 | 20.9 -> 11.5 |
| mul | 6x6 | 42.1 -> 28.0 | - | 17.4 -> 3.5 | 2.31 -> 1.54 | 24.6 -> 10.8 |
| mul | 8x8 | 47.5 -> 36.0 | - | 14.9 -> 3.4 | 1.65 -> 1.24 | 22.0 -> 10.5 |
| mul | 12x12 | 70.2 -> 57.4 | - | 15.3 -> 2.5 | 1.38 -> 1.12 | 22.4 -> 9.5 |
| mul | 16x16 | 101.8 -> 88.6 | - | 15.4 -> 1.8 | 1.21 -> 1.05 | 22.5 -> 9.4 |
| sqr | 4x4 | 33.3 -> 22.2 | - | 16.4 -> 5.5 | 3.77 -> 2.51 | 24.2 -> 12.3 |
| sqr | 16x16 | 80.1 -> 67.3 | - | 17.5 -> 4.2 | 1.52 -> 1.29 | 23.9 -> 9.5 |
| divrem | 2x1 | 41.7 -> 36.2 | - | 28.9 -> 23.4 | 3.34 -> 2.92 | 36.2 -> 30.5 |
| divrem | 8x4 | 112.7 -> 94.1 | - | 52.9 -> 26.3 | 2.57 -> 2.13 | 67.0 -> 43.8 |
| divrem | 32x16 | 370.9 -> 342.2 | - | 67.4 -> 36.1 | 1.91 -> 1.77 | 82.4 -> 53.9 |
| fromchars | 4x10 | 194.1 -> 115.6 | - | - | 2.85 -> 1.70 | 155.9 -> 77.6 |
| fromchars | 16x10 | 421.8 -> 425.2 | - | - | 1.63 -> 1.66 | 204.4 -> 225.2 |
| tochars | 4x10 | 134.1 -> 132.0 | - | - | 1.38 -> 1.35 | -46.9 -> -48.9 |
| tochars | 16x10 | 1171.4 -> 1168.1 | - | - | 1.99 -> 1.98 | 132.4 -> 135.3 |

### 3.2 M4 (appleclang), small band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 1x1 | 6.9 -> 5.9 | 3.2 -> 1.9 | -2.5 -> -3.7 | 2.26 -> 1.97 | 5.8 -> 4.7 |
| add | 2x2 | 14.8 -> 15.9 | 3.8 -> 4.1 | 4.9 -> 6.3 | 2.52 -> 4.90 | 13.4 -> 14.5 |
| add | 4x4 | 15.4 -> 14.6 | 3.7 -> 3.8 | 4.5 -> 4.2 | 3.19 -> 3.57 | 13.4 -> 12.6 |
| add | 8x8 | 15.8 -> 15.6 | 5.1 -> 4.3 | 3.9 -> 3.6 | 3.02 -> 3.56 | 12.3 -> 12.1 |
| add | 16x16 | 16.4 -> 17.7 | 9.8 -> 5.7 | 2.2 -> 3.7 | 3.62 -> 3.95 | 8.8 -> 10.1 |
| sub | 1x1 | 7.6 -> 5.7 | 3.4 -> 2.0 | 5.8 -> 4.0 | 2.76 -> 2.30 | 6.5 -> 4.7 |
| sub | 2x2 | 15.0 -> 14.7 | 3.3 -> 3.8 | 5.5 -> 5.4 | 4.64 -> 5.14 | 13.6 -> 13.3 |
| sub | 4x4 | 15.8 -> 15.1 | 3.9 -> 4.0 | 5.8 -> 5.2 | 5.31 -> 5.52 | 13.9 -> 13.2 |
| sub | 8x8 | 16.3 -> 15.7 | 5.0 -> 4.4 | 4.6 -> 4.3 | 4.65 -> 4.86 | 12.7 -> 12.2 |
| sub | 16x16 | 18.4 -> 18.1 | 10.9 -> 5.4 | 3.0 -> 3.0 | 4.22 -> 4.13 | 10.7 -> 10.3 |
| shl | 1x13 | 17.5 -> 12.3 | 2.3 -> 2.5 | 6.5 -> 0.9 | 8.82 -> 6.14 | 15.3 -> 10.0 |
| shl | 4x13 | 19.0 -> 13.1 | 3.6 -> 3.8 | 5.9 -> -0.1 | 6.98 -> 4.73 | 15.3 -> 9.4 |
| shl | 16x13 | 21.5 -> 16.1 | 5.8 -> 5.4 | 9.4 -> 3.9 | 4.26 -> 3.09 | 17.4 -> 12.0 |
| shr | 1x13 | 9.5 -> 6.6 | 2.4 -> 3.3 | 7.0 -> 4.1 | 4.77 -> 3.31 | 7.3 -> 4.3 |
| shr | 4x13 | 17.8 -> 15.2 | 3.3 -> 3.6 | 5.4 -> 2.9 | 6.41 -> 5.56 | 14.0 -> 11.5 |
| shr | 16x13 | 21.6 -> 18.5 | 5.1 -> 5.2 | 8.3 -> 5.2 | 5.07 -> 4.35 | 17.4 -> 14.4 |
| mul | 2x2 | 28.1 -> 18.2 | - | 13.4 -> 3.7 | 4.29 -> 2.81 | 21.5 -> 11.9 |
| mul | 3x3 | 30.1 -> 20.0 | - | 13.2 -> 3.0 | 3.57 -> 1.91 | 21.8 -> 11.9 |
| mul | 4x4 | 29.7 -> 21.6 | - | 12.6 -> 4.8 | 2.87 -> 1.97 | 18.5 -> 10.6 |
| mul | 6x6 | 33.0 -> 27.2 | - | 9.2 -> 2.9 | 1.76 -> 1.46 | 13.9 -> 7.9 |
| mul | 8x8 | 41.9 -> 35.1 | - | 11.0 -> 4.7 | 1.56 -> 1.41 | 18.7 -> 12.1 |
| mul | 12x12 | 61.8 -> 53.8 | - | 12.0 -> 4.3 | 1.33 -> 1.15 | 20.3 -> 12.4 |
| mul | 16x16 | 94.8 -> 82.4 | - | 15.7 -> 4.0 | 1.20 -> 1.05 | 24.9 -> 12.8 |
| sqr | 4x4 | 29.6 -> 21.6 | - | 13.2 -> 5.3 | 3.14 -> 2.22 | 18.2 -> 10.3 |
| sqr | 16x16 | 88.9 -> 75.8 | - | 18.3 -> 4.0 | 1.35 -> 1.15 | 26.7 -> 13.9 |
| divrem | 2x1 | 31.1 -> 25.2 | - | 17.3 -> 11.5 | 3.53 -> 2.89 | 25.1 -> 19.2 |
| divrem | 8x4 | 96.0 -> 75.4 | - | 34.8 -> 14.6 | 2.20 -> 1.72 | 49.8 -> 28.8 |
| divrem | 32x16 | 481.3 -> 432.0 | - | 63.6 -> 21.3 | 2.28 -> 2.05 | 79.1 -> 39.6 |
| fromchars | 4x10 | 91.2 -> 91.9 | - | - | 1.19 -> 1.20 | 58.6 -> 58.9 |
| fromchars | 16x10 | 399.5 -> 394.3 | - | - | 1.26 -> 1.25 | 206.1 -> 201.8 |
| tochars | 4x10 | 84.0 -> 84.4 | - | - | 0.86 -> 0.88 | -19.0 -> -17.8 |
| tochars | 16x10 | 828.8 -> 841.7 | - | - | 1.37 -> 1.40 | 88.0 -> 103.1 |

### 3.3 x64, medium band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 64x64 | 63.7 -> 46.2 | 38.9 -> 35.8 | 22.4 -> 5.1 | 3.86 -> 2.80 | 30.4 -> 13.4 |
| add | 256x256 | 223.9 -> 162.9 | 145.8 -> 144.7 | 65.1 -> 4.4 | 2.82 -> 2.05 | 81.3 -> 20.6 |
| add | 1024x1024 | 841.9 -> 599.1 | 583.0 -> 581.0 | 247.6 -> 5.1 | 2.15 -> 1.53 | 263.4 -> 21.3 |
| add | 2000x2000 | 1721.6 -> 1153.3 | 1134.8 -> 1132.4 | 571.1 -> 3.9 | 3.56 -> 2.40 | 588.6 -> 21.7 |
| sub | 64x64 | 65.9 -> 45.3 | 37.7 -> 34.6 | 23.8 -> 3.1 | 3.94 -> 2.70 | 33.0 -> 12.6 |
| sub | 1024x1024 | 842.3 -> 598.3 | 581.5 -> 579.9 | 247.9 -> 4.1 | 2.15 -> 1.53 | 264.0 -> 20.5 |
| shl | 256x13 | 136.9 -> 121.8 | 93.1 -> 98.7 | 19.7 -> 4.0 | 2.69 -> 2.43 | 41.7 -> 26.6 |
| shl | 2000x13 | 916.7 -> 773.4 | 657.7 -> 712.5 | 177.8 -> 34.0 | 2.69 -> 2.28 | 197.2 -> 54.0 |
| shl | 2000x77 | 1066.1 -> 774.4 | 764.8 -> 712.6 | 194.2 -> -98.4 | 3.09 -> 2.25 | 216.0 -> -75.5 |
| shr | 256x13 | 139.7 -> 133.7 | 93.7 -> 98.3 | 7.5 -> 2.9 | 2.71 -> 2.61 | 28.1 -> 23.1 |
| shr | 2000x13 | 950.7 -> 787.9 | 657.2 -> 711.5 | 118.1 -> -44.1 | 2.78 -> 2.31 | 138.9 -> -24.1 |
| shr | 2000x77 | 1084.3 -> 789.8 | 765.0 -> 712.1 | 233.6 -> -60.9 | 3.16 -> 2.31 | 255.9 -> -39.5 |
| mul | 32x32 | 212.3 -> 205.8 | - | 12.1 -> 9.3 | 0.78 -> 0.76 | 24.7 -> 18.9 |
| mul | 64x64 | 524.5 -> 496.2 | - | 31.0 -> 3.7 | 0.60 -> 0.58 | 41.5 -> 9.5 |
| mul | 256x256 | 6231.5 -> 6176.4 | - | 28.3 -> 2.4 | 0.80 -> 0.79 | 62.6 -> -2.4 |
| mul | 1000x1000 | 59904.3 -> 59861.8 | - | 343.6 -> 312.4 | 1.12 -> 1.12 | 389.1 -> 230.9 |
| mul | 2000x2000 | 177072.6 -> 176923.7 | - | 722.9 -> 199.2 | 1.23 -> 1.23 | 692.2 -> -137.4 |
| mul | 2000x100 | 17852.9 -> 17643.6 | - | 154.6 -> -43.6 | 0.50 -> 0.49 | 193.6 -> -52.9 |
| sqr | 64x64 | 301.5 -> 284.4 | - | 20.2 -> 3.8 | 0.54 -> 0.51 | 30.8 -> 9.1 |
| sqr | 512x512 | 10627.6 -> 10594.1 | - | 56.8 -> -15.5 | 0.70 -> 0.70 | 64.8 -> 27.0 |
| sqr | 2000x2000 | 99937.6 -> 99323.2 | - | 651.3 -> 1067.7 | 0.99 -> 0.99 | 646.3 -> 794.2 |
| divrem | 128x64 | 3342.1 -> 3446.3 | - | 67.0 -> 82.1 | 1.94 -> 2.00 | 141.1 -> 103.2 |
| divrem | 512x256 | 29558.6 -> 29817.4 | - | 292.6 -> 178.0 | 1.81 -> 1.83 | 164.7 -> 226.5 |
| divrem | 2000x1000 | 185318.8 -> 186774.1 | - | 1166.1 -> 819.0 | 1.34 -> 1.35 | 1065.1 -> 1041.0 |
| divrem | 4000x2000 | 478481.8 -> 477627.1 | - | 3873.1 -> -100.7 | 1.43 -> 1.43 | 3326.4 -> -564.6 |
| divrem | 2000x100 | 144654.0 -> 144971.4 | - | 1928.4 -> 1154.1 | 2.00 -> 2.00 | 812.6 -> 886.6 |
| divrem | 65536x1 | 193721.3 -> 168835.7 | - | 18973.6 -> 1400.2 | 1.31 -> 1.14 | 26598.0 -> 1889.2 |
| fromchars | 64x10 | 2790.5 -> 2852.3 | - | - | 2.04 -> 2.08 | 667.6 -> 1045.0 |
| fromchars | 256x10 | 28563.9 -> 27920.0 | - | - | 2.87 -> 2.80 | 2874.6 -> 3489.4 |
| fromchars | 2000x10 | 429183.1 -> 385669.1 | - | - | 2.17 -> 1.95 | 20511.4 -> 23105.6 |
| tochars | 64x10 | 5622.5 -> 5695.4 | - | - | 1.90 -> 1.93 | 377.4 -> 362.4 |
| tochars | 256x10 | 35873.4 -> 36990.2 | - | - | 1.82 -> 1.88 | 1283.7 -> 1461.4 |
| tochars | 2000x10 | 608668.2 -> 620642.7 | - | - | 1.49 -> 1.52 | 24355.7 -> 23098.2 |

### 3.4 M4, medium band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 64x64 | 54.7 -> 37.9 | 51.6 -> 20.0 | -6.4 -> -23.0 | 4.21 -> 2.84 | 5.8 -> -11.2 |
| add | 256x256 | 148.8 -> 109.8 | 240.0 -> 101.2 | -99.1 -> -136.9 | 2.57 -> 1.89 | -85.2 -> -126.7 |
| add | 1024x1024 | 564.9 -> 450.3 | 989.7 -> 437.1 | -423.8 -> -537.8 | 2.23 -> 1.78 | -418.7 -> -527.5 |
| add | 2000x2000 | 1132.3 -> 877.0 | 1948.4 -> 858.2 | -812.1 -> -1054.3 | 2.27 -> 1.76 | -797.7 -> -1050.8 |
| sub | 64x64 | 41.2 -> 36.1 | 51.2 -> 19.8 | -19.0 -> -25.1 | 2.97 -> 2.68 | -8.6 -> -17.3 |
| sub | 1024x1024 | 562.3 -> 454.6 | 988.4 -> 435.0 | -424.5 -> -532.1 | 2.21 -> 1.80 | -416.2 -> -524.8 |
| shl | 256x13 | 87.5 -> 41.5 | 36.5 -> 29.8 | 29.7 -> -16.6 | 2.30 -> 1.08 | 38.0 -> -8.2 |
| shl | 2000x13 | 653.6 -> 260.4 | 283.7 -> 195.9 | 252.2 -> -141.8 | 0.30 -> 0.15 | 267.7 -> -124.8 |
| shl | 2000x77 | 796.3 -> 452.3 | 425.8 -> 228.4 | 253.6 -> -92.1 | 2.74 -> 1.55 | 268.9 -> -76.9 |
| shr | 256x13 | 87.6 -> 42.9 | 35.8 -> 30.4 | 12.0 -> -33.0 | 2.27 -> 1.13 | 21.2 -> -23.7 |
| shr | 2000x13 | 692.2 -> 273.8 | 282.5 -> 196.0 | 164.0 -> -252.4 | 2.32 -> 0.89 | 179.5 -> -239.9 |
| shr | 2000x77 | 1064.4 -> 276.0 | 420.0 -> 196.1 | 535.7 -> -254.6 | 3.67 -> 0.96 | 546.1 -> -245.5 |
| mul | 32x32 | 298.7 -> 281.4 | - | 17.5 -> 2.3 | 1.12 -> 1.07 | 39.3 -> 23.1 |
| mul | 64x64 | 1039.3 -> 1007.8 | - | 23.9 -> -2.6 | 1.34 -> 1.30 | 53.3 -> 22.9 |
| mul | 256x256 | 11896.3 -> 12025.0 | - | 102.9 -> 238.8 | 1.51 -> 1.53 | 138.2 -> 229.3 |
| mul | 1000x1000 | 107250.2 -> 106887.8 | - | 178.5 -> -283.6 | 1.94 -> 1.94 | 80.4 -> 117.0 |
| mul | 2000x2000 | 302932.5 -> 301689.0 | - | -292.8 -> -722.3 | 2.03 -> 2.03 | 668.5 -> -700.3 |
| mul | 2000x100 | 47136.3 -> 46406.5 | - | 294.5 -> -291.4 | 1.35 -> 1.34 | 280.3 -> -261.8 |
| sqr | 64x64 | 626.4 -> 600.5 | - | 24.4 -> 0.7 | 0.93 -> 0.89 | 41.3 -> 18.3 |
| sqr | 512x512 | 24695.3 -> 24317.0 | - | 276.0 -> -72.0 | 1.25 -> 1.18 | 327.2 -> -27.6 |
| sqr | 2000x2000 | 224588.0 -> 223255.6 | - | 967.2 -> -545.5 | 1.85 -> 1.84 | 895.4 -> 31.3 |
| divrem | 128x64 | 3762.2 -> 3733.0 | - | 93.6 -> 75.2 | 2.12 -> 2.10 | 141.9 -> 143.5 |
| divrem | 512x256 | 28815.2 -> 28675.6 | - | 417.8 -> 72.8 | 1.75 -> 1.74 | 466.0 -> 215.7 |
| divrem | 2000x1000 | 234126.7 -> 232426.2 | - | 1975.2 -> 102.0 | 1.73 -> 1.73 | 1606.5 -> 322.1 |
| divrem | 4000x2000 | 695982.0 -> 691382.2 | - | 3426.0 -> 4024.5 | 2.00 -> 2.01 | 3967.5 -> 4357.2 |
| divrem | 2000x100 | 95716.3 -> 94547.3 | - | 1235.2 -> 259.8 | 1.38 -> 1.36 | 1567.7 -> 5.9 |
| divrem | 65536x1 | 246390.5 -> 231899.7 | - | 17772.4 -> -655.0 | 1.44 -> 1.35 | 16128.0 -> 1720.2 |
| fromchars | 64x10 | 2198.3 -> 2187.2 | - | - | 1.37 -> 1.36 | 338.6 -> 330.9 |
| fromchars | 256x10 | 13836.4 -> 13804.5 | - | - | 1.28 -> 1.29 | 2272.2 -> 2257.5 |
| fromchars | 2000x10 | 300171.0 -> 300852.0 | - | - | 1.47 -> 1.48 | 17382.0 -> 17573.8 |
| tochars | 64x10 | 4746.0 -> 4744.5 | - | - | 1.50 -> 1.50 | 250.2 -> 220.3 |
| tochars | 256x10 | 33794.6 -> 33818.9 | - | - | 1.66 -> 1.66 | 858.7 -> 1075.2 |
| tochars | 2000x10 | 679612.5 -> 676793.1 | - | - | 1.63 -> 1.63 | 11123.6 -> 7941.4 |

### 3.5 Large-band spot checks

x64:

| op | shape | auto ns | kernel ns | gmpz ns | auto/gmpz | change in auto |
|---|---|---:|---:|---:|---:|---:|
| add | 16384x16384 | 14810 -> 9330 | 9304 -> 9294 | 6898 | 2.15 -> 1.35 | -37.0% |
| divrem | 4096x2048 | 499034 -> 498477 | 497946 -> 495569 | 368707 | 1.35 -> 1.35 | -0.1% |
| fromchars | 10000x10 | 3670159 -> 3475893 | 3588344 -> 3362265 | 2005997 | 1.83 -> 1.73 | -5.3% |
| mul | 2000x2000 | 177314 -> 177858 | 176337 -> 176741 | 144537 | 1.23 -> 1.23 | +0.3% |
| mul | 16384x16384 | 3778231 -> 3779466 | 3778094 -> 3788186 | 1789186 | 2.11 -> 2.11 | +0.0% |
| tochars | 10000x10 | 5864746 -> 5960832 | 5844018 -> 5920813 | 4223943 | 1.39 -> 1.41 | +1.6% |

M4:

| op | shape | auto ns | kernel ns | gmpz ns | auto/gmpz | change in auto |
|---|---|---:|---:|---:|---:|---:|
| add | 16384x16384 | 9474 -> 7305 | 16179 -> 16418 | 4161 | 2.28 -> 1.76 | -22.9% |
| divrem | 4096x2048 | 706592 -> 707065 | 704150 -> 706127 | 358386 | 1.97 -> 1.97 | +0.1% |
| fromchars | 10000x10 | 4198921 -> 4189262 | 4137033 -> 4117279 | 1872966 | 2.24 -> 2.24 | -0.2% |
| mul | 2000x2000 | 303919 -> 311426 | 304464 -> 305902 | 150382 | 2.02 -> 2.07 | +2.5% |
| mul | 16384x16384 | 5443656 -> 5522911 | 5447302 -> 5449136 | 1599088 | 3.40 -> 3.45 | +1.5% |
| tochars | 10000x10 | 8278333 -> 8342500 | 8238222 -> 8247500 | 4195346 | 1.97 -> 1.99 | +0.8% |

### 3.6 Reading the tables, regressions, and the history of the two measurements

- Small `auto` on x64 improved by 2-4.5 ns for add/sub at 2-16 limbs (1x1: 8.2 -> 5.4 add, 9.0 -> 5.5 sub with the 1x1 fast
  path), 2-7 ns for shifts, 9-14 ns for mul/sqr and 5-35 ns for divrem. On M4, add/sub at 2-16 limbs are within about
  +-1.3 ns of the baseline (add 2x2 14.8 -> 15.9, 16x16 16.4 -> 17.7; its allocator pair is cheap, a floor only 3-6 ns above
  the kernel, so there was little to remove; add/sub 1x1 improved 6.9 -> 5.9 / 7.6 -> 5.7); M4 shifts improved by 3-6 ns,
  mul/sqr by 6-15 ns and divrem by 7-47 ns.
- Medium and large `auto`: x64 add/sub went from -5% at 64 limbs to -29% at 1024-2000 (add 1024x1024 842 -> 599 ns, 2000x2000
  1722 -> 1153), with `auto - floor` down to 4-5 ns; add 16384 is -37% (x64) and -23% (M4). Shifts at 2000 limbs -16..-31%
  (x64) and -42..-74% (M4); mul and divrem above ~256 limbs within +-1.5% except M4 mul 2000x2000 +2.5% and divrem 65536x1
  -10% (x64).
- **x64 `+=`/`-=` regression from the first measurement is fixed.** At 9145b4e add `inplace` 1024x1024 was 583 -> 790 ns
  (+35%) because `add_n_tail` did not reach the speed of `add_unsigned_spans` on x64; the follow-up loop shape (a plain loop
  under GCC, the 4-way one under clang) brings it to 581 ns, the baseline.
- **Decimal `fromchars` regression is fixed** (it was +8-11% at 64-256 limbs-worth of digits at 9145b4e); see section 3.7
  for the cause. Now x64 `auto` 64x10 2791 -> 2852 ns (+2%), 256x10 28564 -> 27920 (-2%), 2000x10 429k -> 386k (-10%),
  16x10 +0.8%; 10000x10 is -5.3%. M4 is unchanged (<= +-1%).
- **In-place shifts (`c <<= s; c >>= s`, per op) after the shift fix (b823c5f).** The history is: the first build
  (9145b4e, single in-place `lshift_copy`/`rshift_copy` pass) was faster than the baseline on M4 and slower on x64;
  d689f75 routed whole-limb-free in-place shifts to `shift_left_n`/`shift_right_n`, which made M4 much slower; b823c5f
  reverts that and uses register-carried loops under GCC only (section 3.8):

  | `inplace` ns | x64 base | x64 9145b4e | x64 d689f75 | x64 b823c5f | M4 base | M4 9145b4e | M4 d689f75 | M4 b823c5f |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|
  | shl 4x13 | 5.2 | 4.6 | 5.3 | 4.4 | 3.6 | 3.8 | 5.5 | 3.8 |
  | shl 16x13 | 9.2 | 9.1 | 9.3 | 8.6 | 5.8 | 5.3 | 6.9 | 5.4 |
  | shl 256x13 | 93.1 | 102.2 | 99.3 | 98.7 | 36.5 | 29.9 | 45.1 | 29.8 |
  | shl 2000x13 | 657.7 | 740.9 | 716.9 | 712.5 | 283.7 | 196.0 | 347.8 | 195.9 |
  | shr 2000x13 | 657.2 | 738.7 | 716.5 | 711.5 | 282.5 | 196.8 | 347.5 | 196.0 |

  Now M4 in-place shifts are 6-53% faster than the baseline at >= 16 limbs (shl/shr 2000x13 -31%, 256x13 -15..-18%,
  131072x13 -6%) and four small-shape rows are slower by 0.2-1.0 ns (shr 1x13 2.4 -> 3.3 ns +41%, shr 4x13 3.3 -> 3.6 +10%,
  shl 1x13 2.3 -> 2.5 +9%, shl 4x13 3.6 -> 3.8 +6%). On x64 the 1-16-limb rows are 8-17% faster than the baseline but
  >= 256 limbs stay +5-8% slower (256x13 +5-6%, 2000x13 +8%, 131072x13 +7%); the `auto` rows keep their gains on both
  machines (x64 -4..-49%, M4 -14..-74%), with x64 shr 256x13 `auto` down to -4% from -9%.
- **Rows more than 3% slower than the baseline** (`item1_regress.py` on the d689f75 runs with the shift rows replaced by the
  b823c5f runs; `auto` and `inplace`, small and medium bands, four-run medians):
  - x64: 9 of 107 rows. The in-place shift rows shl/shr 256x13 (+5-6%) and 2000x13 (+8%); add 2x2 `inplace` +0.4 ns
    (+5.9%), sub 2x2/4x4 `inplace` +0.2 ns (+4.4%, +3.4%); tochars 256x10 `auto` +3.1% and divrem 128x64 `auto` +3.1%
    (both borderline). Kernel rows (library code, not the front end): divrem 8x4 45.7 -> 50.3 ns (+10%), divrem 128x64
    +4.4%, sqr 4x4 9.1 -> 9.9 ns (+9%), within the 1-2 ns kernel noise at the smallest sizes. The 131072x13 in-place shift
    spot check is +7% (+3 us).
  - M4: 9 of 107 rows. The four small in-place shift rows above (+0.2-1.0 ns); add 2x2 `auto` +1.1 ns (+7%), add 16x16
    `auto` +1.3 ns (+7.7%), add/sub 2x2 `inplace` +0.3-0.6 ns, sub 4x4 `inplace` +0.1 ns.
  - No medium or large `auto` row on either machine is more than 3% slower, and no large spot check other than the x64
    131072x13 in-place shift (+7%) moves by more than +2.5% (M4 mul 2000x2000).
  - A single interleaved check at b823c5f of add/sub/mul/sqr/divrem in the small band (one run per binary) found nothing
    else moved against the d689f75 numbers beyond noise (x64 add 2x2 `inplace` 6.5 vs 6.2 ns; the M4 mul 6x6 and add 16x16
    `auto` rows flip between their usual noise modes).
  - These are the open items for item 2: the x64 GCC in-place shift gap at >= 256 limbs (+5-8%; the baseline in-place path
    is faster than every variant tried), the sub-nanosecond small add/sub in-place rows on both machines, and the M4 small
    add `auto` rows (+1.1-1.3 ns).
- **History.** The first measurement (opt_1 at 9145b4e, CSVs `x64_*`/`mac_*`) showed x64 add/sub `inplace` +23..+35% at
  >= 64 limbs, decimal `fromchars` +8-11%, and in-place shifts +10-13% on x64 but -30..-35% on M4. The follow-ups fixed the
  first two and the add/sub `auto` rows improved a further 20-26%; the in-place shift change in d689f75 traded the x64
  result for an M4 loss, and b823c5f (revert plus per-compiler loops) restored the M4 result and kept most of the x64 one.

### 3.7 Cause of the x64 `fromchars` regression (diagnosis and fix)

The regression showed with the header basecase shortcut compiled in and disappeared with it off, but the shortcut was only
the trigger. Facts:

- Instructions per call are equal (+-2%) in the baseline, shortcut-on and shortcut-off builds, so it is not extra work;
  IPC is lower with the shortcut (3.93 against 4.17-4.20 in `perf stat`, with 92% of uops from the decoded-uop cache
  against 99%). It is a stable +8-10%, independent of ASLR, stack offset and heap layout (checked with `setarch -R`,
  environment padding, `GLIBC_TUNABLES`).
- `digits_to_limbs` is byte-identical between the two builds apart from nop padding, with the hot loops at the same
  alignment, and 99.96% of the samples are in the out-of-line `digits_to_limbs<std::allocator<ull>> [clone .isra.0]`.
  Called standalone (inlined into a micro driver), the same function takes 20.9 us at 256 limbs in both builds, against
  25.4 us for the baseline in the harness: the harness copy is slower even without the shortcut.
- The cause is the hot loop of `mul_add_single_limb_in_place`: `widening_mul` returns the product through
  `wide<T>::from_int`, a `std::bit_cast` of the 128-bit value, and GCC 14 in that out-of-line copy stores the product to the
  stack and reloads both halves every iteration (`mulx; mov %r8,(%rsp); mov %r9,0x8(%rsp); mov (%rsp),%r8; mov 0x8(%rsp),%r9;
  add ...`). That store-forward round trip is the baseline's ~20% penalty; the shortcut changes inlining and register
  allocation in the harness translation unit enough to make the spilled loop a further ~8-10% slower.
- Fix (`d689f75`): under `BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL` compute `wide(s[i]) * mul + carry` and split it directly,
  keeping the `widening_mul` loop in the `#else`. The loop becomes `mulx; add; adc; mov; inc; cmp; jb`. Measured in a
  scratch copy (decimal `fromchars` kernel row, base / shortcut-on / shortcut-off / fixed): 64x10 2116 / 2400 / 2110-2160 /
  1680-1800 ns, 256x10 25.7k / 29.8k / 25.4k / 21.4-22.0k, 2000x10 401k / 398k / 401k / 361k. Changing `from_int` or
  `widening_mul` globally instead helps `fromchars` but regresses divrem 512x256 by +17% and tochars by +10%, so the fix
  has to stay local to this loop.

### 3.8 In-place shift A/B (variants of the whole-limb-free path)

Three variants built from scratch copies of d689f75 (outside the repository) and measured against `new64` (d689f75) and
`base64`, three interleaved runs per binary on both machines (rows `auto`, `inplace`, `kernel`; the unchanged `kernel`
rows are omitted; x64 pinned):

- V1: revert 2f9150b, so `<<=`/`>>=` go back to `lshift_copy`/`rshift_copy` (the earlier A2 behaviour);
- V2: V1 plus register-carried loops in `lshift_copy`/`rshift_copy` (one load per limb);
- V3: V2 with the combine written as `funnel_shl`/`funnel_shr` (identical to V2 under GCC, which has no builtin).

x64 (g++-14):

| op | shape | row | base64 | new64 | v1 | v2 | v3 |
|---|---|---|---:|---:|---:|---:|---:|
| shl | 1x13 | auto | 16.7 | 12.5 (-25%) | 12.6 (-25%) | 12.6 (-25%) | 12.6 (-25%) |
| shl | 1x13 | inplace | 4.1 | 4.2 (+3%) | 3.6 (-11%) | 3.4 (-15%) | 3.4 (-15%) |
| shl | 4x13 | auto | 18.6 | 13.4 (-28%) | 13.4 (-28%) | 13.4 (-28%) | 13.4 (-28%) |
| shl | 4x13 | inplace | 5.2 | 5.2 (+2%) | 4.5 (-12%) | 4.3 (-16%) | 4.3 (-16%) |
| shl | 16x13 | auto | 24.5 | 17.1 (-30%) | 17.0 (-31%) | 16.7 (-32%) | 16.8 (-32%) |
| shl | 16x13 | inplace | 9.2 | 9.2 (+1%) | 9.1 (-1%) | 8.4 (-8%) | 8.4 (-8%) |
| shl | 256x13 | auto | 133.9 | 122.0 (-9%) | 122.2 (-9%) | 122.0 (-9%) | 121.9 (-9%) |
| shl | 256x13 | inplace | 93.0 | 99.3 (+7%) | 102.2 (+10%) | 98.7 (+6%) | 98.7 (+6%) |
| shl | 2000x13 | auto | 917.5 | 772.8 (-16%) | 773.4 (-16%) | 773.6 (-16%) | 773.5 (-16%) |
| shl | 2000x77 | auto | 1063.4 | 774.6 (-27%) | 775.0 (-27%) | 774.5 (-27%) | 774.5 (-27%) |
| shl | 2000x13 | inplace | 656.7 | 716.9 (+9%) | 739.0 (+13%) | 712.5 (+9%) | 712.6 (+9%) |
| shl | 2000x77 | inplace | 765.7 | 741.7 (-3%) | 739.0 (-3%) | 712.5 (-7%) | 712.5 (-7%) |
| shl | 131072x13 | auto | 97977.3 | 49725.2 (-49%) | 49751.9 (-49%) | 49864.2 (-49%) | 49877.8 (-49%) |
| shl | 131072x13 | inplace | 43553.8 | 46689.0 (+7%) | 48260.2 (+11%) | 46565.6 (+7%) | 46554.9 (+7%) |
| shr | 1x13 | auto | 10.1 | 8.2 (-19%) | 8.2 (-19%) | 8.3 (-17%) | 8.3 (-17%) |
| shr | 1x13 | inplace | 3.6 | 3.9 (+7%) | 3.3 (-10%) | 3.0 (-17%) | 3.0 (-17%) |
| shr | 4x13 | auto | 20.9 | 16.1 (-23%) | 16.0 (-23%) | 15.8 (-24%) | 15.8 (-24%) |
| shr | 4x13 | inplace | 4.7 | 4.9 (+5%) | 4.3 (-8%) | 4.1 (-13%) | 4.1 (-13%) |
| shr | 16x13 | auto | 25.8 | 20.0 (-22%) | 20.0 (-22%) | 20.2 (-21%) | 20.2 (-21%) |
| shr | 16x13 | inplace | 8.7 | 8.9 (+2%) | 8.7 (-1%) | 8.0 (-8%) | 8.0 (-8%) |
| shr | 256x13 | auto | 136.6 | 125.0 (-9%) | 125.2 (-8%) | 133.6 (-2%) | 133.7 (-2%) |
| shr | 256x13 | inplace | 93.7 | 99.1 (+6%) | 101.7 (+8%) | 98.3 (+5%) | 98.3 (+5%) |
| shr | 2000x13 | auto | 949.9 | 749.0 (-21%) | 749.6 (-21%) | 788.0 (-17%) | 788.0 (-17%) |
| shr | 2000x77 | auto | 1086.8 | 749.5 (-31%) | 750.0 (-31%) | 790.0 (-27%) | 790.0 (-27%) |
| shr | 2000x13 | inplace | 657.1 | 716.5 (+9%) | 737.4 (+12%) | 711.5 (+8%) | 711.7 (+8%) |
| shr | 2000x77 | inplace | 765.5 | 740.8 (-3%) | 739.3 (-3%) | 712.1 (-7%) | 712.1 (-7%) |
| shr | 131072x13 | auto | 98933.2 | 48304.4 (-51%) | 48328.0 (-51%) | 50322.4 (-49%) | 50313.5 (-49%) |
| shr | 131072x13 | inplace | 43515.1 | 46701.2 (+7%) | 48481.1 (+11%) | 46567.5 (+7%) | 46580.6 (+7%) |

- new64: 8 of 28 auto/inplace rows more than 3% slower than base64: shl 2000x13 inplace +9%, shr 2000x13 inplace +9%, shr 131072x13 inplace +7%, shl 131072x13 inplace +7%, shl 256x13 inplace +7%, shr 1x13 inplace +7%, shr 256x13 inplace +6%, shr 4x13 inplace +5%
- v1: 6 of 28 auto/inplace rows more than 3% slower than base64: shl 2000x13 inplace +13%, shr 2000x13 inplace +12%, shr 131072x13 inplace +11%, shl 131072x13 inplace +11%, shl 256x13 inplace +10%, shr 256x13 inplace +8%
- v2: 6 of 28 auto/inplace rows more than 3% slower than base64: shl 2000x13 inplace +9%, shr 2000x13 inplace +8%, shr 131072x13 inplace +7%, shl 131072x13 inplace +7%, shl 256x13 inplace +6%, shr 256x13 inplace +5%
- v3: 6 of 28 auto/inplace rows more than 3% slower than base64: shl 2000x13 inplace +9%, shr 2000x13 inplace +8%, shr 131072x13 inplace +7%, shl 131072x13 inplace +7%, shl 256x13 inplace +6%, shr 256x13 inplace +5%

M4 (appleclang):

| op | shape | row | base64 | new64 | v1 | v2 | v3 |
|---|---|---|---:|---:|---:|---:|---:|
| shl | 1x13 | auto | 18.5 | 12.2 (-34%) | 12.5 (-32%) | 12.3 (-34%) | 12.1 (-35%) |
| shl | 1x13 | inplace | 2.3 | 3.4 (+48%) | 2.5 (+8%) | 2.5 (+8%) | 2.5 (+8%) |
| shl | 4x13 | auto | 19.9 | 12.7 (-36%) | 13.0 (-35%) | 13.2 (-33%) | 13.1 (-34%) |
| shl | 4x13 | inplace | 3.6 | 5.2 (+46%) | 3.8 (+5%) | 3.7 (+4%) | 3.8 (+7%) |
| shl | 16x13 | auto | 21.4 | 15.7 (-27%) | 15.5 (-28%) | 15.5 (-28%) | 16.3 (-24%) |
| shl | 16x13 | inplace | 5.8 | 6.8 (+18%) | 5.3 (-9%) | 6.3 (+9%) | 6.5 (+12%) |
| shl | 256x13 | auto | 85.6 | 41.3 (-52%) | 41.9 (-51%) | 56.3 (-34%) | 63.0 (-26%) |
| shl | 256x13 | inplace | 36.2 | 44.9 (+24%) | 29.7 (-18%) | 40.1 (+11%) | 48.0 (+32%) |
| shl | 2000x13 | auto | 639.0 | 264.0 (-59%) | 267.0 (-58%) | 395.3 (-38%) | 453.0 (-29%) |
| shl | 2000x77 | auto | 806.2 | 342.5 (-58%) | 478.0 (-41%) | 394.8 (-51%) | 452.9 (-44%) |
| shl | 2000x13 | inplace | 282.3 | 345.7 (+22%) | 196.1 (-31%) | 315.7 (+12%) | 375.2 (+33%) |
| shl | 2000x77 | inplace | 424.7 | 228.6 (-46%) | 230.9 (-46%) | 315.6 (-26%) | 377.1 (-11%) |
| shl | 131072x13 | auto | 48761.7 | 18032.3 (-63%) | 18150.9 (-63%) | 25035.9 (-49%) | 28931.4 (-41%) |
| shl | 131072x13 | inplace | 19766.8 | 22825.0 (+15%) | 18524.2 (-6%) | 20843.9 (+5%) | 24609.1 (+24%) |
| shr | 1x13 | auto | 9.2 | 6.7 (-27%) | 6.9 (-25%) | 6.8 (-26%) | 6.7 (-27%) |
| shr | 1x13 | inplace | 2.4 | 3.1 (+30%) | 3.3 (+40%) | 2.8 (+17%) | 2.8 (+18%) |
| shr | 4x13 | auto | 18.4 | 14.3 (-22%) | 15.1 (-18%) | 14.0 (-24%) | 14.3 (-22%) |
| shr | 4x13 | inplace | 3.3 | 4.5 (+37%) | 3.7 (+13%) | 3.3 (+1%) | 3.5 (+8%) |
| shr | 16x13 | auto | 21.5 | 18.1 (-16%) | 18.7 (-13%) | 18.1 (-16%) | 18.2 (-15%) |
| shr | 16x13 | inplace | 5.1 | 6.1 (+19%) | 5.2 (+1%) | 5.5 (+8%) | 5.7 (+11%) |
| shr | 256x13 | auto | 87.1 | 42.5 (-51%) | 43.1 (-50%) | 43.6 (-50%) | 48.3 (-45%) |
| shr | 256x13 | inplace | 35.8 | 44.6 (+25%) | 30.4 (-15%) | 39.7 (+11%) | 47.3 (+32%) |
| shr | 2000x13 | auto | 657.7 | 262.0 (-60%) | 258.7 (-61%) | 289.1 (-56%) | 340.8 (-48%) |
| shr | 2000x77 | auto | 1015.8 | 251.8 (-75%) | 266.6 (-74%) | 299.9 (-70%) | 339.2 (-67%) |
| shr | 2000x13 | inplace | 282.0 | 345.0 (+22%) | 197.8 (-30%) | 313.6 (+11%) | 374.1 (+33%) |
| shr | 2000x77 | inplace | 420.1 | 195.8 (-53%) | 195.7 (-53%) | 314.8 (-25%) | 377.9 (-10%) |
| shr | 131072x13 | auto | 50334.1 | 18098.3 (-64%) | 18178.1 (-64%) | 18015.5 (-64%) | 20633.5 (-59%) |
| shr | 131072x13 | inplace | 19886.4 | 22564.1 (+13%) | 18551.6 (-7%) | 20782.3 (+5%) | 24608.1 (+24%) |

- new64: 12 of 28 auto/inplace rows more than 3% slower than base64: shl 1x13 inplace +48%, shl 4x13 inplace +46%, shr 4x13 inplace +37%, shr 1x13 inplace +30%, shr 256x13 inplace +25%, shl 256x13 inplace +24%, shl 2000x13 inplace +22%, shr 2000x13 inplace +22%, shr 16x13 inplace +19%, shl 16x13 inplace +18%, shl 131072x13 inplace +15%, shr 131072x13 inplace +13%
- v1: 4 of 28 auto/inplace rows more than 3% slower than base64: shr 1x13 inplace +40%, shr 4x13 inplace +13%, shl 1x13 inplace +8%, shl 4x13 inplace +5%
- v2: 11 of 28 auto/inplace rows more than 3% slower than base64: shr 1x13 inplace +17%, shl 2000x13 inplace +12%, shr 2000x13 inplace +11%, shr 256x13 inplace +11%, shl 256x13 inplace +11%, shl 16x13 inplace +9%, shl 1x13 inplace +8%, shr 16x13 inplace +8%, shl 131072x13 inplace +5%, shr 131072x13 inplace +5%, shl 4x13 inplace +4%
- v3: 12 of 28 auto/inplace rows more than 3% slower than base64: shl 2000x13 inplace +33%, shr 2000x13 inplace +33%, shl 256x13 inplace +32%, shr 256x13 inplace +32%, shl 131072x13 inplace +24%, shr 131072x13 inplace +24%, shr 1x13 inplace +18%, shl 16x13 inplace +12%, shr 16x13 inplace +11%, shl 1x13 inplace +8%, shr 4x13 inplace +8%, shl 4x13 inplace +7%

Reading: no variant keeps every `auto`/`inplace` row within 3% of the baseline on both machines. On M4 (clang) V1 is the
best: it keeps the `auto` gains (-51..-64% at >= 256 limbs), makes every medium and large in-place row faster than the
baseline (-6..-31%), and leaves four small-shape in-place rows more than 3% slower, all by under 1 ns; the register-carried
loops (V2, V3) are slower than the original two-load form under clang. On x64 (GCC) V2/V3 improve the small in-place rows
by 8-17% against the baseline and equal `new64` at >= 256 limbs (+6-9%), at the price of 4-7 points on shr `auto`; the
baseline in-place path is still 6-9% faster there than any variant.

Outcome: b823c5f reverts 2f9150b and uses the register-carried loop bodies under GCC only (the original two-load loops
under clang and MSVC), the per-compiler split these numbers suggested. Re-measured at b823c5f (four interleaved runs per
binary on each machine, plus a 131072x13 spot) the result matches V1 on M4 and V2 on x64 (section 3.6).
The pairings measured are GCC 14 on x64 and appleclang on AArch64; clang on x64 and GCC on AArch64 were not.

## 4. Plan targets

Targets from the plan, with the achieved values (median of four runs; b823c5f for shifts, d689f75 for the rest). Kernel-row noise at <= 16 limbs is 1-2 ns
(section 8), so a miss of less than ~1.5 ns on an `inplace - kernel` target is within noise.

| target | x64 | M4 |
|---|---|---|
| `inplace - kernel` <= 3 ns for add/sub/shl/shr at 2-16 limbs | **mostly met**: add 3.9/2.7/2.8/3.6 (2/4/8/16 limbs, before 3.5/2.5/2.7/4.3), sub 2.7/2.8/3.0/2.6, shl 0.9/1.1, shr 0.7/-0.3 (add 2x2 and 16x16 are within kernel noise of the line) | **met**: add 2.7/1.9/0.8/-1.8, sub 2.4/2.1/0.9/-2.3, shl 0.0/1.3, shr -0.1/1.0 |
| `auto - floor` <= 3 ns at <= 16 limbs, add/sub/shl/shr | **not met except shifts**: add 5.7/4.5/4.4/5.6 (before 7.6/7.1/6.8/9.8; 1x1 -4.4), sub 5.8/5.9/5.9/5.7, shl 2.2/1.5 (met), shr 4.7/4.7 | **mostly not met**: add 6.3/4.2/3.6/3.7, sub 5.4/5.2/4.3/3.0, shl -0.1/3.9, shr 2.9/5.2 |
| `auto - floor` <= 5 ns at <= 16 limbs, mul/sqr | **met**: mul 1.8-4.2 (before 13.1-17.4); sqr 5.5 (4x4, marginal) and 4.2 | **met**: mul 2.9-4.8 (before 9.2-15.7); sqr 5.3 (4x4, marginal) and 4.0 |
| x64 add 1000 `auto`: 840 -> <= 650 ns | **met**: add 1024x1024 842 -> 599 ns (2000x2000 1722 -> 1153; `auto - floor` 4-5 ns at 64-2000 limbs) | n/a (M4: 565 -> 450 ns) |
| divrem 8x4 `auto - kernel`: 66 -> <= 2 floors + 5 ns (i.e. `auto - floor` <= 5) | **not met**: `auto - kernel` 67.0 -> 43.8, `auto - floor` 52.9 -> 26.3 | **not met**: `auto - kernel` 49.8 -> 28.8, `auto - floor` 34.8 -> 14.6 |
| medium and large rows: no regression > 3% | **not met only for in-place shifts at >= 256 limbs**: shl/shr 256x13 +5-6%, 2000x13 +8%, 131072x13 +7%; every other medium and large `auto` and `inplace` row and every other large spot check is within 3% | **met for medium and large rows**; only small rows exceed 3% (four in-place shift rows by 0.2-1.0 ns, add 2x2/16x16 `auto` +1.1/+1.3 ns, sub/add 2x2 `inplace` +0.3-0.6 ns); largest large spot check M4 mul 2000x2000 +2.5% |

The allocation targets in `alloc_count.test.cpp` all hold (section 5).

## 5. Allocation counts

Steady-state allocations per call (one warm-up call, then 64 calls, `alloc_count.test.cpp`), before -> after where a baseline
exists. Environments: `big_int` with a counting allocator, `pmr::big_int` with a counting default memory resource, and
`basic_big_int<N, limb, counting>`. All cases are enforced (`item1_pending = false`); the 128 and 512 columns were not
measured at cf1cf54. `c = a / b (2x1)` needs the heap for the 2-limb quotient at 64 bits (1 allocation); it is 0 from 128
bits up.

| case | `big_int` (counting allocator) | `pmr::big_int` | inline 128 | inline 256 | inline 512 |
|---|---|---|---|---|---|
| `c = a + b (16x16)` | 1 -> 1 | 1 -> 1 | 1 | 1 -> 1 | 1 |
| `c = a - b (16x16)` | 1 -> 1 | 1 -> 1 | 1 | 1 -> 1 | 1 |
| `c += b; c -= b (16x16)` | 0 -> 0 | 0 -> 0 | 0 | 0 -> 0 | 0 |
| `c = a << 13 (16 limbs)` | 1 -> 1 | 1 -> 1 | 1 | 1 -> 1 | 1 |
| `c = a >> 13 (16 limbs)` | 1 -> 1 | 1 -> 1 | 1 | 1 -> 1 | 1 |
| `c <<= 13; c >>= 13 (16 limbs)` | 0 -> 0 | 0 -> 0 | 0 | 0 -> 0 | 0 |
| `c = a * b (8x8)` | 1 -> 1 | 1 -> 1 | 1 | 1 -> 1 | 1 |
| `c = a; c *= b (8x8 -> 16)` | 1 -> 0 | 1 -> 0 | 0 | 1 -> 0 | 0 |
| `c = a; c *= 7 (8 limbs)` | 1 -> 0 | 1 -> 0 | 0 | 1 -> 0 | 0 |
| `c = a / b (8x4)` | 2 -> 1 | 2 -> 1 | 1 | 2 -> 1 | 0 |
| `c = a % b (8x4)` | 2 -> 1 | 2 -> 1 | 1 | 2 -> 0 | 0 |
| `div_rem_to_zero (8x4)` | 4 -> 2 | 4 -> 2 | 2 | 3 -> 1 | 0 |
| `c = a / b (2x1)` | 1 | 1 | - | - | - |
| `c = a; c /= b (8x4) [stretch]` | 3 -> 0 | 3 -> 0 | 0 | 2 -> 0 | 0 |
| `c = a; c %= b (8x4) [stretch]` | 2 -> 0 | 2 -> 0 | 0 | 2 -> 0 | 0 |
| `from_chars 2000 digits (reserved)` | 2 -> 2 | 2 -> 2 | 2 | 2 -> 2 | 2 |
| `c = a / b (2x1, inline q)` | - | - | 0 | 0 -> 0 | 0 |

The baseline `div_rem_to_zero` counts are the quotient (5 limbs), a 22-limb scratch and a remainder over-reserved to 9 limbs,
plus one more where the operands need normalising (an earlier version of the test also counted its own check's temporaries).
For the inline-capacity study, allocations per call at 1, 2, 4 and 8 limbs (operands with the top bit set, so sums, shifts
and products have the full limb count; `a / d` is n limbs over n/2):

| op | limbs | 64 | 128 | 256 | 512 |
|---|---:|---:|---:|---:|---:|
| `c = a + b` | 1 | 1 | 0 | 0 | 0 |
| `c = a - b` | 1 | 0 | 0 | 0 | 0 |
| `c = a << 13` | 1 | 1 | 0 | 0 | 0 |
| `c = a >> 13` | 1 | 0 | 0 | 0 | 0 |
| `c = a * b` | 1 | 1 | 0 | 0 | 0 |
| `c = a * a` | 1 | 1 | 0 | 0 | 0 |
| `c = a / d (n x n/2)` | 1 | 0 | 0 | 0 | 0 |
| `div_rem_to_zero (n x n/2)` | 1 | 0 | 0 | 0 | 0 |
| `c = a + b` | 2 | 1 | 1 | 0 | 0 |
| `c = a - b` | 2 | 1 | 0 | 0 | 0 |
| `c = a << 13` | 2 | 1 | 1 | 0 | 0 |
| `c = a >> 13` | 2 | 1 | 0 | 0 | 0 |
| `c = a * b` | 2 | 1 | 1 | 0 | 0 |
| `c = a * a` | 2 | 1 | 1 | 0 | 0 |
| `c = a / d (n x n/2)` | 2 | 1 | 0 | 0 | 0 |
| `div_rem_to_zero (n x n/2)` | 2 | 1 | 0 | 0 | 0 |
| `c = a + b` | 4 | 1 | 1 | 1 | 0 |
| `c = a - b` | 4 | 1 | 1 | 0 | 0 |
| `c = a << 13` | 4 | 1 | 1 | 1 | 0 |
| `c = a >> 13` | 4 | 1 | 1 | 0 | 0 |
| `c = a * b` | 4 | 1 | 1 | 1 | 0 |
| `c = a * a` | 4 | 1 | 1 | 1 | 0 |
| `c = a / d (n x n/2)` | 4 | 1 | 1 | 0 | 0 |
| `div_rem_to_zero (n x n/2)` | 4 | 2 | 1 | 0 | 0 |
| `c = a + b` | 8 | 1 | 1 | 1 | 1 |
| `c = a - b` | 8 | 1 | 1 | 1 | 0 |
| `c = a << 13` | 8 | 1 | 1 | 1 | 1 |
| `c = a >> 13` | 8 | 1 | 1 | 1 | 0 |
| `c = a * b` | 8 | 1 | 1 | 1 | 1 |
| `c = a * a` | 8 | 1 | 1 | 1 | 1 |
| `c = a / d (n x n/2)` | 8 | 1 | 1 | 1 | 0 |
| `div_rem_to_zero (n x n/2)` | 8 | 2 | 2 | 1 | 0 |

## 6. Header basecase shortcut (`mul_header_basecase_enabled`)

Measured on the 9145b4e tree, before the follow-ups: `new64` (shortcut on) against a build of the same tree with
`mul_header_basecase_enabled = false` (edited in a scratch copy, not committed), interleaved, three runs each, `auto` row:

x64:

| op | shape | shortcut on (new64) ns | off ns | on - off ns | runs on / off |
|---|---|---:|---:|---:|---|
| mul | 2x2 | 18.9 | 22.7 | -3.8 | 18.9/18.9/18.9 vs 22.7/22.7/22.7 |
| mul | 3x3 | 20.3 | 24.6 | -4.3 | 20.3/20.4/20.3 vs 24.6/24.4/24.7 |
| mul | 4x4 | 22.3 | 26.3 | -4.0 | 22.3/22.4/22.3 vs 26.3/26.3/26.4 |
| mul | 6x6 | 28.0 | 32.8 | -4.8 | 27.9/28.0/28.0 vs 33.4/32.7/32.8 |
| mul | 8x8 | 35.7 | 41.1 | -5.5 | 35.7/35.7/35.7 vs 41.3/41.1/41.0 |
| mul | 12x12 | 57.7 | 62.6 | -4.9 | 57.7/57.6/57.8 vs 62.6/62.6/62.5 |
| mul | 16x16 | 95.0 | 115.5 | -20.5 | 96.1/95.0/95.0 vs 99.8/115.5/116.0 |
| sqr | 4x4 | 22.0 | 27.3 | -5.3 | 22.2/22.0/21.9 vs 27.3/27.4/27.3 |
| sqr | 16x16 | 68.8 | 72.8 | -4.0 | 68.8/66.3/77.2 vs 71.2/73.3/72.8 |

M4:

| op | shape | shortcut on (new64) ns | off ns | on - off ns | runs on / off |
|---|---|---:|---:|---:|---|
| mul | 2x2 | 19.9 | 19.4 | +0.5 | 18.6/19.9/20.2 vs 19.4/19.3/19.9 |
| mul | 3x3 | 20.3 | 21.2 | -1.0 | 20.1/20.4/20.3 vs 23.8/21.2/21.1 |
| mul | 4x4 | 21.5 | 22.3 | -0.8 | 21.6/21.5/21.2 vs 22.2/22.3/22.4 |
| mul | 6x6 | 27.3 | 28.1 | -0.7 | 27.3/27.1/27.4 vs 28.1/28.3/27.6 |
| mul | 8x8 | 34.8 | 35.8 | -1.1 | 34.8/35.1/34.8 vs 35.9/35.7/35.8 |
| mul | 12x12 | 54.3 | 54.5 | -0.1 | 53.9/54.3/54.5 vs 54.2/54.6/54.5 |
| mul | 16x16 | 82.8 | 83.4 | -0.7 | 82.2/83.5/82.8 vs 82.9/83.4/83.9 |
| sqr | 4x4 | 21.5 | 23.2 | -1.7 | 21.4/21.5/21.6 vs 23.2/23.1/23.3 |
| sqr | 16x16 | 76.1 | 78.0 | -1.9 | 76.1/74.9/76.1 vs 78.3/78.0/77.3 |

Verdict: on x64 the shortcut is worth 3.8-5.5 ns on mul 2x2..12x12 and 4-5 ns on sqr 4 and 16 (the 16x16 difference of 20 ns
is noisy: one of the three shortcut-off runs was 99.8 ns and two were 115 ns, the rest of the data show ~4 ns), clearly above
the 1 ns threshold. On M4 it is worth -1.1 to +0.5 ns on mul 2..16 (in the noise of the runs; 2x2 is 0.5 ns slower with it) and 1.7-1.9 ns on sqr 4 and 16, so
it is at best marginal there. The x64 `fromchars` regression seen at that commit appeared only with the shortcut compiled in, but its cause was a spilled
product in `mul_add_single_limb_in_place` (section 3.7), which is fixed; with the fix the shortcut has no known cost, so
the verdict is: keep it on x64 (-4..5 ns, 13-20% of the small mul/sqr rows) and it is harmless and marginally useful on M4.
The A/B was not repeated on d689f75.

## 7. Inline-capacity study

Measured on the 9145b4e tree (before the add/sub, shift and single-limb follow-ups and the mul-add fix); the study was not
repeated on d689f75, so small add/sub/shift rows are a few ns better there at every N. The same front-end code with the
inline capacity of the integer type set to N = 64 (`big_int`), 128, 256 and 512 bits.
`sizeof`: 16, 24, 40 and 72 bytes. `auto` and `inplace` are medians of two runs (seeds 1 and 2, so divrem is the mean of
seeds); the vecsort row sorts and sums a `std::vector` of 100000 one-limb values of mixed sign (copy-assign + `std::sort` +
sum, ns per element; the builtin row is `std::vector<std::int64_t>`; the copy row is the copy-assign alone).

### 7.1 x64

**auto ns**

| op | shape | N=64 | N=128 | N=256 | N=512 |
|---|---|---:|---:|---:|---:|
| add | 1x1 | 6.7 | 8.3 | 8.6 | 8.7 |
| add | 4x4 | 15.4 | 17.6 | 10.6 | 10.6 |
| add | 16x16 | 21.0 | 24.2 | 23.5 | 23.5 |
| sub | 1x1 | 7.8 | 9.6 | 10.0 | 10.2 |
| sub | 4x4 | 16.6 | 19.5 | 12.1 | 12.1 |
| sub | 16x16 | 21.1 | 24.5 | 23.5 | 25.8 |
| shl | 1x13 | 12.6 | 7.2 | 8.2 | 8.3 |
| shl | 4x13 | 13.4 | 14.9 | 16.3 | 9.1 |
| shl | 16x13 | 17.1 | 18.2 | 20.5 | 19.5 |
| shr | 1x13 | 8.2 | 8.9 | 9.1 | 9.3 |
| shr | 4x13 | 16.0 | 16.8 | 11.9 | 11.9 |
| shr | 16x13 | 19.9 | 20.9 | 21.7 | 22.0 |
| mul | 1x1 | 16.5 | 10.5 | 10.8 | 10.8 |
| mul | 2x2 | 18.9 | 20.3 | 14.3 | 14.3 |
| mul | 4x4 | 22.3 | 24.0 | 24.3 | 17.8 |
| mul | 8x8 | 35.6 | 37.4 | 38.0 | 37.9 |
| mul | 16x16 | 88.5 | 90.4 | 91.9 | 91.3 |
| sqr | 4x4 | 22.1 | 23.8 | 24.4 | 17.4 |
| sqr | 16x16 | 67.2 | 68.0 | 69.3 | 70.2 |
| divrem | 2x1 | 35.4 | 34.5 | 36.7 | 36.6 |
| divrem | 8x4 | 91.9 | 94.9 | 89.1 | 80.2 |
| divrem | 32x16 | 339.6 | 344.6 | 343.7 | 342.3 |

**inplace ns**

| op | shape | N=64 | N=128 | N=256 | N=512 |
|---|---|---:|---:|---:|---:|
| add | 1x1 | 4.9 | 4.9 | 4.9 | 4.9 |
| add | 4x4 | 5.7 | 6.1 | 5.7 | 5.7 |
| add | 16x16 | 11.7 | 13.1 | 12.8 | 11.9 |
| sub | 1x1 | 4.7 | 4.7 | 4.6 | 4.6 |
| sub | 4x4 | 5.6 | 6.0 | 5.6 | 5.6 |
| sub | 16x16 | 11.1 | 11.1 | 11.2 | 11.0 |
| shl | 1x13 | 3.7 | 4.3 | 4.3 | 4.3 |
| shl | 4x13 | 4.6 | 4.8 | 4.7 | 5.2 |
| shl | 16x13 | 9.1 | 8.9 | 9.6 | 8.9 |
| shr | 1x13 | 3.3 | 3.4 | 3.5 | 3.4 |
| shr | 4x13 | 4.3 | 4.4 | 4.5 | 4.5 |
| shr | 16x13 | 8.7 | 8.5 | 8.5 | 9.0 |

**vecsort, ns per element (100000 one-limb values: copy + sort + sum)**

| row | N=64 | N=128 | N=256 | N=512 |
|---|---:|---:|---:|---:|
| auto | 86.13 | 92.31 | 98.59 | 103.44 |
| builtin | 39.44 | 39.39 | 39.41 | 39.33 |
| copy | 1.13 | 2.50 | 2.62 | 3.14 |

### 7.2 M4

**auto ns**

| op | shape | N=64 | N=128 | N=256 | N=512 |
|---|---|---:|---:|---:|---:|
| add | 1x1 | 7.5 | 8.3 | 9.2 | 10.1 |
| add | 4x4 | 14.1 | 14.2 | 9.3 | 10.0 |
| add | 16x16 | 17.0 | 16.3 | 16.6 | 16.4 |
| sub | 1x1 | 7.9 | 8.4 | 8.6 | 8.8 |
| sub | 4x4 | 14.4 | 14.0 | 9.2 | 9.8 |
| sub | 16x16 | 18.3 | 17.7 | 18.5 | 17.9 |
| shl | 1x13 | 11.9 | 6.6 | 7.3 | 6.9 |
| shl | 4x13 | 12.7 | 12.7 | 13.6 | 8.4 |
| shl | 16x13 | 15.5 | 15.5 | 17.0 | 20.2 |
| shr | 1x13 | 7.0 | 8.9 | 10.2 | 9.4 |
| shr | 4x13 | 13.9 | 14.0 | 10.2 | 9.8 |
| shr | 16x13 | 18.2 | 17.7 | 17.8 | 18.4 |
| mul | 1x1 | 14.5 | 9.1 | 9.3 | 9.2 |
| mul | 2x2 | 18.6 | 18.5 | 13.6 | 13.8 |
| mul | 4x4 | 21.6 | 21.6 | 21.8 | 15.0 |
| mul | 8x8 | 35.3 | 35.0 | 34.6 | 34.5 |
| mul | 16x16 | 82.8 | 82.8 | 82.5 | 82.8 |
| sqr | 4x4 | 22.8 | 23.2 | 21.2 | 15.4 |
| sqr | 16x16 | 75.6 | 74.2 | 73.9 | 74.5 |
| divrem | 2x1 | 24.6 | 23.2 | 23.6 | 23.8 |
| divrem | 8x4 | 75.0 | 75.2 | 74.7 | 72.6 |
| divrem | 32x16 | 429.7 | 430.0 | 431.3 | 432.8 |

**inplace ns**

| op | shape | N=64 | N=128 | N=256 | N=512 |
|---|---|---:|---:|---:|---:|
| add | 1x1 | 3.5 | 3.5 | 3.4 | 3.6 |
| add | 4x4 | 3.5 | 3.5 | 3.8 | 3.9 |
| add | 16x16 | 5.6 | 5.6 | 5.7 | 5.7 |
| sub | 1x1 | 3.5 | 3.6 | 3.7 | 3.7 |
| sub | 4x4 | 3.4 | 3.7 | 3.9 | 3.8 |
| sub | 16x16 | 5.2 | 5.3 | 5.3 | 5.3 |
| shl | 1x13 | 2.5 | 3.5 | 3.7 | 4.0 |
| shl | 4x13 | 3.8 | 3.8 | 4.2 | 4.5 |
| shl | 16x13 | 5.3 | 5.4 | 5.3 | 5.3 |
| shr | 1x13 | 3.3 | 3.4 | 3.4 | 3.3 |
| shr | 4x13 | 3.6 | 6.9 | 3.8 | 3.8 |
| shr | 16x13 | 5.2 | 5.2 | 5.3 | 5.3 |

**vecsort, ns per element (100000 one-limb values: copy + sort + sum)**

| row | N=64 | N=128 | N=256 | N=512 |
|---|---:|---:|---:|---:|
| auto | 87.87 | 94.78 | 96.50 | 98.30 |
| builtin | 13.21 | 13.38 | 13.18 | 13.27 |
| copy | 2.70 | 3.90 | 3.77 | 4.40 |

Neutral summary of the trade-off (no default is recommended here):

- **What a larger inline capacity removes.** Results that fit inline skip the allocator entirely: at 128 bits a 2-limb
  product, a 1-limb sum or a 2x1 quotient no longer allocates, at 256 bits operands up to 3-4 limbs, at 512 bits up to ~8.
  That is 4-11 ns per operation in the rows where the result fits (x64: add 4x4 15.4 ns at N=64 against 10.6 at N=256,
  mul 2x2 18.9 against 14.3 at N=256, mul 1x1 16.5 against 10.5 at N=128, divrem 8x4 91.9 against 80.2 at N=512; M4: add
  4x4 14.1 against 9.3, mul 2x2 18.6 against 13.6, mul 1x1 14.5 against 9.1).
- **What it costs.** `sizeof` grows from 16 to 24, 40 and 72 bytes. Rows whose result does not fit inline are slightly
  slower or unchanged: on x64 add 16x16 is 21.0 ns at N=64 and 23.5-24.2 at N=128-512, shl 16x13 17.1 against 18.2-20.5, mul
  16x16 88.5 against 90.4-91.9; on M4 the 16-limb rows are within 1 ns except shl 16x13 (15.5 at N=64, 20.2 at N=512). Some
  single-limb rows are slower at larger N (x64 add 1x1 6.7 ns at N=64, 8.3-8.7 at N=128-512; M4 7.5 against 8.3-10.1). The
  container workload (vecsort) pays for the bigger element: x64
  86.1 -> 92.3 -> 98.6 -> 103.4 ns per element (+7%, +14%, +20% against N=64), M4 87.9 -> 94.8 -> 96.5 -> 98.3 ns (+8%,
  +10%, +12%); the copy-assign alone goes 1.1 -> 2.5 -> 2.6 -> 3.1 ns on x64 and 2.7 -> 3.9 -> 3.8 -> 4.4 ns on M4. A
  `std::int64_t` vector costs 39.4 ns (x64) and 13.2 ns (M4) per element for reference.
- **In-place operations** and divrem at 32x16 do not depend on N within ~1.5 ns.

## 8. Caveats

- **Noise modes at <= 16 limbs.** Kernel rows at tiny sizes flip between two stable modes from process to process with the
  same data and binary (x64 baseline: `add kernel 16x16` 7.56 / 9.55 / 7.56 ns, `shl kernel 4x13` 3.38 / 4.65 / 3.45,
  `shr kernel 16x13` 9.39 / 8.04 / 8.03, `sqr kernel 16x16` 56.2 / 61.2 / 58.5), probably buffer or code alignment. Differences
  of 1-2 ns on kernel rows, and therefore on `auto - kernel` and `inplace - kernel` at <= 16 limbs, are not significant;
  the `floor` and `inplace` rows are the steadier references. The M4 baseline mul 2x2 `auto` varied 26.6-36.3 ns across its
  four runs (the new binary 18.2-19.1).
- **divrem seed dependence.** Different random operands change the division path: divrem 8x4 differs 11-26% between seeds
  (kernel 39.7 vs 50.0 ns in the baseline). Small divrem rows are means of seeds 1 and 2; compare before and after only on
  the same seeds.
- The shift `kernel` rows include the copy-in of the source, so `auto - kernel` and `auto - floor` for shifts at
  >= 256 limbs are negative once `auto` is a single pass.
- The x64 numbers are GCC 14 with `-march=native`; the decimal `fromchars` regression (section 3.7) was seen only there.
  The loop-shape choices for add/sub and the shift variants were measured with GCC 14 on x64 and appleclang on M4 only;
  clang on x64 and GCC on AArch64 were not measured.
- The 2-run study and the 3-run shortcut comparison are small samples: treat differences below ~1 ns as noise.

## 9. Correctness matrix

At d689f75 (ctest counts include skipped benches); at b823c5f, which changes only the shift code, the GCC-only loop path was
re-run: x64 `gcc-release` and `gcc-debug` (g++-14) 1363/1363 each, Docker linux/arm64 `gcc-release` and `gcc-debug`
1292/1292 each, M4 `appleclang-release` 1304/1304:

- M4 (appleclang): `appleclang-debug` workflow (MaxSan) 1304/1304, `appleclang-release` 1304/1304,
  `appleclang-release-namespace` 1290/1290.
- Docker linux/arm64 (GCC 16, `ctest -E float_construction`): `gcc-release` and `gcc-debug` 1292/1292 each.
- x64 box: `llvm-debug` (MaxSan) and `llvm-release` (clang-23, `-march=native`) 1381/1381 each; `gcc-release` with g++-14
  and with g++-13 (`-march=native`), the IFMA=OFF g++-14 build and `gcc-debug` (g++-14, no NDEBUG, so the debug poison fill
  and the debug assertions of the new primitives are compiled) 1363/1363 each, with no build failures and no
  `-Wno-error` overrides.
- 32-bit limbs (`llvm-release`, `-DBEMAN_BIG_INT_FORCED_LIMB_WIDTH=32`, x64): 36 of 1088 tests fail, all of them failures
  that the cf1cf54 tree has as well (bitwise, increment_decrement, karatsuba, pmr and wide_ops do not compile; some
  Addition, Subtraction, CompoundSubtraction, BitShift and IntegerAssignment cases assume 64-bit limbs; the baseline has 40
  failures). `inline_tail`, `span_primitives`, `front_end_add_sub`, `front_end_shift`, `muldiv_frontend`,
  `dispatch_contract`, `alloc_count` (85 tests) and `allocation` (70 tests) all pass at 32-bit limbs.
- At 9145b4e (the first measurement) the GCC builds failed on `front_end_reference.hpp:44` (`-Werror=padded`) and the
  new `front_end_add_sub` and `muldiv_frontend` tests failed at 32-bit limbs through 64-bit assumptions; both were fixed
  in the follow-ups.


## 10. Reproducing

```
# harness binaries (M4 shown; x64: PATH=<gcc14 shim>:$PATH, preset gcc-release, -DCMAKE_CXX_FLAGS=-march=native)
C="-DBEMAN_BIG_INT_BUILD_BENCHMARKS=ON -DBEMAN_BIG_INT_SHAPE_SWEEP_ONLY=ON -DBEMAN_BIG_INT_SWEEP_GMP=ON -DBEMAN_BIG_INT_SIMD_MUL=ON"
cmake --preset appleclang-release -B build/item1/mac-new64 $C -DBEMAN_BIG_INT_SWEEP_INLINE_BITS=64    # also 128, 256, 512
cmake --build build/item1/mac-new64 --target beman.big_int.benchmarks.shape_sweep
# before binary: the same command on a checkout of cf1cf54 with this harness (floor/vecsort rows, inline-bits option)

# before/after, interleaved (seeds 1 and 2, two repeats), x64 pinned with --pin "taskset -c 2"
tools/gap_sweep.sh <bin> out.csv --seed S --ops add,sub,shl,shr,mul,sqr,divrem,fromchars --bands small,medium
# large spot checks (3 rounds): shape_sweep add --rows auto,inplace,kernel,floor,gmp,gmpz --rounds 3 16384x16384;
#   mul 2000x2000 and 16384x16384; divrem 4096x2048; tochars/fromchars 10000x10
# study: tools/gap_sweep.sh <new{64,128,256,512}> out.csv --seed S --ops add,sub,shl,shr,mul,sqr,divrem --bands small
#        tools/gap_sweep.sh <bin> out.csv --ops vecsort --bands medium
# tables: python3 -I tools/item1_tables.py item1 x64r3   (x64, mac, macr3 for the other sets) [small medium spots shortcut study]
# regression check (rows more than 3% slower than base64): python3 -I tools/item1_regress.py item1 x64r3 3 [rows]
# in-place shift A/B tables: python3 -I tools/item1_vshift.py item1 x64   (or: mac)
# re-measurement driver (base64 vs new64 vs previous build): the same loop as tools/item1_bench.sh over
#   --ops add,sub,shl,shr,mul,sqr,divrem,fromchars,tochars --bands small,medium
# allocation counts: cmake --build <dir> --target beman.big_int.tests.alloc_count; run the binary and read the
#   "[alloc_count]" lines
```

The shortcut comparison builds a copy of the tree with `mul_header_basecase_enabled = false` in
`include/beman/big_int/detail/mul_impl.hpp` and otherwise identical flags.
