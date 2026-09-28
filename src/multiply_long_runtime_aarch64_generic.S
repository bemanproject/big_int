// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Schoolbook long multiplication, baseline AArch64 only (ARMv8.0-A A64; no NEON/SVE/LSE).

// Mach-O wants a leading underscore on external symbols and bare-"L" local
// labels (no dot, so they never make it into the symbol table); ELF wants
// neither of those but does want .type/.size and a non-executable-stack note.
#if defined(__APPLE__)
    #define SYM(x) _##x
    #define L(x) L##x
#else
    #define SYM(x) x
    #define L(x) .L##x
#endif

.text

.globl SYM(beman_big_int_multiply_long_runtime)
#if defined(__ELF__)
    .type SYM(beman_big_int_multiply_long_runtime), %function
#endif

//   x0 -> p_result
//   x1 -> p_a
//   x2 -> len_a
//   x3 -> p_b
//   x4 -> len_b
//
// Writes exactly len_a + len_b limbs. The first row stores instead of
// accumulating, so p_result need not be pre-zeroed (matching multiply_long).
// Writes nothing when either length is 0.
//
// Register allocation once the prologue has run:
//
//   x0 -> result walk: advances one limb per inner-loop step, then gets
//         rewound to result + j + 1 (the next row's start) at row end
//   x1 -> a base, fixed; reloaded into x7 at the start of every row
//   x2 -> a_end = a + 8 * len_a, fixed
//   x3 -> b walk, post-incremented by one limb at the start of every row
//   x4 -> rows left to do, starts as len_b and counts down to 0
//   x5 -> b[j], the row's multiplier
//   x6 -> the row's running carry
//   x7 -> a walk, from a base to a_end over the course of a row
//   x8 -> a + 8 * (len_a & 3), fixed: the boundary between the scalar
//         remainder loop and the 4x unrolled body
//   x9-x17 -> temporaries, reused by every macro below
//
// Both walking pointers (x0 and x7) start each row at the row's base and
// advance in lockstep, so no index has to be rescaled between rows: only
// x0 needs an explicit rewind (row length back, then one limb forward)
// once a row's carry has been stored.
//
// Leaf function: only x0-x17 are used, no stack, no calls.

// One limb of the first row: result[i] = a[i] * b[0] + carry.
.macro GENERIC_ARM64_MUL_LIMB
    ldr     x9, [x7], #8    // x9 = a[i], a walk advances
    mul     x11, x9, x5     // x11 = low64(a[i] * b[j])
    umulh   x12, x9, x5     // x12 = high64(a[i] * b[j])

    adds    x11, x11, x6    // low64 += carry
    adc     x6, x12, xzr    // carry = high64 + carry_flag

    str     x11, [x0], #8   // result[i] = low64, result walk advances
.endm

// One limb of a later row: result[i + j] += a[i] * b[j] + carry.
//
// The load of the stored limb goes in before the carry, mirroring the x64
// "stored limb first, incoming carry last" ordering: a[i] * b[j] + result[i + j]
// + carry is at most 2^128 - 1 (single-limb induction: carry <= limb_max), so
// neither adc can carry out of high64.
.macro GENERIC_ARM64_ADDMUL_LIMB
    ldr     x9, [x7], #8    // x9 = a[i], a walk advances
    ldr     x10, [x0]       // x10 = result[i + j]
    mul     x11, x9, x5     // x11 = low64(a[i] * b[j])
    umulh   x12, x9, x5     // x12 = high64(a[i] * b[j])

    adds    x11, x11, x10   // low64 += result[i + j]
    adc     x12, x12, xzr   // high64 += carry_flag
    adds    x11, x11, x6    // low64 += carry
    adc     x6, x12, xzr    // carry = high64 + carry_flag

    str     x11, [x0], #8   // result[i + j] = low64, result walk advances
.endm

// Four limbs of the first row, taking the place of GENERIC_ARM64_MUL_LIMB run
// four times. There is no stored result to fold in, so the low-half sum
// (chain A on the accumulating version below) is never loop-carried here:
// each l_k is simply lo_k, and the shifted high halves plus the incoming
// carry are added in one pass (chain B), closed with xzr in place of a
// chain-A carry that cannot occur.
.macro GENERIC_ARM64_MUL_BLOCK4
    ldp     x9, x10, [x7], #16    // x9, x10  = a[i], a[i+1]
    ldp     x11, x12, [x7], #16   // x11, x12 = a[i+2], a[i+3]; a walk advances by 32

    mul     x13, x9, x5           // x13 = lo(a[i]   * b[j])
    mul     x14, x10, x5          // x14 = lo(a[i+1] * b[j])
    mul     x15, x11, x5          // x15 = lo(a[i+2] * b[j])
    mul     x16, x12, x5          // x16 = lo(a[i+3] * b[j])

    umulh   x9, x9, x5            // x9  = hi(a[i]   * b[j])
    umulh   x10, x10, x5          // x10 = hi(a[i+1] * b[j])
    umulh   x11, x11, x5          // x11 = hi(a[i+2] * b[j])
    umulh   x12, x12, x5          // x12 = hi(a[i+3] * b[j])

    adds    x13, x13, x6          // l0 += carry
    adcs    x14, x14, x9          // l1 += hi0 + carry_flag
    adcs    x15, x15, x10         // l2 += hi1 + carry_flag
    adcs    x16, x16, x11         // l3 += hi2 + carry_flag
    adc     x6, x12, xzr          // carry = hi3 + carry_flag

    stp     x13, x14, [x0], #16   // result[i .. i+1]   = l0, l1
    stp     x15, x16, [x0], #16   // result[i+2 .. i+3] = l2, l3; result walk advances by 32
.endm

// Four limbs of a later row, taking the place of GENERIC_ARM64_ADDMUL_LIMB run
// four times.
//
// Chain A folds the four low halves into the stored result limbs r0..r3
// (a plain 4-limb ripple-carry add, independent of the incoming carry),
// closed by capturing its carry-out t in {0, 1}. Chain B then folds in the
// incoming carry at r0 and the shifted high halves at r1..r3, closed by
// adding t. Treating a[i..i+3] as one 4-limb number A and r0..r3 as one
// 4-limb number R, the whole block computes R + A * b[j] + carry, which is
// at most B^5 - 1 for B = 2^64 (induction: carry <= limb_max), so the new
// carry out of the block (hi3 + t + carry_flag) never exceeds a limb either.
.macro GENERIC_ARM64_ADDMUL_BLOCK4
    ldp     x9, x10, [x7], #16    // x9, x10  = a[i], a[i+1]
    ldp     x11, x12, [x7], #16   // x11, x12 = a[i+2], a[i+3]; a walk advances by 32
    ldp     x13, x14, [x0]        // x13, x14 = result[i], result[i+1]
    ldp     x15, x16, [x0, #16]   // x15, x16 = result[i+2], result[i+3]

    mul     x17, x9, x5           // x17 = lo(a[i] * b[j])
    adds    x13, x13, x17         // r0 += lo0
    mul     x17, x10, x5          // x17 = lo(a[i+1] * b[j])
    adcs    x14, x14, x17         // r1 += lo1 + carry_flag
    mul     x17, x11, x5          // x17 = lo(a[i+2] * b[j])
    adcs    x15, x15, x17         // r2 += lo2 + carry_flag
    mul     x17, x12, x5          // x17 = lo(a[i+3] * b[j])
    adcs    x16, x16, x17         // r3 += lo3 + carry_flag
    adc     x17, xzr, xzr         // t = carry_flag (0 or 1)

    umulh   x9, x9, x5            // x9  = hi(a[i]   * b[j])
    umulh   x10, x10, x5          // x10 = hi(a[i+1] * b[j])
    umulh   x11, x11, x5          // x11 = hi(a[i+2] * b[j])
    umulh   x12, x12, x5          // x12 = hi(a[i+3] * b[j])

    adds    x13, x13, x6          // r0 += carry
    adcs    x14, x14, x9          // r1 += hi0 + carry_flag
    adcs    x15, x15, x10         // r2 += hi1 + carry_flag
    adcs    x16, x16, x11         // r3 += hi2 + carry_flag
    adc     x6, x12, x17          // carry = hi3 + t + carry_flag

    stp     x13, x14, [x0], #16   // result[i .. i+1]   = r0, r1
    stp     x15, x16, [x0], #16   // result[i+2 .. i+3] = r2, r3; result walk advances by 32
.endm

.p2align 2
SYM(beman_big_int_multiply_long_runtime):
.cfi_startproc

    // a * b is symmetric, so run the inner loop over the longer operand: that
    // makes the number of rows, and with it all per-row overhead, min(len_a, len_b).

    cmp     x2, x4
    b.hs    L(generic_arm64_sizes_ordered)

    mov     x5, x1   // swap p_a and p_b through x5
    mov     x1, x3
    mov     x3, x5
    mov     x5, x2   // swap len_a and len_b through x5
    mov     x2, x4
    mov     x4, x5

L(generic_arm64_sizes_ordered):

    cbz     x4, L(generic_arm64_end)   // an empty length leaves nothing to do

    and     x9, x2, #3           // x9 = len_a & 3
    add     x8, x1, x9, lsl #3   // x8 = a + 8 * (len_a & 3): remainder/4x boundary
    add     x2, x1, x2, lsl #3   // x2 = a_end = a + 8 * len_a

    // First row (j = 0) stores instead of accumulating. That drops a load and
    // an add/adc pair per limb, and it is what lets the caller hand over a
    // buffer it has not zeroed.

    ldr     x5, [x3], #8   // x5 = b[0], b walk advances
    mov     x7, x1         // a walk = a base
    mov     x6, xzr        // carry = 0

    cmp     x7, x8
    b.eq    L(generic_arm64_mul_row_4x)

L(generic_arm64_mul_row_rmdr):

    GENERIC_ARM64_MUL_LIMB

    cmp     x7, x8
    b.ne    L(generic_arm64_mul_row_rmdr)

L(generic_arm64_mul_row_4x):

    cmp     x7, x2
    b.eq    L(generic_arm64_mul_row_end)

.p2align 4
L(generic_arm64_mul_row_4x_unroll):

    GENERIC_ARM64_MUL_BLOCK4

    cmp     x7, x2
    b.ne    L(generic_arm64_mul_row_4x_unroll)

L(generic_arm64_mul_row_end):

    str     x6, [x0]     // result[len_a] = carry

    sub     x9, x2, x1   // x9 = a_end - a_base = 8 * len_a
    sub     x0, x0, x9   // rewind the result walk back to this row's start
    add     x0, x0, #8   // ...then step it to the next row's start

    subs    x4, x4, #1   // rows left--
    b.eq    L(generic_arm64_end)

.p2align 4
L(generic_arm64_outer_loop_start):

    ldr     x5, [x3], #8   // x5 = b[j], b walk advances
    mov     x7, x1         // a walk = a base
    mov     x6, xzr        // carry = 0

    cmp     x7, x8
    b.eq    L(generic_arm64_inner_loop_4x)

L(generic_arm64_inner_loop_rmdr):

    GENERIC_ARM64_ADDMUL_LIMB

    cmp     x7, x8
    b.ne    L(generic_arm64_inner_loop_rmdr)

L(generic_arm64_inner_loop_4x):

    cmp     x7, x2
    b.eq    L(generic_arm64_outer_loop_end)

.p2align 4
L(generic_arm64_inner_loop_4x_unroll):

    GENERIC_ARM64_ADDMUL_BLOCK4

    cmp     x7, x2
    b.ne    L(generic_arm64_inner_loop_4x_unroll)

L(generic_arm64_outer_loop_end):

    str     x6, [x0]        // result[len_a + j] = carry

    sub     x9, x2, x1
    sub     x0, x0, x9
    add     x0, x0, #8

    subs    x4, x4, #1
    b.ne    L(generic_arm64_outer_loop_start)

L(generic_arm64_end):

    ret

.cfi_endproc
#if defined(__ELF__)
    .size SYM(beman_big_int_multiply_long_runtime), .-SYM(beman_big_int_multiply_long_runtime)

    // Empty, but present, GNU-stack section
    .section .note.GNU-stack,"",%progbits
#endif
