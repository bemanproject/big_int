<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

# Item 1 results: cutting the fixed per-call front-end cost

Item 1 of [`gmp_gap_analysis.md`](gmp_gap_analysis.md) section 6 targeted the 13-18 ns that `+ - << >>` add on top of the
span kernel at <= 16 limbs, the ~21 ns for small `*`, and the 37-66 ns for small divrem. This document records what changed,
how it was measured, and where the targets were and were not met. Numbers are nanoseconds per operation unless noted;
"before" is the library at cf1cf54 and "after" is `opt_1` at 9145b4e, both measured with the same harness (`shape_sweep.cpp`,
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
- **add and sub.** `add_in_place` and `add_into` use the tail-aware `add_n_tail`/`sub_n_tail` (4-way unrolled, stops once the
  carry or borrow dies), grow only for the carry limb, set sign and size once, and write the result with direct stores into
  `storage_for_overwrite` storage: no zero re-scan, no one-step trim.
- **Shifts.** `c = a << s` and `c = a >> s` build the result from the source in one pass (`lshift_copy`/`rshift_copy`) into an
  exactly sized buffer; `<<=` and `>>=` shift in one in-place pass without reserving a spare limb the value does not need
  (so `1 << 127` stays inline in a 128-bit `basic_big_int`). Negative right shifts keep floor rounding by detecting
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

Cells read `before -> after`. `auto/gmpz` is the user-visible gap to GMP (above 1 means `big_int` is slower).

### 3.1 x64 (g++-14, -march=native), small band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 1x1 | 8.2 -> 6.7 | 4.1 -> 4.9 | -1.6 -> -3.1 | 1.98 -> 1.61 | 6.3 -> 4.8 |
| add | 2x2 | 17.8 -> 15.9 | 5.9 -> 6.1 | 7.6 -> 5.6 | 4.28 -> 3.80 | 15.5 -> 13.5 |
| add | 4x4 | 18.1 -> 15.4 | 5.6 -> 5.7 | 7.2 -> 4.3 | 4.11 -> 3.50 | 15.1 -> 12.3 |
| add | 8x8 | 19.4 -> 16.8 | 7.4 -> 7.1 | 6.9 -> 4.2 | 4.12 -> 3.57 | 14.8 -> 12.2 |
| add | 16x16 | 25.4 -> 20.9 | 11.8 -> 11.9 | 9.8 -> 5.3 | 3.86 -> 3.21 | 17.8 -> 13.4 |
| sub | 1x1 | 9.1 -> 7.8 | 4.0 -> 4.7 | 7.2 -> 5.9 | 2.09 -> 1.79 | 7.6 -> 6.3 |
| sub | 2x2 | 19.1 -> 15.8 | 4.6 -> 5.1 | 9.1 -> 5.8 | 4.40 -> 3.63 | 17.0 -> 13.7 |
| sub | 4x4 | 18.9 -> 16.6 | 5.5 -> 5.6 | 8.2 -> 5.8 | 4.08 -> 3.61 | 16.1 -> 13.7 |
| sub | 8x8 | 20.6 -> 18.0 | 7.2 -> 7.0 | 8.2 -> 5.6 | 4.11 -> 3.51 | 16.2 -> 13.6 |
| sub | 16x16 | 25.8 -> 21.1 | 10.7 -> 11.2 | 10.4 -> 5.7 | 3.76 -> 3.09 | 17.7 -> 13.0 |
| shl | 1x13 | 16.8 -> 12.6 | 4.1 -> 3.7 | 7.0 -> 2.8 | 4.83 -> 3.87 | 14.7 -> 10.6 |
| shl | 4x13 | 18.7 -> 13.5 | 5.2 -> 4.6 | 7.5 -> 2.3 | 4.63 -> 3.34 | 15.2 -> 10.0 |
| shl | 16x13 | 24.5 -> 17.1 | 9.3 -> 9.1 | 9.3 -> 1.9 | 4.21 -> 2.94 | 17.0 -> 9.6 |
| shr | 1x13 | 10.2 -> 8.2 | 3.7 -> 3.3 | 7.9 -> 5.9 | 3.12 -> 2.36 | 8.5 -> 6.5 |
| shr | 4x13 | 21.1 -> 16.1 | 4.7 -> 4.6 | 9.0 -> 5.0 | 5.31 -> 3.82 | 17.6 -> 12.6 |
| shr | 16x13 | 26.1 -> 20.0 | 8.8 -> 8.7 | 9.0 -> 4.4 | 4.18 -> 3.16 | 17.9 -> 11.1 |
| mul | 2x2 | 27.9 -> 18.9 | - | 12.6 -> 3.9 | 4.54 -> 3.08 | 20.3 -> 11.3 |
| mul | 3x3 | 29.6 -> 20.2 | - | 12.9 -> 3.8 | 2.96 -> 2.03 | 20.6 -> 11.3 |
| mul | 4x4 | 32.0 -> 22.3 | - | 13.3 -> 4.1 | 2.62 -> 1.83 | 20.9 -> 11.4 |
| mul | 6x6 | 41.9 -> 28.0 | - | 17.0 -> 3.6 | 2.30 -> 1.54 | 24.4 -> 10.9 |
| mul | 8x8 | 47.2 -> 35.9 | - | 14.3 -> 3.5 | 1.63 -> 1.24 | 21.6 -> 10.5 |
| mul | 12x12 | 70.2 -> 57.9 | - | 14.4 -> 3.1 | 1.38 -> 1.14 | 22.4 -> 10.1 |
| mul | 16x16 | 101.8 -> 88.5 | - | 14.8 -> 2.2 | 1.21 -> 1.05 | 22.1 -> 9.4 |
| sqr | 4x4 | 33.4 -> 22.2 | - | 16.5 -> 5.4 | 3.78 -> 2.51 | 24.3 -> 12.2 |
| sqr | 16x16 | 79.7 -> 66.4 | - | 15.9 -> 3.7 | 1.51 -> 1.26 | 23.4 -> 9.1 |
| divrem | 2x1 | 41.6 -> 35.4 | - | 28.8 -> 22.6 | 3.33 -> 2.84 | 36.1 -> 29.7 |
| divrem | 8x4 | 112.8 -> 94.7 | - | 53.0 -> 27.1 | 2.53 -> 2.07 | 67.7 -> 43.8 |
| divrem | 32x16 | 372.0 -> 340.2 | - | 67.6 -> 34.2 | 1.92 -> 1.73 | 82.6 -> 49.7 |
| fromchars | 4x10 | 195.2 -> 116.1 | - | - | 2.86 -> 1.68 | 154.2 -> 77.3 |
| fromchars | 16x10 | 420.1 -> 433.5 | - | - | 1.63 -> 1.68 | 209.5 -> 217.6 |

### 3.2 M4 (appleclang), small band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 1x1 | 6.8 -> 7.7 | 3.2 -> 3.5 | -2.7 -> -1.7 | 2.07 -> 2.58 | 5.7 -> 6.5 |
| add | 2x2 | 14.6 -> 15.2 | 3.8 -> 3.9 | 5.0 -> 5.6 | 2.84 -> 4.58 | 13.2 -> 13.8 |
| add | 4x4 | 15.2 -> 14.3 | 3.7 -> 3.5 | 4.8 -> 4.1 | 3.20 -> 2.82 | 13.3 -> 12.4 |
| add | 8x8 | 15.6 -> 15.1 | 5.0 -> 4.0 | 3.6 -> 3.1 | 3.02 -> 3.39 | 12.1 -> 11.7 |
| add | 16x16 | 16.3 -> 16.9 | 9.6 -> 5.6 | 2.7 -> 3.2 | 3.63 -> 3.96 | 8.8 -> 9.3 |
| sub | 1x1 | 7.6 -> 8.0 | 3.4 -> 3.5 | 5.8 -> 6.3 | 2.57 -> 3.23 | 6.5 -> 7.0 |
| sub | 2x2 | 15.0 -> 14.7 | 3.3 -> 3.5 | 5.6 -> 5.3 | 4.63 -> 5.14 | 13.6 -> 13.3 |
| sub | 4x4 | 15.9 -> 14.7 | 3.7 -> 3.6 | 5.8 -> 4.3 | 5.33 -> 5.38 | 14.0 -> 12.8 |
| sub | 8x8 | 16.2 -> 15.6 | 5.2 -> 4.1 | 4.8 -> 3.8 | 4.66 -> 4.83 | 12.7 -> 12.1 |
| sub | 16x16 | 18.1 -> 17.5 | 11.0 -> 5.2 | 3.1 -> 2.4 | 4.24 -> 4.07 | 10.4 -> 9.8 |
| shl | 1x13 | 16.8 -> 12.4 | 2.3 -> 2.5 | 5.5 -> 1.4 | 8.46 -> 6.24 | 14.5 -> 10.2 |
| shl | 4x13 | 19.2 -> 13.0 | 3.6 -> 3.8 | 6.3 -> -0.1 | 7.02 -> 4.77 | 15.4 -> 9.3 |
| shl | 16x13 | 21.4 -> 16.2 | 5.8 -> 5.3 | 9.4 -> 3.9 | 4.29 -> 3.15 | 17.3 -> 12.0 |
| shr | 1x13 | 9.4 -> 6.6 | 2.3 -> 3.3 | 6.9 -> 4.1 | 4.74 -> 3.32 | 7.2 -> 4.4 |
| shr | 4x13 | 18.2 -> 14.0 | 3.2 -> 3.7 | 6.3 -> 2.0 | 6.67 -> 5.13 | 14.5 -> 10.3 |
| shr | 16x13 | 21.0 -> 18.4 | 5.1 -> 5.2 | 7.9 -> 5.4 | 4.97 -> 4.34 | 16.9 -> 14.2 |
| mul | 2x2 | 30.5 -> 18.6 | - | 15.8 -> 4.0 | 4.59 -> 2.74 | 24.0 -> 12.2 |
| mul | 3x3 | 30.4 -> 19.5 | - | 13.2 -> 2.8 | 3.69 -> 2.27 | 21.9 -> 11.3 |
| mul | 4x4 | 29.5 -> 21.6 | - | 12.5 -> 4.8 | 2.70 -> 2.04 | 18.3 -> 10.5 |
| mul | 6x6 | 33.0 -> 27.3 | - | 9.2 -> 3.6 | 1.77 -> 1.47 | 13.9 -> 8.4 |
| mul | 8x8 | 41.5 -> 34.8 | - | 11.1 -> 4.3 | 1.56 -> 1.29 | 18.6 -> 11.9 |
| mul | 12x12 | 61.8 -> 54.0 | - | 12.7 -> 4.5 | 1.33 -> 1.16 | 20.8 -> 12.9 |
| mul | 16x16 | 94.9 -> 82.7 | - | 16.3 -> 4.2 | 1.20 -> 1.05 | 25.2 -> 13.2 |
| sqr | 4x4 | 29.7 -> 21.6 | - | 13.4 -> 5.2 | 3.14 -> 2.27 | 18.1 -> 10.4 |
| sqr | 16x16 | 89.8 -> 75.3 | - | 18.0 -> 2.9 | 1.36 -> 1.14 | 27.6 -> 11.5 |
| divrem | 2x1 | 31.7 -> 24.8 | - | 17.8 -> 11.3 | 3.60 -> 2.86 | 25.7 -> 18.8 |
| divrem | 8x4 | 96.4 -> 75.8 | - | 35.5 -> 14.9 | 2.21 -> 1.73 | 50.1 -> 29.0 |
| divrem | 32x16 | 479.3 -> 432.2 | - | 69.3 -> 21.1 | 2.29 -> 2.06 | 87.6 -> 37.8 |
| fromchars | 4x10 | 92.8 -> 91.6 | - | - | 1.22 -> 1.18 | 60.0 -> 58.9 |
| fromchars | 16x10 | 397.2 -> 397.8 | - | - | 1.25 -> 1.25 | 203.7 -> 205.8 |

### 3.3 x64, medium band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 64x64 | 63.6 -> 57.6 | 39.1 -> 48.1 | 22.3 -> 15.8 | 3.86 -> 3.49 | 30.4 -> 24.7 |
| add | 256x256 | 223.8 -> 213.5 | 145.9 -> 196.1 | 65.0 -> 54.3 | 2.81 -> 2.69 | 81.3 -> 71.1 |
| add | 1024x1024 | 841.5 -> 800.1 | 583.0 -> 790.1 | 247.3 -> 205.5 | 2.15 -> 2.04 | 263.0 -> 222.4 |
| add | 2000x2000 | 1721.2 -> 1535.4 | 1134.9 -> 1520.5 | 571.6 -> 384.7 | 3.59 -> 3.19 | 588.5 -> 403.3 |
| sub | 64x64 | 66.5 -> 58.8 | 37.9 -> 47.1 | 24.4 -> 16.5 | 3.97 -> 3.51 | 33.6 -> 26.1 |
| sub | 1024x1024 | 841.5 -> 808.6 | 581.7 -> 791.5 | 247.4 -> 214.7 | 2.15 -> 2.07 | 263.4 -> 230.8 |
| shl | 256x13 | 135.5 -> 122.0 | 93.1 -> 102.2 | 19.0 -> 5.5 | 2.69 -> 2.45 | 41.1 -> 28.2 |
| shl | 2000x13 | 916.5 -> 772.8 | 657.2 -> 740.9 | 176.7 -> 33.6 | 2.71 -> 2.29 | 196.5 -> 53.1 |
| shl | 2000x77 | 1065.3 -> 774.8 | 764.5 -> 741.5 | 193.2 -> -97.1 | 3.13 -> 2.29 | 215.4 -> -75.6 |
| shr | 256x13 | 138.6 -> 125.5 | 93.8 -> 101.7 | 7.3 -> -4.9 | 2.73 -> 2.47 | 28.4 -> 15.0 |
| shr | 2000x13 | 949.4 -> 750.3 | 657.2 -> 738.7 | 117.2 -> -81.7 | 2.80 -> 2.21 | 137.6 -> -61.8 |
| shr | 2000x77 | 1084.2 -> 750.6 | 764.9 -> 740.1 | 232.6 -> -102.0 | 3.20 -> 2.21 | 254.9 -> -79.6 |
| mul | 32x32 | 212.8 -> 200.5 | - | 12.8 -> 0.2 | 0.77 -> 0.74 | 24.1 -> 6.2 |
| mul | 64x64 | 517.4 -> 498.3 | - | 24.8 -> 3.6 | 0.60 -> 0.57 | 30.8 -> 12.8 |
| mul | 256x256 | 6254.4 -> 6185.1 | - | 18.2 -> 4.9 | 0.80 -> 0.79 | 81.2 -> 15.4 |
| mul | 1000x1000 | 59840.1 -> 59737.0 | - | 418.4 -> 167.6 | 1.11 -> 1.11 | 378.1 -> 250.3 |
| mul | 2000x2000 | 177078.2 -> 176669.1 | - | 698.5 -> 219.4 | 1.23 -> 1.23 | 778.0 -> 253.8 |
| mul | 2000x100 | 17857.2 -> 17640.0 | - | 23.0 -> -55.9 | 0.50 -> 0.49 | 192.1 -> -20.1 |
| sqr | 64x64 | 302.6 -> 276.1 | - | 22.7 -> -9.7 | 0.54 -> 0.50 | 32.9 -> -4.1 |
| sqr | 512x512 | 10629.4 -> 10582.1 | - | 63.0 -> -13.5 | 0.71 -> 0.70 | 89.1 -> 22.9 |
| sqr | 2000x2000 | 100001.6 -> 98205.6 | - | 1537.8 -> 79.5 | 0.99 -> 0.98 | 801.6 -> 16.8 |
| divrem | 128x64 | 3364.7 -> 3311.6 | - | 125.0 -> 47.5 | 1.94 -> 1.92 | 120.4 -> 84.6 |
| divrem | 512x256 | 29529.0 -> 29508.4 | - | 289.9 -> 287.0 | 1.81 -> 1.82 | 225.1 -> 342.1 |
| divrem | 2000x1000 | 185683.5 -> 185240.7 | - | 1297.0 -> -229.8 | 1.35 -> 1.34 | 1096.5 -> 20.4 |
| divrem | 4000x2000 | 478299.8 -> 474978.9 | - | 3602.0 -> -216.3 | 1.42 -> 1.41 | 4662.0 -> -563.2 |
| divrem | 2000x100 | 145266.8 -> 144972.4 | - | 870.1 -> 2397.5 | 2.00 -> 2.01 | 747.3 -> 845.4 |
| divrem | 65536x1 | 193782.3 -> 174417.1 | - | 19066.6 -> 6342.3 | 1.31 -> 1.18 | 26686.7 -> 7091.7 |
| fromchars | 64x10 | 2777.3 -> 3094.7 | - | - | 2.00 -> 2.22 | 644.3 -> 660.9 |
| fromchars | 256x10 | 28412.5 -> 30825.9 | - | - | 2.84 -> 3.10 | 2681.4 -> 1265.8 |
| fromchars | 2000x10 | 429598.0 -> 427566.8 | - | - | 2.17 -> 2.16 | 20495.5 -> 21856.1 |

### 3.4 M4, medium band

| op | shape | auto ns | inplace ns | auto - floor ns | auto/gmpz | auto - kernel ns |
|---|---|---:|---:|---:|---:|---:|
| add | 64x64 | 54.5 -> 38.2 | 51.3 -> 20.0 | -7.4 -> -22.6 | 4.22 -> 2.90 | 5.3 -> -11.0 |
| add | 256x256 | 147.1 -> 109.9 | 239.5 -> 100.9 | -98.2 -> -136.3 | 2.54 -> 1.90 | -88.3 -> -124.8 |
| add | 1024x1024 | 551.5 -> 450.1 | 985.4 -> 433.6 | -433.4 -> -539.9 | 2.18 -> 1.78 | -416.4 -> -520.8 |
| add | 2000x2000 | 1126.7 -> 886.7 | 1935.7 -> 854.2 | -809.8 -> -1046.7 | 2.27 -> 1.78 | -788.3 -> -1030.2 |
| sub | 64x64 | 40.9 -> 35.9 | 50.9 -> 19.9 | -19.5 -> -24.3 | 2.96 -> 2.59 | -8.5 -> -13.6 |
| sub | 1024x1024 | 547.5 -> 453.1 | 985.9 -> 434.5 | -428.7 -> -524.8 | 2.16 -> 1.79 | -422.4 -> -522.7 |
| shl | 256x13 | 85.9 -> 41.3 | 36.4 -> 29.8 | 28.2 -> -16.1 | 2.27 -> 1.09 | 36.7 -> -7.8 |
| shl | 2000x13 | 638.6 -> 262.4 | 283.2 -> 196.5 | 238.0 -> -138.1 | 0.29 -> 0.13 | 255.1 -> -121.1 |
| shl | 2000x77 | 790.7 -> 458.6 | 424.8 -> 228.8 | 248.5 -> -81.0 | 2.73 -> 1.59 | 262.7 -> -70.9 |
| shr | 256x13 | 87.3 -> 42.6 | 36.0 -> 30.4 | 11.6 -> -32.7 | 2.29 -> 1.12 | 20.9 -> -23.9 |
| shr | 2000x13 | 706.8 -> 255.1 | 283.0 -> 196.3 | 182.0 -> -270.0 | 2.36 -> 0.86 | 194.1 -> -258.7 |
| shr | 2000x77 | 1057.9 -> 276.4 | 420.0 -> 195.8 | 532.0 -> -249.9 | 3.65 -> 0.95 | 540.9 -> -240.3 |
| mul | 32x32 | 298.2 -> 281.8 | - | 18.6 -> 3.1 | 1.12 -> 1.06 | 39.5 -> 23.4 |
| mul | 64x64 | 1037.9 -> 1008.6 | - | 24.8 -> -0.7 | 1.33 -> 1.29 | 55.2 -> 24.5 |
| mul | 256x256 | 11853.3 -> 11814.0 | - | 94.2 -> 26.0 | 1.51 -> 1.50 | 98.6 -> 27.0 |
| mul | 1000x1000 | 107142.3 -> 106691.9 | - | 289.0 -> -192.8 | 1.94 -> 1.93 | 374.3 -> -156.5 |
| mul | 2000x2000 | 302134.9 -> 301592.0 | - | 128.7 -> -681.6 | 2.04 -> 2.03 | 759.9 -> -99.5 |
| mul | 2000x100 | 46964.9 -> 46319.6 | - | 189.0 -> -476.9 | 1.35 -> 1.33 | 170.0 -> -377.5 |
| sqr | 64x64 | 626.4 -> 600.0 | - | 26.4 -> 1.6 | 0.93 -> 0.89 | 43.0 -> 19.3 |
| sqr | 512x512 | 24592.6 -> 24374.4 | - | 257.1 -> 26.4 | 1.25 -> 1.24 | 206.7 -> 17.9 |
| sqr | 2000x2000 | 224138.1 -> 223342.4 | - | 1273.4 -> 81.0 | 1.86 -> 1.85 | 1332.5 -> -404.5 |
| divrem | 128x64 | 3731.8 -> 3691.2 | - | 111.5 -> 47.5 | 2.10 -> 2.08 | 121.0 -> 89.4 |
| divrem | 512x256 | 28589.6 -> 28613.1 | - | 241.5 -> 234.6 | 1.74 -> 1.74 | 262.6 -> 236.7 |
| divrem | 2000x1000 | 232543.6 -> 232354.6 | - | 820.3 -> 397.1 | 1.73 -> 1.73 | 1102.5 -> 5.9 |
| divrem | 4000x2000 | 687816.7 -> 688042.4 | - | 1309.8 -> 565.7 | 2.00 -> 2.00 | -317.3 -> 1268.1 |
| divrem | 2000x100 | 95254.0 -> 94399.0 | - | 1331.8 -> 434.9 | 1.38 -> 1.37 | 1083.7 -> 362.2 |
| divrem | 65536x1 | 241855.9 -> 229283.2 | - | 12147.0 -> -1278.5 | 1.43 -> 1.34 | 15073.8 -> -605.0 |
| fromchars | 64x10 | 2195.2 -> 2182.2 | - | - | 1.37 -> 1.36 | 339.6 -> 326.3 |
| fromchars | 256x10 | 13831.3 -> 13791.9 | - | - | 1.31 -> 1.28 | 2271.7 -> 2254.6 |
| fromchars | 2000x10 | 300314.1 -> 300086.8 | - | - | 1.47 -> 1.48 | 17698.1 -> 17469.0 |

### 3.5 Large-band spot checks

x64:

| op | shape | auto ns | kernel ns | gmpz ns | auto/gmpz | change in auto |
|---|---|---:|---:|---:|---:|---:|
| add | 16384x16384 | 14883 -> 12435 | 9301 -> 9298 | 7410 | 2.01 -> 1.68 | -16.4% |
| divrem | 4096x2048 | 498654 -> 498767 | 497775 -> 498832 | 367635 | 1.36 -> 1.36 | +0.0% |
| fromchars | 10000x10 | 3670138 -> 3651386 | 3586031 -> 3567768 | 2006859 | 1.83 -> 1.82 | -0.5% |
| mul | 2000x2000 | 177038 -> 177290 | 176452 -> 176534 | 144170 | 1.23 -> 1.23 | +0.1% |
| mul | 16384x16384 | 3784524 -> 3774169 | 3790346 -> 3781385 | 1783926 | 2.12 -> 2.12 | -0.3% |
| tochars | 10000x10 | 5890002 -> 5879016 | 5823308 -> 5846315 | 4221163 | 1.40 -> 1.39 | -0.2% |

M4:

| op | shape | auto ns | kernel ns | gmpz ns | auto/gmpz | change in auto |
|---|---|---:|---:|---:|---:|---:|
| add | 16384x16384 | 9473 -> 7267 | 15838 -> 15813 | 4102 | 2.31 -> 1.77 | -23.3% |
| divrem | 4096x2048 | 703037 -> 708499 | 702306 -> 702139 | 358229 | 1.96 -> 1.98 | +0.8% |
| fromchars | 10000x10 | 4331417 -> 4171163 | 4095996 -> 4088150 | 1857816 | 2.33 -> 2.25 | -3.7% |
| mul | 2000x2000 | 302942 -> 301376 | 301403 -> 303536 | 148256 | 2.04 -> 2.03 | -0.5% |
| mul | 16384x16384 | 5400760 -> 5353474 | 5359177 -> 5393000 | 1567599 | 3.45 -> 3.42 | -0.9% |
| tochars | 10000x10 | 8247452 -> 8240938 | 8206028 -> 8216750 | 4180900 | 1.97 -> 1.97 | -0.1% |

Observations that matter for reading the tables:

- Small `auto` on x64 improved by 1.5-4.5 ns for add and sub, 2-7 ns for shifts, 9-14 ns for mul/sqr and 6-32 ns for divrem
  (2x1 to 32x16). On M4 the add/sub rows at 1-16 limbs are within noise of the baseline, apart from a small consistent
  loss at the smallest shapes (add 1x1 6.8 -> 7.7 ns, 2x2 14.6 -> 15.2; its allocator pair is cheap, a floor only 3-6 ns above
  the kernel, so there was little to remove); M4 shifts improved by 3-6 ns, mul/sqr by 6-15 ns and divrem by 7-47 ns.
- Medium and large `auto` rows move by their copy and zero-fill savings only: add 16384 -16% (x64) and -23% (M4), shifts at
  2000 limbs -16..-31% (x64) and -42..-74% (M4), mul and divrem above ~256 limbs within +-1.5% (divrem 65536x1 -10% on x64).
- **Regressions, all reproducible (4 of 4 runs):**
  - x64 `inplace` (`c += b; c -= b`) at >= 64 limbs is slower: add 64x64 39.1 -> 48.1 ns, 256x256 145.9 -> 196.1,
    1024x1024 583.0 -> 790.1 (+35%), 2000x2000 1134.9 -> 1520.5; sub the same; the 256x13 and 2000x13 shift pairs 93 -> 102 (+10%) and 657 -> 741 (+13%); the smallest `inplace` rows are 0.5-0.8 ns
    slower too (add 1x1 4.1 -> 4.9, sub 1x1 4.0 -> 4.7).
    `auto` improved but `inplace` did not: `add_n_tail` on x64 does not reach the ADX-assisted speed of
    `add_unsigned_spans` (the unchanged `kernel` row is 578 ns at 1024x1024, the new in-place pair is 790). On M4 the same
    rows improved by 2.3x (985 -> 434 ns at 1024x1024).
  - x64 `fromchars` (decimal) at 64 and 256 limbs-worth of digits: 64x10 2777 -> 3095 ns (+11%), 256x10 28412 -> 30826
    (+8.5%), 16x10 420 -> 434 (+3%); 4x10 improved 195 -> 116. The `kernel` row moves the same way (256x10 25.7k ->
    29.6k), 2000x10 and the M4 are unchanged. With `mul_header_basecase_enabled = false` the x64 rows return to the
    baseline (64x10 2800 / 2870 ns base / shortcut-off, 3050 with the shortcut), while direct mul shapes (40x2 .. 133x31)
    show no slowdown. `digits_to_limbs` is a header template compiled into the harness TU (a perf profile is 99.9% in that
    one function), so this looks like an inlining/code-generation interaction in GCC 14 rather than an algorithmic change; it
    was not isolated further.

## 4. Plan targets

Targets from the plan, with the achieved values (median of the four runs). Kernel-row noise at <= 16 limbs is 1-2 ns
(section 8), so a miss of less than ~1.5 ns on an `inplace - kernel` target is within noise.

| target | x64 | M4 |
|---|---|---|
| `inplace - kernel` <= 3 ns for add/sub/shl/shr at 2-16 limbs | **partly met**: add 3.7/2.6/2.5/4.3 (2/4/8/16 limbs, before 3.5/2.5/2.8/4.2), sub 3.0/2.8/2.6/3.2, shl 1.1/1.7, shr 1.1/-0.2. The in-place path was already near the kernel at these sizes; essentially unchanged | **met**: add 2.5/1.6/0.6/-2.0, sub 2.1/1.7/0.6/-2.5, shifts 0.0..1.2 |
| `auto - floor` <= 3 ns at <= 16 limbs, add/sub/shl/shr | **not met except shl**: add 5.6/4.3/4.2/5.3 (before 7.6/7.2/6.9/9.8), sub 5.8/5.8/5.6/5.7, shl 2.3/1.9 (met), shr 5.0/4.4 | **mostly not met**: add 5.6/4.1/3.1/3.2, sub 5.3/4.3/3.8/2.4, shl -0.1/3.9, shr 2.0/5.4 |
| `auto - floor` <= 5 ns at <= 16 limbs, mul/sqr | **met** for mul 2.2-4.1 (before 12.6-17.0); sqr 5.4 (4x4, marginal) and 3.7 (16x16) | **met** for mul 2.8-4.8 (before 9.2-16.3); sqr 5.2 (4x4, marginal) and 2.9 |
| x64 add 1000 `auto`: 840 -> <= 650 ns | **not met**: add 1024x1024 841 -> 800 ns (2000x2000 1721 -> 1535) | n/a (M4: 552 -> 450 ns) |
| divrem 8x4 `auto - kernel`: 66 -> <= 2 floors + 5 ns (i.e. `auto - floor` <= 5) | **not met**: `auto - kernel` 67.7 -> 43.8, `auto - floor` 53.0 -> 27.1 | **not met**: `auto - kernel` 50.1 -> 29.0, `auto - floor` 35.5 -> 14.9 |
| medium and large rows: no regression > 3% | **not met**: `inplace` add/sub +23..+35% at >= 64 limbs, shifts +13%, decimal `fromchars` +8..+11% at 64-256 limbs-worth of digits (section 3); `auto` rows and the large spot checks are within 3% | **met for medium and large rows**: none regresses; in the small band add 1x1 and 2x2 `auto` are 0.4-0.9 ns slower (+4%, +13%) |

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

`new64` (shortcut on) against a build of the same tree with `mul_header_basecase_enabled = false` (edited in a scratch copy,
not committed), interleaved, three runs each, `auto` row:

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
the 1 ns threshold. On M4 it saves 0.1-1.1 ns on mul 3..16 (in the noise of the runs; 2x2 is 0.5 ns slower with it) and 1.7-1.9 ns on sqr 4 and 16, so
it is at best marginal there. The x64 `fromchars` regression in section 3 appears only with the shortcut compiled in. The
trade-off is therefore: -4..5 ns on small x64 mul/sqr (13-20% of those rows) against +8-11% on decimal `fromchars` at
64-256 limbs-worth of digits on x64 with GCC 14.

## 7. Inline-capacity study

The same front-end code with the inline capacity of the integer type set to N = 64 (`big_int`), 128, 256 and 512 bits.
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
- The x64 numbers are GCC 14 with `-march=native`; the decimal `fromchars` regression (section 3) was seen only there.
- The 2-run study and the 3-run shortcut comparison are small samples: treat differences below ~1 ns as noise.

## 9. Correctness matrix

- M4 (appleclang): `appleclang-debug` workflow (MaxSan) 1264/1264, `appleclang-release` 1296/1296,
  `appleclang-release-namespace` 1282/1282 (ctest counts include skipped benches).
- x64 box: `llvm-debug` (MaxSan) 1341/1341 and `llvm-release` (clang-23, `-march=native`) 1341/1341. GCC (g++-14, g++-13,
  IFMA=OFF g++-14, and g++-14 debug without NDEBUG): every target builds except the two test targets
  `front_end_add_sub` and `front_end_shift`, which fail `-Werror=padded` at
  `tests/beman/big_int/front_end_reference.hpp:44` (`std::vector<limb> mag;` in struct `ref`). With
  `-Wno-error=padded` both pass on gcc-14 release and debug (15 and 12 tests); all other GCC tests pass (1296/1298 with the
  two targets not built).
- Docker linux/arm64 (GCC 16, `ctest -E float_construction`): `gcc-release` and `gcc-debug` 1225/1227 passed, the same two
  `front_end_*` targets failing to build on `-Werror=padded`.
- 32-bit limbs (`llvm-release` with `-DBEMAN_BIG_INT_FORCED_LIMB_WIDTH=32`, x64): `inline_tail`, `span_primitives`,
  `front_end_shift`, `dispatch_contract` and `alloc_count` compile and pass. `front_end_add_sub`
  (`*.IntegerOperands`, 3 tests) and `muldiv_frontend` (`CompoundWithIntegers`, aborts on a division by zero reference)
  fail at 32-bit limbs because the tests build the reference operand from a 64-bit `v` truncated to one 32-bit limb
  (`front_end_add_sub.test.cpp:93`, `muldiv_frontend.test.cpp:395`); `Allocation.MoveAssignStealsHeapSrcEvenWhenDstLarger`
  replaces the baseline's equally failing `MoveAssignReusesDstStorageWhenLarger` (it assumes 64-bit limbs). Every other
  32-bit failure (bitwise, increment_decrement, karatsuba, pmr, wide_ops fail to compile; CompoundSubtraction, BitShift,
  Addition and IntegerAssignment cases) is in the cf1cf54 tree as well.
- GCC with 32-bit limbs: `basic_big_int.hpp:643` (`shift_max` narrowing; line 636 at cf1cf54), `base_conversion.hpp:144`
  (`-Wpadded`) and `base_tables.hpp:66` (`-Wconversion`) are errors at cf1cf54 too.

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
# tables: python3 -I tools/item1_tables.py item1 x64   (or: mac) [small medium spots shortcut study]
# allocation counts: cmake --build <dir> --target beman.big_int.tests.alloc_count; run the binary and read the
#   "[alloc_count]" lines
```

The shortcut comparison builds a copy of the tree with `mul_header_basecase_enabled = false` in
`include/beman/big_int/detail/mul_impl.hpp` and otherwise identical flags.
