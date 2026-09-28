# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

.intel_syntax noprefix

# Empty, but present, GNU-stack section
.section .note.GNU-stack,"",%progbits

# Enter the text section for code/symbols
.section .text

# Schoolbook long multiplication, baseline x86-64 only (no SSE/AVX, no BMI2/ADX).

.globl beman_big_int_multiply_long_runtime_generic
.type beman_big_int_multiply_long_runtime_generic, @function

#   rdi -> p_result
#   rsi -> p_a
#   rdx -> len_a
#   rcx -> p_b
#   r8  -> len_b
#
# Writes exactly len_a + len_b limbs. The first row stores instead of
# accumulating, so p_result need not be pre-zeroed (matching multiply_long).
#
# Register allocation once the prologue has run:
#
#   rdi -> result + len_a, walking forward one limb per row
#   rsi -> a + len_a, fixed
#   rcx -> b, walking forward one limb per row
#   r8  -> rows left to do
#   r9  -> -len_a, the index each row starts from
#   r10 -> b[j], the row's multiplier
#   r11 -> the row's running carry
#   rbx -> inner loop index, running -len_a .. 0
#   r12 -> -(len_a & ~3), the index where the 4x unrolled body takes over
#
# Both arrays are addressed as [ptr + rbx * 8], so no pointer has to be rewound
# between rows: the index is just reloaded from r9.

# One limb of the first row: result[i] = a[i] * b[0] + carry.
.macro GENERIC_X64_MUL_LIMB off

    mov     rax, QWORD PTR [rsi + rbx * 8 + \off]   # rax = a[i]
    mul     r10                                     # rdx : rax = a[i] * b[j]

    add     rax, r11                                # low64 += temp
    adc     rdx, 0                                  # high64 += carry_flag

    mov     QWORD PTR [rdi + rbx * 8 + \off], rax   # result[i + j] = low64
    mov     r11, rdx                                # temp = high64

.endm

# One limb of a later row: result[i + j] += a[i] * b[j] + carry.
#
# The stored product goes in first and the incoming carry last, which leaves
# just the final add/adc pair loop-carried (two cycles); the widening multiply
# and the load of result[i + j] hang off that chain instead of sitting on it.
# a[i] * b[j] + result[i + j] + temp is at most 2^128 - 1, so neither adc can
# carry out of high64.
.macro GENERIC_X64_ADDMUL_LIMB off

    mov     rax, QWORD PTR [rsi + rbx * 8 + \off]   # rax = a[i]
    mul     r10                                     # rdx : rax = a[i] * b[j]

    add     rax, QWORD PTR [rdi + rbx * 8 + \off]   # low64 += result[i + j]
    adc     rdx, 0                                  # high64 += carry_flag
    add     rax, r11                                # low64 += temp
    adc     rdx, 0                                  # high64 += carry_flag

    mov     QWORD PTR [rdi + rbx * 8 + \off], rax   # result[i + j] = low64
    mov     r11, rdx                                # temp = high64

.endm

beman_big_int_multiply_long_runtime_generic:
.cfi_startproc

    push    rbx
.cfi_adjust_cfa_offset 8
.cfi_rel_offset rbx, 0

    push    r12
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r12, 0

.Lgeneric_x64_start:

    # a * b is symmetric, so run the inner loop over the longer operand: that
    # makes the number of rows, and with it all per-row overhead, min(len_a, len_b).

    cmp     rdx, r8
    jae     .Lgeneric_x64_sizes_ordered

    xchg    rsi, rcx
    xchg    rdx, r8

.Lgeneric_x64_sizes_ordered:

    test    r8, r8      # an empty operand leaves nothing to do
    jz      .Lgeneric_x64_end

    mov     r9, rdx     # free up rdx for the mul instruction

    # now len_a in r9

    lea     rsi, [rsi + r9 * 8]     # rsi = a + len_a
    lea     rdi, [rdi + r9 * 8]     # rdi = result + len_a

    mov     r12, r9
    and     r12, -4     # r12 = len_a & ~3
    neg     r12         # r12 = -(len_a & ~3)
    neg     r9          # r9  = -len_a

    # First row (j = 0) stores instead of accumulating. That drops a load and
    # an add/adc pair per limb, and it is what lets the caller hand over a
    # buffer it has not zeroed.

    mov     r10, QWORD PTR [rcx]    # r10 = b[0]
    mov     rbx, r9                 # i = -len_a
    xor     r11d, r11d              # temp = 0

    cmp     rbx, r12
    je      .Lgeneric_x64_mul_row_4x

.Lgeneric_x64_mul_row_rmdr:

    GENERIC_X64_MUL_LIMB 0

    add     rbx, 1                          # i++
    cmp     rbx, r12
    jne     .Lgeneric_x64_mul_row_rmdr      # if (i != -(len_a & ~3)) { goto rmdr_loop_start; }

.Lgeneric_x64_mul_row_4x:

    test    rbx, rbx
    jz      .Lgeneric_x64_mul_row_end

.p2align 4
.Lgeneric_x64_mul_row_4x_unroll:

    GENERIC_X64_MUL_LIMB 0
    GENERIC_X64_MUL_LIMB 8
    GENERIC_X64_MUL_LIMB 16
    GENERIC_X64_MUL_LIMB 24

    add     rbx, 4                              # i += 4
    jnz     .Lgeneric_x64_mul_row_4x_unroll     # if (i != 0) { goto unroll_loop_start; }

.Lgeneric_x64_mul_row_end:

    mov     QWORD PTR [rdi], r11    # result[len_a] = temp

    add     rdi, 8                  # increment the result ptr for next iter
    add     rcx, 8                  # increment the b ptr for next iter
    sub     r8, 1                   # len_b--
    jz      .Lgeneric_x64_end

.p2align 4
.Lgeneric_x64_outer_loop_start:

    mov     r10, QWORD PTR [rcx]    # r10 = b[j]
    mov     rbx, r9                 # i = -len_a
    xor     r11d, r11d              # temp = 0

    cmp     rbx, r12
    je      .Lgeneric_x64_inner_loop_4x

.Lgeneric_x64_inner_loop_rmdr:

    GENERIC_X64_ADDMUL_LIMB 0

    add     rbx, 1                          # i++
    cmp     rbx, r12
    jne     .Lgeneric_x64_inner_loop_rmdr   # if (i != -(len_a & ~3)) { goto rmdr_loop_start; }

.Lgeneric_x64_inner_loop_4x:

    test    rbx, rbx
    jz      .Lgeneric_x64_outer_loop_end

.p2align 4
.Lgeneric_x64_inner_loop_4x_unroll:

    GENERIC_X64_ADDMUL_LIMB 0
    GENERIC_X64_ADDMUL_LIMB 8
    GENERIC_X64_ADDMUL_LIMB 16
    GENERIC_X64_ADDMUL_LIMB 24

    add     rbx, 4                              # i += 4
    jnz     .Lgeneric_x64_inner_loop_4x_unroll  # if (i != 0) { goto unroll_loop_start; }

.Lgeneric_x64_outer_loop_end:

    mov     QWORD PTR [rdi], r11    # result[len_a + j] = temp

    add     rdi, 8                          # increment the result ptr for next iter
    add     rcx, 8                          # increment the b ptr for next iter
    sub     r8, 1                           # len_b--
    jnz     .Lgeneric_x64_outer_loop_start  # if (len_b != 0) { goto outer_loop_start; }

.Lgeneric_x64_end:

    pop     r12
.cfi_adjust_cfa_offset -8

    pop     rbx
.cfi_adjust_cfa_offset -8
    ret

.cfi_endproc
.size beman_big_int_multiply_long_runtime_generic, .-beman_big_int_multiply_long_runtime_generic
