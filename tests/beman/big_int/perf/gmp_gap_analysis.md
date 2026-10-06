<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
SPDX-License-Identifier: BSL-1.0
-->

# GMP gap analysis: where `big_int` must get faster to match GMP

Analysis only: no library code was changed. Method: `shape_sweep` (extended for this study, see `shape_sweep.cpp`)
times, per op and shape, the public API into a reused destination (`auto`), compound assignment (`inplace`), the
library's span kernels on preallocated buffers (`kernel`), GMP `mpn` on preallocated buffers (`gmp`) and GMP `mpz` with
destination reuse (`gmpz`). **auto/gmpz** is the gap a user sees; **kernel/gmp** is the algorithm-and-codegen gap;
**auto - kernel** (ns) is the front-end cost. A ratio above 1 means `big_int` is slower. All ratios are like-for-like
medians from one process per shape; absolute cycle counts are harness numbers (see Caveats).

## 1. Summary

### 1.1 Heat table: auto/gmpz, geometric mean over the shapes of a band (worst shape in parentheses)

Bands: small <= 16 limbs, medium <= 2000 (divrem: <= 4000), large above. x64 = i9-11900K, g++-14 `-march=native`
(BMI2/ADX + AVX-512 IFMA kernels, SIMD_MUL), two seeds averaged; M4 = Apple M4 Max, AppleClang, SIMD_MUL, two seeds.

| op | band | x64 gcc14 auto/gmpz (geomean, worst) | M4 auto/gmpz (geomean, worst) |
|---|---|---|---|
| add | small | 3.60 (4.42) | 3.21 (3.81) |
| add | medium | 2.52 (3.80) | 2.04 (4.20) |
| add | large | 1.90 (2.15) | 2.20 (2.26) |
| sub | small | 3.51 (4.15) | 4.44 (5.66) |
| sub | medium | 2.53 (3.85) | 1.91 (3.00) |
| sub | large | 2.00 (2.19) | 2.20 (2.28) |
| shl | small | 4.59 (4.91) | 6.55 (8.91) |
| shl | medium | 2.90 (3.14) | 1.33 (2.70) |
| shl | large | 3.00 (3.06) | 2.51 (2.61) |
| shr | small | 3.84 (5.07) | 5.44 (6.80) |
| shr | medium | 2.88 (3.19) | 2.61 (3.64) |
| shr | large | 2.77 (2.94) | 2.62 (2.69) |
| cmp | small | 0.58 (0.62) | 1.07 (1.33) |
| cmp | medium | 0.74 (0.74) | 1.03 (1.05) |
| cmp | large | 0.70 (0.70) | 1.02 (1.02) |
| mul | small | 2.39 (5.01) | 2.50 (5.56) |
| mul | medium | 0.80 (1.24) | 1.54 (2.02) |
| mul | large | 2.33 (3.04) | 2.31 (3.44) |
| sqr | small | 2.42 (3.83) | 2.03 (3.10) |
| sqr | medium | 0.72 (0.99) | 1.39 (1.88) |
| sqr | large | 2.96 (3.63) | 2.42 (3.76) |
| divrem | small | 2.94 (3.46) | 2.73 (3.41) |
| divrem | medium | 1.71 (2.00) | 1.82 (2.31) |
| divrem | large | 2.44 (4.69) | 2.95 (5.52) |
| tochars | small | 0.93 (1.97) | 0.63 (1.37) |
| tochars | medium | 1.36 (1.90) | 1.23 (1.66) |
| tochars | large | 1.58 (2.77) | 2.01 (3.58) |
| fromchars | small | 2.15 (2.84) | 1.20 (1.20) |
| fromchars | medium | 2.39 (3.01) | 1.37 (1.47) |
| fromchars | large | 2.33 (2.80) | 2.97 (3.43) |
| gcd | small | 1.90 (3.65) | 1.79 (3.32) |
| gcd | medium | 2.06 (2.72) | 2.41 (3.90) |
| gcd | large | 15.01 (24.56) | 20.04 (32.80) |

Reading it: `big_int` already beats GMP on x64 for mul/sqr at 32-512 limbs (IFMA kernels) and for cmp everywhere, and
is close for conversions below ~2000 limbs. It loses by 3-5x on small add/sub/shift (fixed per-call cost), 2-3x on
medium and large linear ops, 2-3x on mul/sqr above ~16k limbs, 3-5x on large divrem, and by an unbounded factor on large
gcd (quadratic algorithm).

### 1.2 Direct answers

**Do add/sub need assembly?** Not on x64, and not first anywhere. On x64 `add_n` in the library is 1.5x GMP in
cycles/limb with g++-14 (1.6x clang-23, 2.3x g++-13, whose carry falls back to two `__builtin_add_overflow` and an `||`),
but a 4x-unrolled `__builtin_addcll` loop in plain C++ reaches 1.12x GMP at 64 limbs (1.16-1.28x at 512 limbs) and an
inline-asm `adc` x4 loop reaches 1.09x (1.02-1.03x at 512): asm buys ~4-7% over the unrolled builtins at 64 limbs and
~12-21% at 512 limbs, which is closer to the span lengths of the Toom/Karatsuba linear passes in a 16k-limb mul. The same unroll closes ~75% of the gap on the M4 (3.9x -> 1.7x GMP). So the
order is: restructure the C++ kernels (unrolled carry chain, one carry materialization per 4 limbs), re-measure at 512+
limbs, and only then decide on asm: x64 `adc` asm for the remaining 12-21% and AArch64 `adcs`/`sbcs` asm for the
remaining ~1.7x on M4. At 64-2000 limbs the plan's criterion is met
(kernel/gmp 1.5-2.4x x64, 3.8-4.3x M4, and linear helpers are 24-96% of the profiled mul/div/conversion/gcd time), and the
"unrolled builtins close the gap" clause says to do those first.

**Are `span_ops` a slowdown?** Yes, wherever linear passes dominate, and the cause is codegen and structure, not the
algorithm:
- `kernel/gmp` for add/sub is 1.5-2.4x (x64) and 3.8-4.3x (M4) at 64-2000 limbs; shl/shr 2.0-2.5x on x64 with gcc (clang
  vectorizes `shift_left_n` and beats GMP, 0.69x); `submul_single_limb` 1.5x (x64) / 3.1x (M4); `multiply_single_limb`
  2.2x / 1.9x; `divide_unsigned_short` by 3 is 5.2x GMP's `divexact_by3c`.
- Inclusive `span_ops` time (including the carry primitives of `wide_ops` beneath it) is 46% of mul 16384x16384, 96% of
  divrem 256x128, 51% of divrem 4096x2048, 54% of tochars/fromchars 10000 limbs and ~80% of gcd 1024.
- If those helpers ran 1.5-2x faster, projected ratios move: mul 16384 2.14 -> 1.64-1.81, divrem 4096x2048 1.37 -> 1.02-1.14,
  tochars 10000 1.39 -> 1.03-1.15, gcd 1024 2.20 -> 1.32-1.61, divrem 131072x65536 3.19 -> 2.4-2.7 (table in 4.2).
  Large mul (FFT) and large divrem do not close by this alone.
Where they are not the problem: mul 64x64 (asm kernel is 93% of the time and beats GMP by 1.7x), mul 262144 (FFT), cmp.

### 1.3 Top-5 actions (full ranked list with evidence in section 6)

1. **Cut the fixed per-call front-end cost** (S-M, both archs). `auto - kernel` is 13-18 ns for add/sub/shift and ~21 ns
   for mul at <= 16 limbs: 3.6-4.6x GMP on add/sub/shl small shapes; 46% member/`construct_at`, 43% allocation at add 16x16.
2. **Rewrite the linear span kernels in C++** as 4x unrolled carry chains (S-M, both archs; fixes the g++-13 fallback
   and makes gcc shifts vectorize like clang). Closes ~75% of add/sub on M4, 1.5-1.7x -> 1.1-1.3x on x64 (64-512 limbs); worth 15-23% of mul 16384
   and most of the divrem 4096x2048 / tochars 10000 / gcd 1024 gaps.
3. **HGCD (or at least a subquadratic gcd) above ~2000 limbs** (L). Ratio grows 2.2 (1k) -> 9.2 (16k) -> 24.6 (65k) on
   x64, 2.4 -> 12.2 -> 32.7 on M4; no linear-kernel work helps beyond 1k limbs.
4. **Large-multiplication path: NTT speed and the Toom/FFT crossover** (L). mul/sqr/divrem above 16k limbs are 2-3.6x
   (x64) with 65% `fft_ntt` + 26% `wide_ops` at 262144 limbs; large divrem/tochars/fromchars inherit it (88% of
   divrem 131072x65536 is inside multiply).
5. **Re-tune cutoffs after item 2 and use exact division in Toom** (M). Our asm basecase alone (1.98 ms) exceeds GMP's
   whole mul 16384 (1.77 ms); the divexact-by-3/5 steps use a general short division 5x slower than GMP's.
   Asm for AArch64 add/sub/mul_1/submul_1 is item 6, gated on item 2's result.

## 2. Setup

| | x64 | M4 |
|---|---|---|
| CPU | Intel i9-11900K (Rocket Lake, AVX-512 IFMA), pinned `taskset -c 2`, turbo 5.26-5.29 GHz measured | Apple M4 Max, P-core assumed 4.47 GHz (not pinned) |
| Compilers | g++ 14.3.0 (primary), clang 23.0.0, g++ 13.4.0 | AppleClang 21.0.0 |
| Flags | `-march=native`, effective **-O2** (see below), RelWithDebInfo, NDEBUG | effective -O2, RelWithDebInfo |
| big_int config | BMI2/ADX=1, AVX512_IFMA=1, SIMD_MUL=1 (noIFMA run: IFMA=0) | SIMD_MUL=1, NEON |
| GMP | 6.3.0 built in `~/tools/gmp` for rocketlake (coreibwl mulx/adx mul_basecase/addmul_1, coreihwl aors_n), -O2 | 6.3.0 Homebrew, `libgmp.a` |
| Seeds / rounds | seed 1 and 2 (gcc14, M4); seed 1 (others); 9 rounds x 20 ms, large band 3 rounds, gcd 65536 1 round | same |

- **-O2 finding:** the release presets look like -O3 (`-march=native -O3 -O2 -g -DNDEBUG` in the `#const cxx_flags`)
  but CMake's `CMAKE_CXX_FLAGS_RELWITHDEBINFO` (`-O2 -g -DNDEBUG`) comes last, so all builds here (and users of the presets)
  run at -O2. The probe shows -O3 leaves the linear-kernel c/l unchanged within 5% (add_n, shifts, submul_1, mul_1, all three compilers); the sweeps were not re-run at -O3.
- **Code state:** `git rev-parse HEAD` = `22ef17b12246e1ebed3571208589e7785ec388ea` (branch `gmp-gap-analysis`). The only
  change is the uncommitted extension of `tests/beman/big_int/perf/shape_sweep.cpp`.
- **Tuning tables:** the IFMA build uses karatsuba 260, toom3 1600, toom4 4000, fft_mul_min 16000, Burnikel-Ziegler 160.
  The IFMA=OFF build also switches to the BMI2 table (karatsuba 47, toom3 400, toom4 1600, fft 6000), so the noIFMA column
  changes kernels **and** cutoffs. M4 uses karatsuba 112, toom3 300, toom4 1400, fft 3300, BZ 40.
- **`#const` (abbreviated):** x64 gcc14 `ARCH=x86_64 BMI2_ADX=1 AVX512_IFMA=1 SIMD_MUL=1 limb_bits=64 ndebug=1
  isa_avx512ifma=1 compiler=gcc-14.3.0 build_type=RelWithDebInfo`; M4 `ARCH=aarch64 SIMD_MUL=1 limb_bits=64
  compiler=appleclang-21.0.0`. The full line is the third line of every CSV.
- **Shape conventions:** add/sub/cmp/gcd `AxB` limbs (cmp operands equal except limb 0; gcd with b odd); shl/shr `NxS` =
  N limbs by S bits (13, plus 77 for the whole-limb case); tochars/fromchars `NxBASE` (10; base 16 for tochars); divrem
  `2N x N`, plus 2000x100 and 65536x1. Kernel shift rows include a copy-in (in-place kernels); gcd `kernel` is
  `gcd_unsigned_spans` alone (no Euclidean pre-steps), valid for square shapes only.
- **Data files** (this directory): `gmp_gap_x64_gcc14.csv` (seed 1), `gmp_gap_x64_clang23.csv`,
  `gmp_gap_x64_gcc13_linear.csv` (add, sub, shl, shr, cmp), `gmp_gap_x64_gcc14_noifma.csv` (mul, sqr), `gmp_gap_m4.csv`
  (seed 1). The tables below average both seeds where two exist. Seed-to-seed spread: 35 of 518 x64 rows and 51 of 518 M4
  rows differ by more than 5% (mostly sub-20 ns rows and 1M-limb DRAM-bound rows; they also see different random operands).

## 3. Ratio tables

Columns: x64 g++-14 `auto/gmpz`, `kernel/gmp`, `inplace/gmpz` (per op, already halved), `auto - kernel` in ns; then
clang-23 `auto/gmpz` and `kernel/gmp`; g++-13 `kernel/gmp` (linear ops only); noIFMA `auto/gmpz` (mul, sqr only); then M4
`auto/gmpz`, `kernel/gmp`, `inplace/gmpz`, `auto - kernel` ns. "-" = row not measured or not applicable. mul/sqr/divrem
`kernel` is the runtime tier ladder, so `auto - kernel` there is dispatcher overhead (negative values: the auto path
slices/shapes better than the `unsliced` kernel row, or noise).

#### add

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x1 | 1.97 | 0.91 | 0.93 | 6 | 1.90 | 0.83 | 0.92 | - | 2.22 | 0.77 | 1.14 | 5 |
| 2x2 | 4.42 | 1.08 | 1.28 | 16 | 3.97 | 1.05 | 1.12 | - | 3.47 | 0.72 | 0.94 | 13 |
| 4x4 | 4.29 | 1.26 | 1.22 | 15 | 3.83 | 1.21 | 1.37 | - | 3.79 | 1.09 | 0.95 | 13 |
| 8x8 | 4.17 | 1.24 | 1.52 | 15 | 3.76 | 1.22 | 1.51 | - | 3.05 | 1.57 | 1.02 | 12 |
| 16x16 | 3.91 | 1.37 | 1.73 | 18 | 3.50 | 1.55 | 2.11 | - | 3.81 | 2.30 | 2.28 | 9 |
| 64x64 | 3.80 | 2.18 | 2.38 | 29 | 2.40 | 2.59 | 3.11 | - | 4.20 | 4.25 | 3.90 | 7 |
| 256x256 | 2.80 | 1.82 | 1.83 | 81 | 1.81 | 2.14 | 2.47 | - | 2.51 | 4.00 | 4.04 | -82 |
| 1024x1024 | 2.14 | 1.48 | 1.49 | 261 | 1.28 | 1.73 | 1.99 | - | 2.17 | 3.79 | 3.83 | -394 |
| 2000x2000 | 3.69 | 2.43 | 2.41 | 605 | 2.21 | 2.78 | 3.28 | - | 2.27 | 3.76 | 3.83 | -715 |
| 1024x1 | 1.20 | 2.49 | 2.48 | -298 | 0.66 | 1.25 | 2.48 | - | 0.67 | 2.70 | 3.58 | -533 |
| 16384x16384 | 1.90 | 1.23 | 1.19 | 5531 | 1.36 | 1.44 | 1.65 | - | 2.26 | 3.72 | 3.88 | -6002 |
| 131072x131072 | 2.15 | 1.24 | 1.25 | 53610 | 1.41 | 1.46 | 1.69 | - | 2.19 | 3.73 | 3.78 | -49938 |
| 1048576x1048576 | 1.68 | 1.20 | 0.93 | 338532 | 1.35 | 1.19 | 1.31 | - | 2.14 | 3.76 | 3.86 | -430539 |

#### sub

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x1 | 2.14 | 0.67 | 0.85 | 8 | 1.91 | 0.67 | 0.75 | - | 2.91 | 0.71 | 1.34 | 6 |
| 2x2 | 4.15 | 0.92 | 1.01 | 16 | 3.68 | 0.97 | 1.00 | - | 4.98 | 0.74 | 1.08 | 14 |
| 4x4 | 4.06 | 1.14 | 1.14 | 16 | 3.53 | 1.09 | 1.23 | - | 5.66 | 1.10 | 1.37 | 14 |
| 8x8 | 4.05 | 1.18 | 1.38 | 16 | 3.49 | 1.04 | 1.58 | - | 4.91 | 1.58 | 1.57 | 13 |
| 16x16 | 3.67 | 1.41 | 1.52 | 17 | 3.34 | 1.20 | 2.13 | - | 4.29 | 2.38 | 2.31 | 10 |
| 64x64 | 3.85 | 2.18 | 2.26 | 31 | 2.32 | 1.57 | 3.10 | - | 3.00 | 4.39 | 3.68 | -7 |
| 256x256 | 2.88 | 1.83 | 1.82 | 87 | 1.77 | 1.50 | 2.47 | - | 2.28 | 4.03 | 4.02 | -97 |
| 1024x1024 | 2.17 | 1.48 | 1.49 | 270 | 1.27 | 1.23 | 1.99 | - | 2.16 | 3.77 | 3.83 | -400 |
| 2000x2000 | 3.58 | 2.38 | 2.33 | 610 | 2.24 | 1.98 | 3.18 | - | 2.23 | 3.74 | 3.81 | -741 |
| 1024x1 | 1.21 | 2.47 | 2.48 | -293 | 0.41 | 1.45 | 2.46 | - | 0.77 | 2.78 | 3.66 | -519 |
| 16384x16384 | 2.16 | 1.26 | 1.34 | 5732 | 1.38 | 1.12 | 1.78 | - | 2.28 | 3.76 | 3.82 | -6215 |
| 131072x131072 | 2.19 | 1.27 | 1.27 | 53529 | 1.40 | 1.04 | 1.68 | - | 2.19 | 3.78 | 3.89 | -51958 |
| 1048576x1048576 | 1.69 | 1.10 | 0.92 | 417562 | 1.36 | 1.00 | 1.30 | - | 2.14 | 3.82 | 3.83 | -439473 |

#### shl

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x13 | 4.91 | 1.00 | 1.20 | 15 | 5.35 | 1.00 | 0.95 | - | 8.91 | 2.00 | 1.15 | 16 |
| 4x13 | 4.66 | 1.37 | 1.30 | 15 | 5.82 | 1.52 | 1.52 | - | 7.58 | 1.88 | 1.32 | 17 |
| 16x13 | 4.22 | 1.81 | 1.60 | 17 | 4.47 | 0.92 | 2.02 | - | 4.16 | 1.08 | 1.10 | 18 |
| 256x13 | 2.87 | 1.98 | 1.85 | 51 | 1.48 | 0.53 | 2.31 | - | 2.30 | 1.32 | 0.97 | 39 |
| 2000x13 | 2.71 | 2.14 | 1.94 | 200 | 1.44 | 0.46 | 2.05 | - | 0.38 | 0.27 | 0.17 | 246 |
| 2000x77 | 3.14 | 2.53 | 2.26 | 213 | 1.82 | 0.86 | 2.43 | - | 2.70 | 1.91 | 1.46 | 232 |
| 131072x13 | 2.95 | 2.08 | 1.29 | 29476 | 2.14 | 1.28 | 2.02 | - | 2.42 | 1.49 | 0.98 | 19425 |
| 1048576x13 | 3.06 | 1.94 | 0.94 | 415797 | 2.71 | 1.69 | 1.91 | - | 2.61 | 1.50 | 1.05 | 173676 |

#### shr

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x13 | 3.21 | 0.90 | 1.14 | 9 | 2.93 | 0.91 | 0.90 | - | 4.74 | 2.52 | 1.12 | 7 |
| 4x13 | 5.07 | 1.48 | 1.19 | 17 | 4.94 | 1.45 | 1.49 | - | 6.80 | 2.27 | 1.19 | 15 |
| 16x13 | 3.49 | 2.02 | 1.22 | 13 | 4.66 | 4.17 | 2.75 | - | 4.99 | 1.28 | 1.19 | 17 |
| 256x13 | 2.67 | 2.31 | 1.84 | 25 | 1.29 | 1.01 | 2.29 | - | 2.27 | 1.80 | 0.94 | 20 |
| 2000x13 | 2.80 | 2.39 | 1.94 | 136 | 1.18 | 1.02 | 2.42 | - | 2.16 | 1.66 | 0.94 | 142 |
| 2000x77 | 3.19 | 2.46 | 2.26 | 255 | 1.13 | 1.08 | 2.46 | - | 3.64 | 1.77 | 1.45 | 536 |
| 131072x13 | 2.61 | 1.96 | 1.15 | 22856 | 1.92 | 1.23 | 1.99 | - | 2.55 | 1.91 | 1.00 | 12337 |
| 1048576x13 | 2.94 | 2.11 | 0.92 | 322697 | 2.62 | 1.72 | 2.09 | - | 2.69 | 1.94 | 1.06 | 110119 |

#### cmp

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x1 | 0.61 | 0.71 | - | 0 | 0.85 | 0.73 | 0.74 | - | 1.33 | 1.19 | - | 0 |
| 4x4 | 0.50 | 0.58 | - | 0 | 0.62 | 0.57 | 0.51 | - | 0.92 | 1.09 | - | 1 |
| 16x16 | 0.62 | 1.06 | - | -1 | 0.66 | 1.36 | 1.35 | - | 0.99 | 1.02 | - | 0 |
| 256x256 | 0.73 | 1.02 | - | 4 | 1.05 | 1.52 | 1.02 | - | 1.02 | 1.10 | - | 1 |
| 2000x2000 | 0.74 | 0.99 | - | 60 | 1.01 | 1.56 | 0.99 | - | 1.05 | 1.21 | - | 20 |
| 131072x131072 | 0.70 | 1.06 | - | -3092 | 0.79 | 1.10 | 0.99 | - | 1.02 | 1.18 | - | -6049 |

#### mul

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x1 | 5.01 | - | - | - | 3.44 | - | - | 4.76 | 5.56 | - | - | - |
| 2x2 | 4.68 | 1.96 | - | 21 | 4.22 | 1.71 | - | 4.57 | 4.29 | 1.40 | - | 22 |
| 3x3 | 3.03 | 1.13 | - | 21 | 2.77 | 1.01 | - | 2.97 | 2.79 | 1.32 | - | 21 |
| 4x4 | 2.65 | 1.14 | - | 21 | 2.47 | 0.96 | - | 2.65 | 2.86 | 1.22 | - | 18 |
| 6x6 | 2.13 | 1.05 | - | 21 | 2.34 | 1.00 | - | 2.16 | 1.84 | 1.26 | - | 14 |
| 8x8 | 1.59 | 0.93 | - | 21 | 1.52 | 0.85 | - | 1.58 | 1.68 | 0.93 | - | 18 |
| 12x12 | 1.39 | 0.97 | - | 23 | 1.33 | 0.93 | - | 1.31 | 1.34 | 0.95 | - | 20 |
| 16x16 | 1.20 | 0.96 | - | 22 | 1.19 | 0.93 | - | 1.20 | 1.95 | 0.92 | - | 84 |
| 32x32 | 0.80 | 0.73 | - | 21 | 0.77 | 0.68 | - | 1.26 | 1.14 | 0.98 | - | 42 |
| 64x64 | 0.60 | 0.56 | - | 38 | 0.59 | 0.56 | - | 1.39 | 1.32 | 1.27 | - | 45 |
| 128x128 | 0.65 | 0.63 | - | 45 | 0.64 | 0.60 | - | 1.44 | 1.32 | 1.29 | - | 68 |
| 256x256 | 0.81 | 0.80 | - | 64 | 0.80 | 0.80 | - | 1.60 | 1.50 | 1.51 | - | -104 |
| 512x512 | 0.93 | 0.92 | - | 199 | 0.91 | 0.91 | - | 1.83 | 1.95 | 1.94 | - | 295 |
| 1000x1000 | 1.13 | 1.13 | - | -22 | 1.11 | 1.10 | - | 2.06 | 1.96 | 1.93 | - | 447 |
| 2000x2000 | 1.24 | 1.23 | - | 899 | 1.21 | 1.20 | - | 2.05 | 2.02 | 2.03 | - | -733 |
| 2000x100 | 0.50 | 0.49 | - | 188 | 0.50 | 0.50 | - | 1.43 | 1.35 | 1.30 | - | 777 |
| 4096x4096 | 1.34 | 1.32 | - | 6352 | 1.34 | 1.33 | - | 2.21 | 2.64 | 2.58 | - | 3341 |
| 16384x16384 | 2.13 | 2.11 | - | 3150 | 2.13 | 2.13 | - | 4.29 | 3.44 | 3.36 | - | -17370 |
| 65536x65536 | 2.85 | 2.78 | - | -56190 | 2.82 | 2.81 | - | 3.71 | 2.51 | 2.51 | - | -36334 |
| 262144x262144 | 2.79 | 2.72 | - | 2716664 | 2.13 | 2.09 | - | 2.79 | 1.94 | 1.69 | - | 926500 |
| 1048576x1048576 | 3.04 | 3.03 | - | 1543947 | 2.53 | 2.55 | - | 3.04 | 1.29 | 1.32 | - | 457750 |
| 262144x8192 | 2.32 | 3.06 | - | -15235319 | 2.25 | 3.03 | - | 3.45 | 2.68 | 2.71 | - | -881146 |

#### sqr

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4x4 | 3.83 | 1.66 | - | 24 | 3.48 | 1.50 | - | 3.73 | 3.10 | 1.56 | - | 19 |
| 16x16 | 1.53 | 1.12 | - | 23 | 1.48 | 1.23 | - | 1.65 | 1.33 | 0.96 | - | 27 |
| 64x64 | 0.54 | 0.50 | - | 22 | 0.53 | 0.50 | - | 1.31 | 0.95 | 0.86 | - | 49 |
| 512x512 | 0.70 | 0.70 | - | 112 | 0.69 | 0.69 | - | 1.72 | 1.50 | 1.34 | - | 3 |
| 2000x2000 | 0.99 | 0.98 | - | 1396 | 0.99 | 0.98 | - | 2.06 | 1.88 | 1.85 | - | 3488 |
| 16384x16384 | 2.11 | 2.09 | - | 24542 | 2.09 | 2.07 | - | 6.25 | 3.76 | 3.78 | - | 7675 |
| 262144x262144 | 3.40 | 3.40 | - | 117972 | 3.39 | 3.38 | - | 3.77 | 2.42 | 2.43 | - | 706730 |
| 1048576x1048576 | 3.63 | 3.62 | - | 1857343 | 2.83 | 2.84 | - | 3.62 | 1.56 | 1.55 | - | -828896 |

#### divrem

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2x1 | 3.46 | 0.76 | - | 37 | 2.34 | 0.66 | - | - | 3.41 | 1.17 | - | 24 |
| 8x4 | 2.50 | 1.05 | - | 66 | 2.39 | 1.20 | - | - | 2.19 | 1.13 | - | 49 |
| 32x16 | 1.90 | 1.51 | - | 80 | 1.71 | 1.48 | - | - | 2.31 | 1.90 | - | 88 |
| 128x64 | 1.94 | 1.86 | - | 147 | 1.89 | 1.84 | - | - | 2.11 | 1.98 | - | 194 |
| 512x256 | 1.80 | 1.77 | - | 367 | 1.76 | 1.75 | - | - | 1.73 | 1.70 | - | 112 |
| 2000x1000 | 1.33 | 1.32 | - | 1362 | 1.27 | 1.27 | - | - | 1.64 | 1.71 | - | 2808 |
| 4000x2000 | 1.41 | 1.40 | - | 2352 | 1.39 | 1.38 | - | - | 1.93 | 2.00 | - | 3428 |
| 2000x100 | 2.00 | 1.96 | - | 2749 | 1.98 | 1.96 | - | - | 1.39 | 1.38 | - | 671 |
| 65536x1 | 1.32 | 1.13 | - | 27989 | 1.57 | 1.48 | - | - | 1.42 | 1.34 | - | 14130 |
| 16384x8192 | 1.82 | 1.81 | - | 8877 | 1.82 | 1.79 | - | - | 3.03 | 3.20 | - | -55484 |
| 131072x65536 | 3.19 | 3.19 | - | -37358 | 3.17 | 3.15 | - | - | 5.52 | 5.54 | - | -1140708 |
| 1048576x524288 | 4.69 | 4.70 | - | -3279742 | 3.57 | 3.56 | - | - | 3.17 | 3.01 | - | 3286834 |

#### tochars

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1x10 | 0.43 | 2.97 | - | -48 | 0.42 | 2.76 | - | - | 0.25 | 1.99 | - | -27 |
| 4x10 | 1.35 | 2.53 | - | -51 | 1.23 | 2.37 | - | - | 0.86 | 1.36 | - | -18 |
| 16x10 | 1.97 | 2.05 | - | 127 | 2.08 | 2.13 | - | - | 1.37 | 1.45 | - | 97 |
| 64x10 | 1.90 | 1.98 | - | 368 | 1.89 | 1.96 | - | - | 1.49 | 1.56 | - | 307 |
| 256x10 | 1.83 | 1.87 | - | 1291 | 1.79 | 1.83 | - | - | 1.66 | 1.66 | - | 1461 |
| 2000x10 | 1.49 | 1.46 | - | 22512 | 1.46 | 1.44 | - | - | 1.62 | 1.62 | - | 4983 |
| 16384x10 | 1.52 | 1.52 | - | 87912 | 1.48 | 1.49 | - | - | 2.35 | 2.36 | - | 134176 |
| 131072x10 | 2.20 | 2.22 | - | 750279 | 2.15 | 2.18 | - | - | 3.41 | 3.42 | - | 1418730 |
| 262144x10 | 2.77 | 2.77 | - | 3817818 | 2.56 | 2.56 | - | - | 3.58 | 3.61 | - | 1700062 |
| 16x16 | 0.66 | - | - | - | 0.70 | - | - | - | 0.53 | - | - | - |
| 2000x16 | 0.67 | - | - | - | 0.79 | - | - | - | 0.57 | - | - | - |
| 131072x16 | 0.66 | - | - | - | 1.03 | - | - | - | 0.57 | - | - | - |

#### fromchars

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4x10 | 2.84 | 1.29 | - | 152 | 2.62 | 1.07 | - | - | 1.20 | 1.05 | - | 59 |
| 16x10 | 1.63 | 1.74 | - | 201 | 1.38 | 1.17 | - | - | 1.19 | 1.33 | - | 188 |
| 64x10 | 2.13 | 2.80 | - | 454 | 1.31 | 1.25 | - | - | 1.35 | 1.61 | - | 626 |
| 256x10 | 3.01 | 3.51 | - | 1839 | 1.60 | 1.66 | - | - | 1.30 | 1.40 | - | 2243 |
| 2000x10 | 2.14 | 2.20 | - | 20787 | 1.53 | 1.54 | - | - | 1.47 | 1.54 | - | 17033 |
| 16384x10 | 1.94 | 1.96 | - | 145390 | 1.68 | 1.70 | - | - | 2.57 | 2.68 | - | 134445 |
| 131072x10 | 2.80 | 2.82 | - | 1599741 | 2.49 | 2.55 | - | - | 3.43 | 3.42 | - | 3604896 |

#### gcd

| shape | x64 auto/gmpz | x64 kern/gmp | x64 inpl/gmpz | x64 auto-kern ns | clang auto/gmpz | clang kern/gmp | gcc13 kern/gmp | noIFMA auto/gmpz | M4 auto/gmpz | M4 kern/gmp | M4 inpl/gmpz | M4 auto-kern ns |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2x2 | 3.65 | 3.50 | - | 48 | 3.01 | 2.96 | - | - | 3.32 | 3.21 | - | 21 |
| 4x4 | 1.38 | 1.25 | - | 84 | 1.41 | 1.30 | - | - | 1.32 | 1.21 | - | 58 |
| 16x16 | 1.36 | 1.32 | - | 114 | 1.22 | 1.19 | - | - | 1.31 | 1.29 | - | 84 |
| 64x64 | 1.44 | 1.44 | - | 59 | 1.40 | 1.40 | - | - | 1.46 | 1.44 | - | 551 |
| 256x256 | 2.09 | 1.92 | - | 2164 | 1.87 | 1.79 | - | - | 1.99 | 1.99 | - | 1726 |
| 1024x1024 | 2.20 | 2.25 | - | -20099 | 1.85 | 1.93 | - | - | 3.00 | 3.12 | - | -25244 |
| 2000x2000 | 2.72 | 2.73 | - | -8454 | 2.24 | 2.24 | - | - | 3.90 | 3.80 | - | 99146 |
| 16384x16384 | 9.18 | 9.21 | - | -1089222 | 7.55 | 7.55 | - | - | 12.24 | 12.21 | - | -580396 |
| 65536x65536 | 24.56 | 24.53 | - | 621130 | 20.20 | 20.05 | - | - | 32.80 | 32.58 | - | 1138792 |

### 3.1 What the tables show

- **Front end.** `auto - kernel` is 13-18 ns (x64) / 5-14 ns (M4) for add/sub/shift at <= 16 limbs and ~21 ns for mul/sqr
  (x64); it is up to ~45% of the call at 64 limbs for add/sub (5-10% for mul) and vanishes above ~1000 limbs except on x64 add/sub (261 ns at 1024, 606 ns at
  2000; see 4.3) and shifts (200 ns at 2000).
- **Compiler dependence of the public add path.** x64 `c = a + b` at 1024 limbs is 2.14x gmpz with g++-14 but 1.28x with
  clang-23, and the g++ number is slower than the g++ span kernel (4.3 vs 3.0 c/l); on the M4 `auto` is 2.2x vs kernel/inplace
  3.8x because the 4-way unrolled `add_into` loop beats `add_unsigned_spans`/`add_in_place` there.
- **1024x1 add.** The span kernel (`add_unsigned_spans`) walks all 1024 limbs for the carry tail and is 2.5x GMP; the
  public path stops propagating when the carry dies (1.20x).
- **Mul/sqr.** x64 wins at 32-512 limbs (0.56-0.93x; without IFMA the same shapes are 1.26-1.83x), crosses at ~700 limbs
  and loses 1.24x (2000), 2.13x (16384), 2.8x (65536), 3.0x (1M). M4 never wins (1.1-2.0x at 16-2000 limbs) and is 3.4x at
  16384; its 1M-limb ratio is 1.29 (GMP is relatively slower there; cause not investigated).
- **Div.** 1-2x at 32-2000 limbs (schoolbook below the 160-limb Burnikel-Ziegler cutoff on x64), 3.2x at 131072x65536 and
  4.7x at 1048576x524288 on x64. `kernel/gmp` is ~auto/gmpz everywhere, so there is no front-end gap.
- **Conversions.** Tiny sizes and base 16 are faster than GMP on x64 (tochars 1x10 0.43x, 16x16 0.66x); 64-2000 limbs
  1.5-2.0x tochars, 2.1-3.0x fromchars; 262144 limbs 2.8x. fromchars is the worse direction. clang/gcc agree within noise.
- **cmp.** x64 is faster than GMP (0.5-0.74x auto/gmpz, ~1.0x kernel/gmp). M4 is at parity (1.0-1.3x).
- **Anomaly (M4 shl/shr):** GMP's `lshift` for 13 bits at 1000-4000 limbs ran 0.7-2 ns/limb, 4-10x its own speed at 512
  or with a one-limb offset (2000x77); `gmpz` shows the same. Treat the M4 shl/shr medium ratios (and `shl 2000x13`) as
  unreliable; the `2000x77` and 256-limb rows are clean.

## 4. Profiles

Recorded on x64 with the g++-14 binary used for the sweep: `perf record -e cycles:u -F 2000 --call-graph lbr`, then
`perf script --inline`, stack-collapse, and a classifier that assigns each stack to the frame nearest the leaf. Buckets are
heuristic (name based). `span_ops incl.` counts any stack with a `span_ops` frame, including the `wide_ops` carry/mul
primitives beneath it. Flame graphs: [`flamegraphs/`](flamegraphs/) (`<op>_<shape>_{auto,gmpz}.svg`; add 1000 also
`_inplace`).

### 4.1 Where the time goes

| profile (x64 gcc14) | auto | gmpz | ours top buckets | GMP top buckets | span_ops incl. |
|---|---:|---:|---|---|---:|
| add 16x16 | 26 ns | 7 ns | member_inline 46%, alloc_mem 43%, wide_ops 11% | linear 94%, alloc_mem 6% | 0% |
| divrem 131072x65536 | 77.40 ms | 24.26 ms | asm_kernel 49%, span_ops 36%, wide_ops 14%, alloc_mem 1% | basecase 42%, linear 38%, fft 12%, toom 5% | 50% |
| divrem 256x128 | 12.6 us | 5.5 us | wide_ops 55%, span_ops 42%, member_inline 1% | basecase 76%, division 12%, linear 10%, toom 3% | 96% |
| divrem 4096x2048 | 511.4 us | 373.6 us | asm_kernel 47%, span_ops 27%, wide_ops 25% | basecase 46%, linear 32%, fft 13%, toom 6% | 51% |
| fromchars 10000x10 | 3.76 ms | 2.03 ms | asm_kernel 38%, span_ops 38%, wide_ops 16%, base_conv 7% | basecase 53%, linear 24%, toom 12%, conv 9% | 54% |
| gcd 1024x1024 | 1.95 ms | 889.3 us | span_ops 45%, wide_ops 35%, gcd 20% | basecase 57%, gcd 30%, linear 9%, toom 3% | 100% |
| mul 1024x1024 | 62.4 us | 56.0 us | asm_kernel 74%, span_ops 21%, wide_ops 4% | basecase 62%, linear 24%, toom 12%, division 1% | 24% |
| mul 16384x16384 | 3.77 ms | 1.77 ms | asm_kernel 52%, span_ops 35%, wide_ops 11%, alloc_mem 1% | linear 41%, basecase 37%, fft 19%, toom 2% | 46% |
| mul 262144x262144 | 138.89 ms | 49.72 ms | fft_ntt 65%, wide_ops 26%, member_inline 5%, span_ops 3% | linear 46%, basecase 32%, fft 18%, toom 3% | 9% |
| mul 64x64 | 522 ns | 861 ns | asm_kernel 93%, alloc_mem 4%, toom_karatsuba 2% | basecase 82%, linear 13%, toom 5% | 1% |
| tochars 10000x10 | 6.01 ms | 4.31 ms | asm_kernel 36%, span_ops 28%, wide_ops 25%, base_conv 8% | basecase 54%, linear 22%, division 8%, toom 8% | 53% |

Flame graphs: [add 16x16](flamegraphs/add_16x16_auto.svg) / [gmpz](flamegraphs/add_16x16_gmpz.svg),
[add 1000](flamegraphs/add_1000x1000_auto.svg) / [inplace](flamegraphs/add_1000x1000_inplace.svg) / [gmpz](flamegraphs/add_1000x1000_gmpz.svg),
[mul 64](flamegraphs/mul_64x64_auto.svg), [mul 1024](flamegraphs/mul_1024x1024_auto.svg),
[mul 16384](flamegraphs/mul_16384x16384_auto.svg) / [gmpz](flamegraphs/mul_16384x16384_gmpz.svg),
[mul 262144](flamegraphs/mul_262144x262144_auto.svg) / [gmpz](flamegraphs/mul_262144x262144_gmpz.svg),
[divrem 256x128](flamegraphs/divrem_256x128_auto.svg), [4096x2048](flamegraphs/divrem_4096x2048_auto.svg),
[131072x65536](flamegraphs/divrem_131072x65536_auto.svg),
[tochars 10000](flamegraphs/tochars_10000x10_auto.svg), [fromchars 10000](flamegraphs/fromchars_10000x10_auto.svg),
[gcd 1024](flamegraphs/gcd_1024x1024_auto.svg) (each has a `_gmpz.svg` sibling).

### 4.2 Time saved if the linear helpers ran at GMP-like speed

`span_ops incl.` ns x (1 - 1/r), with r = 1.5 (measured add/sub/submul_1 ratio at 64 limbs on x64) to r = 2.0
(mul_1, shifts, M4-like). An estimate, not a measurement; it assumes every helper in the bucket speeds up by r.

| profile | span_ops incl. ns | saved at r=1.5 | saved at r=2.0 | auto -> projected | gmpz | projected ratio |
|---|---:|---:|---:|---:|---:|---:|
| divrem 131072x65536 | 38.62 ms | 12.87 ms | 19.31 ms | 64.52 ms to 58.09 ms | 24.26 ms | 2.66 to 2.39 (now 3.19) |
| divrem 256x128 | 12.1 us | 4.0 us | 6.1 us | 8.6 us to 6.6 us | 5.5 us | 1.57 to 1.20 (now 2.31) |
| divrem 4096x2048 | 261.8 us | 87.3 us | 130.9 us | 424.1 us to 380.5 us | 373.6 us | 1.14 to 1.02 (now 1.37) |
| fromchars 10000x10 | 2.04 ms | 680.2 us | 1.02 ms | 3.08 ms to 2.74 ms | 2.03 ms | 1.52 to 1.35 (now 1.85) |
| gcd 1024x1024 | 1.57 ms | 522.0 us | 783.0 us | 1.43 ms to 1.17 ms | 889.3 us | 1.61 to 1.32 (now 2.20) |
| mul 1024x1024 | 15.3 us | 5.1 us | 7.6 us | 57.3 us to 54.7 us | 56.0 us | 1.02 to 0.98 (now 1.11) |
| mul 16384x16384 | 1.75 ms | 583.6 us | 875.4 us | 3.19 ms to 2.90 ms | 1.77 ms | 1.81 to 1.64 (now 2.14) |
| mul 262144x262144 | 12.64 ms | 4.21 ms | 6.32 ms | 134.68 ms to 132.57 ms | 49.72 ms | 2.71 to 2.67 (now 2.79) |
| tochars 10000x10 | 3.17 ms | 1.06 ms | 1.58 ms | 4.95 ms to 4.43 ms | 4.31 ms | 1.15 to 1.03 (now 1.39) |

Reading: linear speed alone brings divrem 4096x2048, tochars 10000 and mul 1024 to parity and gcd 1024 from 2.2x to
1.3-1.6x; it does not fix divrem 256x128 (1.2-1.6x left: the schoolbook division below the BZ cutoff 160 does 91% of its
time in `submul_single_limb`, while GMP's profile is 76% `mul_basecase`-class, i.e. it is already
divide-and-conquer there), nor mul 16384 (1.64-1.81x left), nor anything FFT-bound.

### 4.3 Specific findings

- **add 1000 `auto` on x64 (g++-14):** 33% `construct_at`, 28% `basic_big_int.hpp` (the `add_into` loop), 26% `wide_ops`
  (`carrying_add`), 11% memmove from the copy-assign in `operator=` (the prvalue result appears to be copied into the
  destination instead of stolen), 1.5% malloc. The store lambda writes through `std::construct_at` per limb and the
  carry is re-materialized per limb. The kernel is 578 ns, `auto` 840 ns, gmpz 390 ns.
- **add 16x16:** 43% allocation (`alloc_mem`), 46% member-function inline code; GMP spends 94% in the add loop.
- **mul 16384:** our asm basecase alone is 1.98 ms (52%), more than GMP's whole multiply (1.77 ms, of which basecase 37%,
  0.66 ms; GMP's `linear` bucket is 41%, 0.73 ms). `span_ops` incl. is 1.75 ms (46%) versus GMP's ~0.73 ms linear work.
- **mul 262144:** 65% `fft_ntt` + 26% `wide_ops` (modular multiply primitives); `span_ops` is only 3-9%. This is
  an NTT-implementation/algorithm gap, not a linear-kernel gap.
- **divrem 131072x65536:** 88% of the stacks are inside `multiply*` (Burnikel-Ziegler divide-and-conquer); the gap is the
  large-mul gap.
- **tochars/fromchars 10000:** the power-chain construction (`build_power_table`/`append_squared_power`) is only 4.5%
  (tochars) and 7.1% (fromchars); tochars is 72% inside Burnikel-Ziegler division (39% in multiply) and fromchars 47% in
  `combine_level` multiplies and 42% in the basecase. So at 10000 limbs rebuilding the power chain is not the lever; the
  multiply/divide gap is.
- **gcd 1024:** `span_ops` 45% + `wide_ops` 35% + the Lehmer driver 20%; GMP is 57% basecase + 30% `gcd` + 9% linear.

## 5. Probe: linear kernel cycles/limb

A standalone probe (`tools/probe/probe.cpp`; `noinline` wrappers, L1-resident, 64 limbs, -O2)
times each library kernel against GMP's. x64 values are `perf stat` cycles per limb, M4 values are ns/limb x 4.47 GHz
(approximate). "x GMP" = ratio to GMP's kernel on the same machine.

| kernel | variant | gcc13 c/l | gcc14 c/l | clang23 c/l | M4 c/l | gcc14 / GMP | M4 / GMP |
|---|---|---:|---:|---:|---:|---:|---:|
| add_n | lib_add_unsigned_spans | 3.97 | 2.63 | 2.78 | 3.26 | 1.51 | 3.93 |
| add_n | loop_carrying_add | 3.97 | 2.59 | 2.10 | 3.27 | 1.48 | 3.94 |
| add_n | addcll_x4 | - | 1.97 | 1.91 | 1.43 | 1.12 | 1.73 |
| add_n | asm_adc_x4 | 1.89 | 1.90 | 1.79 | - | 1.09 | - |
| add_n | gmp | 1.75 | 1.75 | 1.71 | 0.83 | 1.00 | 1.00 |
| sub_n | lib_subtract_unsigned_spans | 3.96 | 2.67 | 2.46 | 3.48 | 1.51 | 4.24 |
| sub_n | loop_borrowing_sub | 3.96 | 2.65 | 2.28 | 3.30 | 1.50 | 4.02 |
| sub_n | gmp | 1.75 | 1.77 | 1.73 | 0.82 | 1.00 | 1.00 |
| lshift13 | lib_copy_shift_left_n | 2.72 | 2.55 | 0.72 | 1.06 | 2.39 | 1.19 |
| lshift13 | gmp_copy_inplace | 1.41 | 0.78 | 1.49 | 1.53 | 0.73 | 1.72 |
| lshift13 | gmp | 1.09 | 1.07 | 1.04 | 0.89 | 1.00 | 1.00 |
| lshift1 | lib_copy_shift_left_n | 2.71 | 2.65 | 0.73 | 1.05 | 2.48 | 1.17 |
| lshift1 | gmp_copy_inplace | 1.53 | 1.42 | 1.26 | 1.57 | 1.33 | 1.75 |
| lshift1 | gmp | 1.07 | 1.07 | 1.05 | 0.90 | 1.00 | 1.00 |
| lshift63 | lib_copy_shift_left_n | 2.87 | 2.75 | 0.74 | 1.06 | 2.58 | 1.18 |
| lshift63 | gmp_copy_inplace | 1.30 | 1.40 | 1.39 | 1.57 | 1.32 | 1.75 |
| lshift63 | gmp | 1.06 | 1.06 | 1.05 | 0.90 | 1.00 | 1.00 |
| submul_1 | lib_submul_single_limb | 4.45 | 3.70 | 4.07 | 4.83 | 1.51 | 3.14 |
| submul_1 | gmp | 2.53 | 2.46 | 2.60 | 1.54 | 1.00 | 1.00 |
| mul_1 | lib_multiply_single_limb | 3.30 | 2.90 | 2.31 | 1.80 | 2.16 | 1.85 |
| mul_1 | gmp | 1.36 | 1.34 | 1.26 | 0.98 | 1.00 | 1.00 |
| divrem_1_norm | lib_divide_unsigned_short | 15.79 | 13.29 | 16.93 | 14.94 | 1.09 | 1.48 |
| divrem_1_norm | gmp | 12.18 | 12.17 | 12.09 | 10.06 | 1.00 | 1.00 |
| divrem_1_unnorm | lib_divide_unsigned_short | 15.75 | 14.29 | 17.11 | 14.91 | 1.13 | 1.45 |
| divrem_1_unnorm | gmp | 12.55 | 12.61 | 12.51 | 10.30 | 1.00 | 1.00 |
| divexact_3 | lib_divide_unsigned_short | 15.76 | 14.29 | 17.11 | 14.89 | - | - |
| divexact_3 | gmp_divrem_1 | 12.55 | 12.61 | 12.50 | 10.23 | - | - |
| divexact_3 | gmp_divexact_by3c | 2.73 | 2.74 | 2.76 | 1.71 | - | - |
| divexact_3 | gmp_bdiv_q_1 | 7.63 | 7.65 | 7.61 | 4.47 | - | - |
| divexact_3 | gmp_divexact_1 | 7.91 | 7.88 | 7.81 | 5.59 | - | - |

### 5.0 Same kernels at 512 limbs (-O2)

The 512-limb size is closer to the span lengths of the Toom/Karatsuba linear passes in large products.

| kernel | variant | gcc13 c/l | gcc14 c/l | clang23 c/l | M4 c/l | gcc14 / GMP | M4 / GMP |
|---|---|---:|---:|---:|---:|---:|---:|
| add_n | lib_add_unsigned_spans | 4.00 | 2.96 | 2.97 | 3.91 | 1.71 | 3.71 |
| add_n | loop_carrying_add | 4.00 | 2.95 | 2.47 | 3.99 | 1.70 | 3.79 |
| add_n | addcll_x4 | - | 2.01 | 2.24 | 1.82 | 1.16 | 1.73 |
| add_n | asm_adc_x4 | 1.78 | 1.78 | 1.76 | - | 1.02 | - |
| add_n | gmp | 1.74 | 1.73 | 1.74 | 1.05 | 1.00 | 1.00 |
| sub_n | lib_subtract_unsigned_spans | 3.99 | 3.02 | 2.45 | 3.96 | 1.73 | 3.75 |
| sub_n | loop_borrowing_sub | 4.00 | 2.96 | 2.47 | 3.96 | 1.70 | 3.75 |
| sub_n | gmp | 1.74 | 1.74 | 1.73 | 1.06 | 1.00 | 1.00 |
| submul_1 | lib_submul_single_limb | 4.52 | 3.94 | 4.26 | 5.24 | 1.46 | 3.11 |
| submul_1 | gmp | 2.71 | 2.71 | 2.71 | 1.69 | 1.00 | 1.00 |
| mul_1 | lib_multiply_single_limb | 2.81 | 2.53 | 2.67 | 2.17 | 2.04 | 2.01 |
| mul_1 | gmp | 1.20 | 1.24 | 1.20 | 1.08 | 1.00 | 1.00 |

- Variants: `loop_carrying_add` is the library's `carrying_add` loop; `addcll_x4` a 4x-unrolled `__builtin_addcll` loop
  (not available on g++-13); `asm_adc_x4` a 4x `adc` inline-asm loop (x64 only).
- At 512 limbs `addcll_x4` is 1.16 (g++-14) / 1.29 (clang-23) x GMP while `asm_adc_x4` is 1.02-1.03x, so the asm advantage grows
  from 4-7% (64 limbs) to 12-21% (512); a rewritten C++ kernel must be re-measured at 512+ limbs.
- gcc13 vs gcc14 `lib_add` (64 limbs): 3.97 vs 2.63 c/l. Absolute GMP numbers (1.75 c/l add_n on x64) are above GMP's published
  ~1.0-1.1 for this core class, so only the ratios should be trusted (Caveats).
- Not measured at the probe level: exact division by 5 and the full Toom interpolation share of `divide_unsigned_short`;
  the profile buckets do not isolate them. The 5.2x divexact-by-3 gap (14.3 vs 2.74 c/l) is real but its weight in mul
  is unquantified.

### 5.1 `add_n` hot loops (x64, -O2)

g++-13 (no `__builtin_addc`: two `setb` and an `or` per limb, carry round-trips through a register):
```
mov (%rcx),%rdx ; movzbl %al,%eax ; add 0x1040(%rcx),%rdx ; setb %sil ; add %rax,%rdx ; setb %r8b
mov %esi,%eax ; mov %rdx,0x2080(%rcx) ; add $8,%rcx ; or %r8d,%eax ; cmp ; jne
```
g++-14 (`adc`, but the carry is re-created every limb: `add $0xff,%al` ... `setb %al`):
```
add $0xff,%al ; mov 0x1040(%rdx),%rax ; adc (%rdx),%rax ; mov %rax,0x2080(%rdx) ; setb %al ; add $8,%rdx ; cmp ; jne
```
clang-23 (2x unrolled, `add $0xff,%sil` / `setb` / `movzbl` between the pairs):
```
mov (..),%r8 ; mov 8(..),%r9 ; add 0x1040(..),%r8 ; setb %r10b ; add %rsi,%r8 ; setb %sil ; or %r10b,%sil
mov %r8,0x2080(..) ; add $0xff,%sil ; adc 0x1048(..),%r9 ; setb %r8b ; movzbl %r8b,%esi ; mov %r9,0x2088(..) ; ...
```
Inline-asm 4x (what GMP-class code does: four `adc` back to back, one carry materialization per 4 limbs):
```
mov (%rbx,%rsi,8),%r8 ; mov 8(..),%r9 ; mov 16(..),%r10 ; mov 24(..),%r11
adc (%rdx,%rsi,8),%r8 ; adc 8(..),%r9 ; adc 16(..),%r10 ; adc 24(..),%r11 ; mov %r8,(%rdi,%rsi,8) ; ...
```
`tools/probe/run_x64.sh` writes the complete listings for every compiler and variant.

## 6. Ranked optimization list

Decision criteria applied (from the plan):
- **Linear ops in asm** only if kernel/gmp >= 1.3x at 64-2000 limbs and linear helpers >= 10% of the mul/div/conversion
  profiles: met on both archs (x64 1.5-2.4x add/sub, 2.0-2.5x shifts; M4 3.8-4.3x; helpers 24-54% inclusive in the
  profiles above), but the "unrolled builtins first" clause applies and decides the order: on x64 `addcll_x4` is within 4-7% of the asm loop at 64 limbs, but 12-21% behind it at 512 limbs, so asm is
  deferred (item 8), not ruled out.
- **Front-end fixes first** where `auto - kernel` >= 30% of kernel time at <= 64 limbs: met for add/sub/shl/shr (~15 ns on
  a 4-25 ns kernel), mul <= 16 limbs (21 ns on 5-80 ns), divrem 8x4 (66 ns on 44 ns); not met for gcd 2x2 (11%),
  tochars 4x10 (auto already faster).
- **Algorithmic items** flagged where the ratio grows with n: gcd (1.4 at 64 -> 24.6 at 65536 limbs), mul/sqr above 2000
  limbs, divrem above 4000, conversions above 2000.

| # | Item | Evidence | Expected gain | Effort | Arch |
|---|---|---|---|---|---|
| 1 | Cut fixed per-call cost of `+ - << >>`, small mul/div: inline capacity beyond one limb, avoid `construct_at` zero/store loops and the copy in `operator=`, avoid double zero-fill in `c = a * b` | auto - kernel 13-18 ns on add/sub/shift <= 16 limbs, ~21 ns mul <= 16; add 16x16 profile 43% alloc + 46% member code; add 1000 profile 33% `construct_at` + 11% memmove | add/sub/shl small 3.6-4.6x -> ~1.3-1.8x GMP (remove ~13-18 ns of 18-25); add 1000 on x64: auto 840 -> ~580 ns (kernel level) | S-M | both |
| 2 | Rewrite `add/sub_unsigned_spans`, `shift_left_n/right_n`, `multiply_single_limb`, `submul_single_limb` as 4x-unrolled carry chains (builtin addc/subb), fix g++-13 fallback, keep one carry materialization per 4 limbs | probe: add 1.51x -> 1.12x at 64 limbs, 1.70x -> 1.16x at 512 (x64), 3.9x -> 1.7x (M4); gcc shifts 2.4-2.6x vs clang 0.7x; submul 1.5x/3.1x; mul_1 2.2x/1.9x | add/sub 64-2000 limbs 1.5-2.4x -> ~1.1-1.3x; mul 16384 -0.58 to -0.88 ms (2.14 -> 1.64-1.81); divrem 4096x2048 1.37 -> 1.0-1.14; tochars 10000 1.39 -> 1.03-1.15; fromchars 10000 1.85 -> 1.35-1.5; gcd 1024 2.2 -> 1.3-1.6 | S (add/sub) to M (mul_1/submul) | both |
| 3 | Subquadratic gcd (HGCD / half-gcd) above ~1000-2000 limbs; keep Lehmer below | gcd auto/gmpz 2.2 (1k), 2.7 (2k), 9.2 (16k), 24.6 (65k) x64; M4 12.2/32.7; auto == kernel, so no front-end part | 16k limbs: 405 ms -> ~45 ms; 65k: 6.4 s -> ~0.26 s (GMP's times) | L | both |
| 4 | Faster large multiplication: NTT/FFT implementation (65% `fft_ntt` + 26% `wide_ops` at 262144), Toom-8 / unbalanced and FFT crossovers for 16k-65k limbs; benefits sqr, divrem and conversions | mul 2.13x (16k), 2.85x (65k), 3.0x (1M); sqr 3.6x (1M); divrem 131072x65536 88% in multiply; tochars 262144 2.8x | up to 2-3x on mul/sqr >= 65k limbs and the large div/conversion rows that sit on them | L | both (M4: mul 3.4x at 16384, 2.5x at 65536, 1.9x at 262144, 1.3x at 1M; it has NEON NTT kernels) |
| 5 | Re-tune Karatsuba/Toom/BZ cutoffs after #2, and use exact division (divexact by 3, 5) in Toom interpolation | mul 16384: our asm basecase 1.98 ms > GMP total 1.77 ms; mul ratio 0.6-0.9 (64-512) rising to 1.24 (2000) and 2.1 (16384); divexact_3 14.3 vs 2.74 c/l; BZ cutoff 160 leaves divrem 128x64..512x256 at 1.8-1.9x | not quantifiable before #2; the mul 2000-16384 band (1.24-2.13x) is the target; each cutoff change must be verified end to end | M | x64 first (IFMA table) |
| 6 | AArch64 asm for add_n/sub_n/mul_1/submul_1 (`adcs`/`sbcs` chains) | M4 after unrolling: addcll_x4 still 1.7x GMP, submul 3.1x, mul_1 1.9x; no asm variant was probed on M4 | M4 add/sub 64-2000 limbs 3.8-4.3x -> ~1.0-1.7x; effect on M4 mul/div rows not estimated | M | AArch64 |
| 7 | x64 `submul_1`/`mul_1` asm (mulx/adx) if #2 leaves them above ~1.3x GMP | probe 1.5x / 2.2x at 64 limbs | divrem 128x64..512x256 (91% submul): up to ~1.5x on those shapes | M | x64 |
| 8 | x64 add/sub in asm: reconsider after #2 (re-measure the rewritten C++ at 512+ limbs) | probe add_n: `asm_adc_x4` 1.09x vs `addcll_x4` 1.12x at 64 limbs, but 1.02-1.03x vs 1.16-1.28x GMP at 512 limbs (gcc14 / clang23) | asm currently buys 12-21% at 512 limbs over the unrolled builtin (4-7% at 64); worth it only if #2 leaves add/sub > ~1.15x at 512+ limbs | M | x64 |
| 9 | Make `add_unsigned_spans` stop at the carry tail (1024x1 add is 2.5x GMP in the kernel row) and give the public add the same compiler-independent loop | kernel/gmp 2.5x, auto 1.2x | kernel rows only; public path already fine | S | both |
| 10 | Conversions: after #2 and #4, recheck; power-chain reuse looks minor at 10000 limbs (4.5-7%) but is untested above 100k limbs | tochars/fromchars 262144/131072 2.8x | speculative | M | both |

Status of item 1: implemented on branch `opt_1`; results, targets met and not met, and the inline-capacity study are in
[`item1_frontend_results.md`](item1_frontend_results.md) (small `auto` is now 2-5 ns above the allocation floor for
mul/sqr, 0-6 ns for shifts and 3-6 ns for add/sub; x64 add 1000 840 -> 600 ns, meeting the 650 ns goal; open items: x64
GCC in-place shifts at >= 256 limbs are 5-8% slower than the baseline, and a few sub-nanosecond small in-place rows)

Not recommended: asm for cmp (already faster than GMP on x64, parity on M4); asm for mul 32-512 limbs on x64 (IFMA beats
GMP by 1.1-1.8x).

## 7. Caveats

- GMP's absolute `add_n` is ~1.75-2.0 c/l in both the probe and the sweep on x64, above its published ~1.0-1.1 on this
  class of core (possible 4K aliasing or buffer placement shared by the probe and harness). Ratios are like-for-like;
  absolute cycles are harness numbers.
- M4 GMP `lshift` at 1000-4000 limbs x 13 bits is erratic (0.7-2 ns/limb); M4 shl/shr medium ratios are unreliable there.
- 1M-limb add/sub rows are DRAM-bound with up to +-20% seed spread; M4 frequency is assumed (4.47 GHz), x64 pinned and
  measured.
- The classifier buckets are name-based heuristics; `span_ops incl.` counts any stack with a `span_ops` frame, so it
  includes the `wide_ops` leaves beneath it. The `ns saved` table assumes a uniform speed-up r over the bucket.
- The kernel shift rows include a copy-in; `kernelip` (in-place pair, in the CSVs for shl) does not. The gcd `kernel` row
  exists only for square shapes (it skips the driver's Euclidean pre-steps). mul `kernel` = `unsliced`; auto may slice.
- noIFMA changes the tuning table as well as the kernels, so it does not isolate the IFMA contribution at large sizes.
  The g++-13 run covers only the linear ops. Only one clang (23.0.0 dev) and one GMP configuration per machine were used.
- The flame-graph profiles are x64 only and use one shape per op/size class, averaged over the profiling run (medians in
  4.1 come from the profile run or the sweep as marked in `buckets.md`, so they can differ by a few percent from section 3).

## 8. Appendix: reproducing

The tools live in [`tools/`](tools/README.md): `gap_sweep.sh`, `selftest.sh`, `summarize.py`, `compare.py`,
`probe_tables.py`, `bucket_tables.py`, `prof.sh`, `classify.py` and the standalone `tools/probe/` (see its README for inputs and
outputs).

Configure and build (any machine; `-DBEMAN_BIG_INT_SIMD_MUL=ON` for the numbers here; the harness is
`beman.big_int.benchmarks.shape_sweep`):
```
# M4
cmake --preset appleclang-release -B build/gmpgap/mac-release -DBEMAN_BIG_INT_BUILD_BENCHMARKS=ON \
  -DBEMAN_BIG_INT_SHAPE_SWEEP_ONLY=ON -DBEMAN_BIG_INT_SWEEP_GMP=ON -DBEMAN_BIG_INT_SIMD_MUL=ON
cmake --build build/gmpgap/mac-release --target beman.big_int.benchmarks.shape_sweep
# x64, one build directory per configuration. infra/cmake/gnu-toolchain.cmake hard-codes `g++`, so for g++-14 put a
# directory whose `g++` and `gcc` point at GCC 14 first on PATH. GMP 6.3.0 is found by the usual library search
# (CMAKE_PREFIX_PATH=<gmp prefix> for a private build).
C="-DBEMAN_BIG_INT_BUILD_BENCHMARKS=ON -DBEMAN_BIG_INT_SHAPE_SWEEP_ONLY=ON -DBEMAN_BIG_INT_SWEEP_GMP=ON -DBEMAN_BIG_INT_SIMD_MUL=ON"
PATH=<gcc14-shim-dir>:$PATH cmake --preset gcc-release -B build/gmpgap/x64-gcc14 "-DCMAKE_CXX_FLAGS=-march=native" $C
cmake --preset gcc-release -B build/gmpgap/x64-gcc13 "-DCMAKE_CXX_FLAGS=-march=native" $C          # system g++-13
cmake --preset llvm-release -B build/gmpgap/x64-clang "-DCMAKE_CXX_FLAGS=-march=native" $C         # clang-23
PATH=<gcc14-shim-dir>:$PATH cmake --preset gcc-release -B build/gmpgap/x64-noifma "-DCMAKE_CXX_FLAGS=-march=native" \
  -DBEMAN_BIG_INT_X86_64_AVX512_IFMA=OFF $C
for d in x64-gcc14 x64-gcc13 x64-clang x64-noifma; do cmake --build build/gmpgap/$d --target beman.big_int.benchmarks.shape_sweep; done
```
The numbers here were produced with an out-of-tree copy-and-build driver (`variant.sh`, not in the repo) that also checked
the `#const` keys (`EXPECT="BMI2_ADX=1 AVX512_IFMA=1 SIMD_MUL=1"`, `AVX512_IFMA=0` for noIFMA); the equivalent check is
reading the first `#const` line of each binary.

Check the first `#const` line of each binary (`compiler=`, `BMI2_ADX=`, `AVX512_IFMA=`, `SIMD_MUL=`, `ndebug=1`).

Run (paths relative to `tests/beman/big_int/perf`; the sweep driver encodes the size grid, rows per op, 9 rounds / 3 for the large band / 1 for gcd 65536):
```
tools/gap_sweep.sh <shape_sweep> out.csv --seed 1 [--ops add,sub,...] [--bands small,medium,large] [--pin "taskset -c 2"]
tools/summarize.py a.csv [b.csv] [probe.csv] > summary.md          # python3 -I; two CSVs => seed spread
```
The probe: `tools/probe/build_x64.sh` then `run_x64.sh` (x64), `build_mac.sh` then `run_mac.sh` (M4); `probe --check` is the
correctness run.
Single shapes by hand: `shape_sweep <op> --rows auto,inplace,kernel,gmp,gmpz [--rounds N] [--round-ms X] [--seed S] SHAPE...`.
Primary sweep: ~4.5 min per full run on either machine (gcc14 x64 268 s, M4 281 s); g++-13 linear ops 52 s; noIFMA 55 s.

Profiles (x64 Linux, `kernel.perf_event_paranoid=1`, FlameGraph cloned to `$FLAMEGRAPH_DIR`):
```
tools/prof.sh <shape_sweep> profiles/                       # all 25 shape/row pairs; add a substring to run a subset
tools/classify.py profiles/ gap_x64_gcc14_seed1.csv > profiles/buckets.md
tools/bucket_tables.py profiles/buckets.md
```
`prof.sh` records `perf record -q -e cycles:u -F 2000 --call-graph lbr` under `taskset -c 2` with
`--rows ROW --rounds 1 --round-ms 4000 --seed 1`, folds with `perf script --inline | stackcollapse-perf.pl`, strips template
arguments and parameter lists, and renders with `flamegraph.pl --minwidth 0.5 --width 1200`. ROW is `auto` and `gmpz` (and
`inplace` for add 1000). `MINW` overrides the minwidth (not overridden for any of the 25 SVGs; largest SVG 464 KB).
`classify.py` buckets the folded stacks by the frame nearest the leaf and scales by the median.
