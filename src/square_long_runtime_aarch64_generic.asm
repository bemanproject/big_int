; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

    AREA |.text|, CODE, READONLY, ALIGN=4, CODEALIGN

; Schoolbook squaring, baseline AArch64 only (ARMv8.0-A A64; no NEON/SVE/LSE).

    EXPORT beman_big_int_square_long_runtime

;   x0 -> p_result
;   x1 -> p_a
;   x2 -> len_a
;
; Writes exactly 2 * len_a limbs, so p_result need not be pre-zeroed (matching
; multiply_long_runtime). p_result must not alias p_a.
;
; Leaf function: only x0-x17 are touched, never x18 or x19-x30/sp, so there is
; no prologue, no stack, and no FRAME directive or unwind data are needed.
;
; AAPCS64, Apple and Windows ARM64 all pass the three arguments in x0-x2, so
; this body is register-for-register identical to the GNU-syntax .S version
; (square_long_runtime_aarch64_generic.S) -- unlike x86_64, no argument
; shuffling prologue is needed.
;
; a^2 = D + 2 * T, where D = sum a[i]^2 * B^(2i) is the diagonal and
; T = sum_{i < j} a[i] * a[j] * B^(i + j) the off-diagonal triangle. The first
; phase builds T with one row per a[i], the same mul/addmul rows as
; multiply_long_runtime but each only as long as a[i + 1 .. len_a - 1]: half
; the widening multiplies of a general product. The second phase is a single
; pass that doubles T and adds D.
;
; Register allocation while building the triangle:
;
;   x0 -> result walk: the next limb to read/write within the current row
;   x1 -> &a[i], the row's multiplier pointer; the ldr below advances it to
;         &a[i + 1] before the row starts, which is also where the row's
;         inner scan begins
;   x2 -> a_end = a + 8 * len_a, fixed
;   x3 -> a, fixed (kept for the diagonal pass)
;   x4 -> next row's start in result, result + 8 * (2i + 1); advances by 16
;         (two limbs) per row, since row i + 1 starts two result limbs and
;         one a limb further on than row i
;   x5 -> a[i], the row's multiplier
;   x6 -> the row's running carry
;   x7 -> a walk: the row's inner scan pointer over a[i + 1 .. len_a - 1]
;   x8 -> the row's 4x boundary: x7's starting value plus (row_len & 3) limbs
;   x9-x17 -> temporaries (widened products, loaded/stored result limbs)
;
; Row i multiplies a[i] into a[j] for j = i + 1 .. len_a - 1 and accumulates
; at result[i + j]. Both x0 and x7 already start the row offset to j = i + 1
; and advance one limb together, so every access is a plain [ptr] or a
; post-indexed [ptr], #n -- no per-row rewind is needed, unlike a fixed-length
; multiply row.
;
; Row 0 stores instead of accumulating, writing result[1 .. len_a]. Each
; later row starts one limb further on in both a and result and ends one limb
; further on, so its accumulating span is always already written and its
; carry-out slot never is: no limb of the triangle needs pre-zeroing.

; One remainder limb of row 0: result[i + j] = a[j] * a[i] + carry.
    MACRO
GENERIC_ARM64_SQR_MUL_LIMB
    ldr     x9, [x7], #8       ; x9 = a[j], then advance the a walk
    mul     x10, x9, x5        ; x10 = lo(a[j] * a[i])
    umulh   x11, x9, x5        ; x11 = hi(a[j] * a[i])
    adds    x10, x10, x6       ; lo += carry
    adc     x6, x11, xzr       ; carry = hi + carry_flag
    str     x10, [x0], #8      ; result[i + j] = lo, then advance the result walk
    MEND

; One remainder limb of a later row: result[i + j] += a[j] * a[i] + carry.
;
; Same ordering as multiply_long_runtime: the stored product goes in first
; and the incoming carry last, which leaves just the final adds/adc pair
; loop-carried.
    MACRO
GENERIC_ARM64_SQR_ADDMUL_LIMB
    ldr     x9, [x7], #8       ; x9 = a[j], then advance the a walk
    ldr     x12, [x0]          ; x12 = result[i + j]
    mul     x10, x9, x5        ; x10 = lo(a[j] * a[i])
    umulh   x11, x9, x5        ; x11 = hi(a[j] * a[i])
    adds    x10, x10, x12      ; lo += result[i + j]
    adc     x11, x11, xzr      ; hi += carry_flag
    adds    x10, x10, x6       ; lo += carry
    adc     x6, x11, xzr       ; carry = hi + carry_flag
    str     x10, [x0], #8      ; result[i + j] = lo, then advance the result walk
    MEND

; Four limbs of row 0. No result limb is read: A * a[i] + carry < B^5 across
; the block, so the closing adc below never carries out.
    MACRO
GENERIC_ARM64_SQR_MUL_4X
    ldp     x9, x10, [x7], #16    ; a0, a1
    ldp     x11, x12, [x7], #16   ; a2, a3

    mul     x13, x9, x5            ; l0 = lo(a0 * a[i])
    mul     x14, x10, x5           ; l1 = lo(a1 * a[i])
    mul     x15, x11, x5           ; l2 = lo(a2 * a[i])
    mul     x16, x12, x5           ; l3 = lo(a3 * a[i])

    umulh   x9, x9, x5             ; h0 = hi(a0 * a[i]), overwriting a0
    umulh   x10, x10, x5           ; h1 = hi(a1 * a[i]), overwriting a1
    umulh   x11, x11, x5           ; h2 = hi(a2 * a[i]), overwriting a2
    umulh   x12, x12, x5           ; h3 = hi(a3 * a[i]), overwriting a3

    adds    x13, x13, x6           ; l0 += carry
    adcs    x14, x14, x9           ; l1 += h0 + carry_flag
    adcs    x15, x15, x10          ; l2 += h1 + carry_flag
    adcs    x16, x16, x11          ; l3 += h2 + carry_flag
    adc     x6, x12, xzr           ; carry = h3 + carry_flag

    stp     x13, x14, [x0], #16
    stp     x15, x16, [x0], #16
    MEND

; Four limbs of a later row: result[i + j .. i + j + 3] += a[j .. j + 3] * a[i] + carry.
;
; Chain A (mul, not loop-carried) folds each product's low half into the
; loaded result limb as soon as it is ready; chain B (umulh, loop-carried)
; folds in the high halves and the incoming carry last. R + A * b + c across
; the 4-limb block stays below B^5, so the closing adc's carry-out never
; exceeds one limb: h3 + t + carry_flag always fits in x6.
    MACRO
GENERIC_ARM64_SQR_ADDMUL_4X
    ldp     x9, x10, [x7], #16    ; a0, a1
    ldp     x11, x12, [x7], #16   ; a2, a3
    ldp     x13, x14, [x0]        ; r0, r1
    ldp     x15, x16, [x0, #16]   ; r2, r3

    mul     x17, x9, x5            ; t = lo(a0 * a[i])
    adds    x13, x13, x17          ; r0 += t
    mul     x17, x10, x5           ; t = lo(a1 * a[i])
    adcs    x14, x14, x17          ; r1 += t + carry_flag
    mul     x17, x11, x5           ; t = lo(a2 * a[i])
    adcs    x15, x15, x17          ; r2 += t + carry_flag
    mul     x17, x12, x5           ; t = lo(a3 * a[i])
    adcs    x16, x16, x17          ; r3 += t + carry_flag
    adc     x17, xzr, xzr          ; t = carry_flag (0 or 1)

    umulh   x9, x9, x5             ; h0 = hi(a0 * a[i]), overwriting a0
    umulh   x10, x10, x5           ; h1 = hi(a1 * a[i]), overwriting a1
    umulh   x11, x11, x5           ; h2 = hi(a2 * a[i]), overwriting a2
    umulh   x12, x12, x5           ; h3 = hi(a3 * a[i]), overwriting a3

    adds    x13, x13, x6           ; r0 += carry
    adcs    x14, x14, x9           ; r1 += h0 + carry_flag
    adcs    x15, x15, x10          ; r2 += h1 + carry_flag
    adcs    x16, x16, x11          ; r3 += h2 + carry_flag
    adc     x6, x12, x17           ; carry = h3 + carry_flag + t

    stp     x13, x14, [x0], #16
    stp     x15, x16, [x0], #16
    MEND

; Per-row setup: index bounds, multiplier and carry for the row that starts
; at a[i] (via x1) and result[2i + 1] (via x4).
    MACRO
GENERIC_ARM64_SQR_ROW_SETUP
    ldr     x5, [x1], #8       ; x5 = a[i], then advance the multiplier pointer to &a[i + 1]
    mov     x7, x1             ; a walk starts at a[i + 1]
    mov     x0, x4             ; result walk starts at this row's first limb
    mov     x6, xzr            ; carry = 0

    sub     x9, x2, x7         ; bytes left in the row = 8 * row_len
    and     x9, x9, #24        ; bytes of remainder = 8 * (row_len & 3)
    add     x8, x7, x9         ; x8 = where the 4x unrolled body takes over
    MEND

beman_big_int_square_long_runtime PROC

    cmp     x2, #1
    bls     generic_arm64_sqr_tiny   ; with at most one limb there is no triangle

    add     x2, x1, x2, lsl #3   ; x2 = a_end = a + 8 * len_a
    mov     x3, x1               ; a base, kept for the diagonal pass
    add     x4, x0, #8           ; row 0 starts at result + 8 * (2 * 0 + 1)

    GENERIC_ARM64_SQR_ROW_SETUP

    cmp     x7, x8
    beq     generic_arm64_sqr_mul_row_4x

generic_arm64_sqr_mul_row_rmdr

    GENERIC_ARM64_SQR_MUL_LIMB

    cmp     x7, x8
    bne     generic_arm64_sqr_mul_row_rmdr   ; if (a_walk != row_4x_boundary) { goto rmdr_loop_start; }

generic_arm64_sqr_mul_row_4x

    cmp     x7, x2
    beq     generic_arm64_sqr_mul_row_end    ; remainder alone covered the whole row

    ALIGN   16
generic_arm64_sqr_mul_row_4x_unroll

    GENERIC_ARM64_SQR_MUL_4X

    cmp     x7, x2
    bne     generic_arm64_sqr_mul_row_4x_unroll   ; if (a_walk != a_end) { goto unroll_loop_start; }

generic_arm64_sqr_mul_row_end

    str     x6, [x0]      ; result[len_a] = carry, row 0's carry-out slot
    add     x4, x4, #16   ; next row starts one limb further in a, two further in result

    sub     x9, x2, x1
    cmp     x9, #8
    bls     generic_arm64_sqr_diag   ; len_a == 2: row 0 was the only row

    ALIGN   16
generic_arm64_sqr_outer_loop_start

    GENERIC_ARM64_SQR_ROW_SETUP

    cmp     x7, x8
    beq     generic_arm64_sqr_inner_loop_4x

generic_arm64_sqr_inner_loop_rmdr

    GENERIC_ARM64_SQR_ADDMUL_LIMB

    cmp     x7, x8
    bne     generic_arm64_sqr_inner_loop_rmdr   ; if (a_walk != row_4x_boundary) { goto rmdr_loop_start; }

generic_arm64_sqr_inner_loop_4x

    cmp     x7, x2
    beq     generic_arm64_sqr_outer_loop_end    ; remainder alone covered the whole row

    ALIGN   16
generic_arm64_sqr_inner_loop_4x_unroll

    GENERIC_ARM64_SQR_ADDMUL_4X

    cmp     x7, x2
    bne     generic_arm64_sqr_inner_loop_4x_unroll   ; if (a_walk != a_end) { goto unroll_loop_start; }

generic_arm64_sqr_outer_loop_end

    str     x6, [x0]      ; result[i + len_a] = carry, this row's carry-out slot
    add     x4, x4, #16   ; next row starts one limb further in a, two further in result

    sub     x9, x2, x1
    cmp     x9, #8
    bhi     generic_arm64_sqr_outer_loop_start   ; if (a_end - &a[i + 1] > 8) { goto outer_loop_start; }

generic_arm64_sqr_diag

    ; The triangle occupies result[1 .. 2 * len_a - 2]; zero its two missing
    ; end limbs so the pass below can treat every limb pair alike. x4 is now
    ; exactly result + 8 * (2 * len_a - 1).

    str     xzr, [x4]           ; result[2 * len_a - 1] = 0

    sub     x9, x2, x3          ; x9 = 8 * len_a
    sub     x0, x4, x9, lsl #1  ; x0 = x4 - 16 * len_a
    add     x0, x0, #8          ; x0 = result, the diagonal pass's result-pair walk
    str     xzr, [x0]           ; result[0] = 0

    mov     x6, xzr             ; extra = 0

    ; Limb pair (2i, 2i + 1) becomes a[i]^2 + 2 * T[2i + 1 : 2i] + extra, where
    ; extra (at most 2) carries the bit doubling shifts out of T[2i + 1] plus
    ; the carry out of the pair's sum. a[i]^2 + 2 * T[2i + 1 : 2i] + 2 < 2^129,
    ; so that carry out is at most 1. As in the rows, extra is added last so
    ; only one three-instruction chain is loop-carried.

    ALIGN   16
generic_arm64_sqr_diag_loop

    ldr     x5, [x3], #8         ; x5 = a[i], then advance the a walk

    mul     x9, x5, x5           ; x9  = lo(a[i]^2)
    umulh   x10, x5, x5          ; x10 = hi(a[i]^2)

    ldp     x11, x12, [x0]       ; x11 = T[2i], x12 = T[2i + 1]

    lsr     x13, x12, #63        ; x13 = bit shifted out of T[2i + 1] by doubling
    extr    x12, x12, x11, #63   ; T[2i + 1] doubled, with the carry-in from T[2i]'s top bit
    lsl     x11, x11, #1         ; T[2i] doubled

    adds    x9, x9, x11          ; pair_lo += 2 * T[2i]
    adcs    x10, x10, x12        ; pair_hi += 2 * T[2i + 1] + carry_flag
    adc     x13, x13, xzr        ; extra_out += carry_flag

    adds    x9, x9, x6           ; pair_lo += extra
    adcs    x10, x10, xzr        ; pair_hi += carry_flag
    adc     x13, x13, xzr        ; extra_out += carry_flag

    stp     x9, x10, [x0], #16   ; result[2i .. 2i + 1] = pair, then advance
    mov     x6, x13              ; extra = extra_out

    cmp     x3, x2
    bne     generic_arm64_sqr_diag_loop   ; if (a_walk != a_end) { goto diag_loop_start; }

    b       generic_arm64_sqr_end

generic_arm64_sqr_tiny

    cbz     x2, generic_arm64_sqr_end   ; an empty operand leaves nothing to do

    ldr     x9, [x1]          ; x9 = a[0]
    mul     x10, x9, x9       ; lo(a[0]^2)
    umulh   x11, x9, x9       ; hi(a[0]^2)
    stp     x10, x11, [x0]    ; result[0 .. 1] = a[0]^2

generic_arm64_sqr_end

    ret

    ENDP

    END
