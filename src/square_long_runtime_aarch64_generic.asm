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
; a^2 = D + 2 * T, where D = sum a[i]^2 * B^(2i) is the diagonal and
; T = sum_{i < j} a[i] * a[j] * B^(i + j) the off-diagonal triangle. The first
; phase builds T with one row per a[i], the same mul/addmul rows as
; multiply_long_runtime but each only as long as a[i + 1 .. len_a - 1]: half
; the widening multiplies of a general product. The second phase is a single
; pass that doubles T and adds D.
;
; Register allocation while building the triangle, solo-row path:
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
;
; This solo-row code is used directly for small operands (see the row-pair
; size gate below); the row-pair path is a separate PROC (so this entry
; never adjusts sp) that finishes the WHOLE computation itself for
; the sizes it handles (see that PROC's own comment), so this leaf never has
; anything branched back into it from there. Leaf: only x0-x17 are touched,
; no stack, no calls.

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

; ---------------------------------------------------------------------
; Row-pair path: triangle rows i and i + 1 run interleaved, for the same
; reason and in the same shape as multiply_long_runtime's row pairs (see
; that file for the full derivation): row A (row i) is the plain solo-row
; code above, unchanged, on its usual registers; row B (row i + 1) runs on
; a second register set via the two ROWB macros below, so that one row's
; carry chain and load-use latency can be hidden behind the other's
; independent work, with the two rows' shared result accesses arranged to
; hit the same 4-limb window (forwarding-friendly) rather than straddling
; it.
;
; Row i has length len_i = len_a - 1 - i and starts at a[i + 1], result[2i
; + 1]; row i + 1 has length len_i - 1 (one shorter) and starts at a[i + 2],
; result[2i + 3] -- two result limbs further on, not one, since each row
; advances the result base by two limbs, not one, unlike multiply's equal-
; length rows. Both rows still scan to the SAME a_end, so row B's natural
; block index k needs shifting back by 2 (not multiply's 1) to land on row
; A's block boundaries: row B's block m covers a-relative index
; [4m - 2, 4m + 1], giving a row B prefix of (len_i & 3) + 2 single-limb
; elements (row A's own remainder, plus the 2 elements block 0 would have
; covered) before the first full paired block.
;
; Row lengths shrink going down the triangle, so this path is a prefix of
; the triangle: it keeps pairing rows (i, i + 1) as long as row i has at
; least two 4-limb blocks, then finishes the rest of the computation itself
; (see the PROC below), instead of handing off to the solo-row path above:
; Windows ARM64 unwind data only describes a prolog at the start of a
; function, so once this path has moved sp and saved x19-x26, control can
; only return to the leaf entry's unwind region by fully unwinding this
; PROC first, never by branching into the middle of it.
;
;   Row A (row i):     rp=x0,  ap=x7,  mult=x5,  carry=x6  (solo-row registers)
;   Row B (row i + 1): rp=x19, ap=x20, mult=x21, carry=x22
;   Fixed across the whole call: a_end=x2, a base=x3, row mult walk=x1
;     (shared with the solo-row path: it always points at the next
;     unconsumed row's multiplier), next pair's result base=x4
;   Per pair: row A's 4x boundary=x8, row B's prefix count=x25 (x26 counts
;     it down)
;   Temporaries x9-x17, shared by every row A and row B macro invocation.
; ---------------------------------------------------------------------

    MACRO
GENERIC_ARM64_SQR_ADDMUL_LIMB_ROWB $rp, $ap, $mult, $carry
    ldr     x9, [$ap], #8
    ldr     x12, [$rp]
    mul     x10, x9, $mult
    umulh   x11, x9, $mult
    adds    x10, x10, x12
    adc     x11, x11, xzr
    adds    x10, x10, $carry
    adc     $carry, x11, xzr
    str     x10, [$rp], #8
    MEND

    MACRO
GENERIC_ARM64_SQR_ADDMUL_BLOCK4_ROWB $rp, $ap, $mult, $carry
    ldp     x9, x10, [$ap], #16
    ldp     x11, x12, [$ap], #16
    ldp     x13, x14, [$rp]
    ldp     x15, x16, [$rp, #16]

    mul     x17, x9, $mult
    adds    x13, x13, x17
    mul     x17, x10, $mult
    adcs    x14, x14, x17
    mul     x17, x11, $mult
    adcs    x15, x15, x17
    mul     x17, x12, $mult
    adcs    x16, x16, x17
    adc     x17, xzr, xzr

    umulh   x9, x9, $mult
    umulh   x10, x10, $mult
    umulh   x11, x11, $mult
    umulh   x12, x12, $mult

    adds    x13, x13, $carry
    adcs    x14, x14, x9
    adcs    x15, x15, x10
    adcs    x16, x16, x11
    adc     $carry, x12, x17

    stp     x13, x14, [$rp], #16
    stp     x15, x16, [$rp], #16
    MEND

beman_big_int_square_long_runtime PROC

    cmp     x2, #1
    bls     generic_arm64_sqr_tiny   ; with at most one limb there is no triangle

    add     x2, x1, x2, lsl #3   ; x2 = a_end = a + 8 * len_a
    mov     x3, x1               ; a base, kept for the diagonal pass
    add     x4, x0, #8           ; row 0 starts at result + 8 * (2 * 0 + 1)

    ; Row-pair gate: pairing row 0 needs its length (len_a - 1) to have at
    ; least two 4-limb blocks, i.e. len_a - 1 >= 8, i.e. len_a >= 9. Below
    ; that, the whole triangle runs through the solo-row path below,
    ; unchanged; otherwise, tail-branch to the row-pair PROC, which finishes
    ; the entire computation itself.

    sub     x9, x2, x1
    cmp     x9, #72
    bhs     generic_arm64_square_row_pair

generic_arm64_sqr_solo_path

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

; ---------------------------------------------------------------------
; Row-pair path (see the macro block comment above for the design), split
; into its own PROC so its stack use gets its own unwind region. Reached
; only by the plain "b" above (never "bl"), so x30 still holds the original
; caller's return address throughout; the epilogue's "ret" returns straight
; to it. Once row lengths drop below the pairing threshold, this PROC
; finishes the computation itself -- the outer loop over the remaining
; rows and the diagonal pass, both duplicated from the solo-row path above
; under renamed labels -- rather than branching back into the leaf entry's
; unwind region with sp still adjusted.
;
; No explicit unwind directives are emitted for the prologue below.
; ---------------------------------------------------------------------

generic_arm64_square_row_pair PROC

    stp     x19, x20, [sp, #-64]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]

    ; ---- Pair 0: row A is row 0, so it stores instead of accumulating. ----

    ldr     x5, [x1], #8    ; row A multiplier = a[0]
    mov     x7, x1          ; row A a-walk = a[1]
    mov     x6, xzr
    mov     x0, x4          ; row A result walk = this pair's result base

    sub     x9, x2, x7
    and     x9, x9, #24
    add     x8, x7, x9       ; row A's 4x boundary
    lsr     x25, x9, #3
    add     x25, x25, #2     ; row B's prefix length = row A's remainder + 2

    cmp     x7, x8
    beq     generic_arm64_sqr_pair0_mul_4x

generic_arm64_sqr_pair0_mul_rmdr
    GENERIC_ARM64_SQR_MUL_LIMB
    cmp     x7, x8
    bne     generic_arm64_sqr_pair0_mul_rmdr

generic_arm64_sqr_pair0_mul_4x
    GENERIC_ARM64_SQR_MUL_4X    ; row A block 0
    GENERIC_ARM64_SQR_MUL_4X    ; row A block 1 (guaranteed to exist)

    ldr     x21, [x1], #8       ; row B multiplier = a[1]
    mov     x20, x1             ; row B a-walk = a[2]
    add     x19, x4, #16        ; row B result walk = pair base + 2 limbs
    mov     x22, xzr
    mov     x26, x25

generic_arm64_sqr_pair0_rowb_prefix
    GENERIC_ARM64_SQR_ADDMUL_LIMB_ROWB x19, x20, x21, x22
    subs    x26, x26, #1
    bne     generic_arm64_sqr_pair0_rowb_prefix

    cmp     x7, x2
    beq     generic_arm64_sqr_pair0_tail

generic_arm64_sqr_pair0_steady
    GENERIC_ARM64_SQR_MUL_4X
    GENERIC_ARM64_SQR_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22
    cmp     x7, x2
    bne     generic_arm64_sqr_pair0_steady

generic_arm64_sqr_pair0_tail
    str     x6, [x0]                                          ; row A's carry
    GENERIC_ARM64_SQR_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22    ; row B's last block
    GENERIC_ARM64_SQR_ADDMUL_LIMB_ROWB   x19, x20, x21, x22    ; row B's leftover element
    str     x22, [x19]           ; row B's carry

    add     x4, x4, #32          ; two rows on: next pair's result base

    ; ---- Every later pair: both rows accumulate. ----

generic_arm64_sqr_pair_check
    sub     x9, x2, x1
    cmp     x9, #72
    blo     generic_arm64_sqr_pair_done    ; not enough left for another pair

    ldr     x5, [x1], #8
    mov     x7, x1
    mov     x6, xzr
    mov     x0, x4

    sub     x9, x2, x7
    and     x9, x9, #24
    add     x8, x7, x9
    lsr     x25, x9, #3
    add     x25, x25, #2

    cmp     x7, x8
    beq     generic_arm64_sqr_pair_addmul_4x

generic_arm64_sqr_pair_addmul_rmdr
    GENERIC_ARM64_SQR_ADDMUL_LIMB
    cmp     x7, x8
    bne     generic_arm64_sqr_pair_addmul_rmdr

generic_arm64_sqr_pair_addmul_4x
    GENERIC_ARM64_SQR_ADDMUL_4X
    GENERIC_ARM64_SQR_ADDMUL_4X

    ldr     x21, [x1], #8
    mov     x20, x1
    add     x19, x4, #16
    mov     x22, xzr
    mov     x26, x25

generic_arm64_sqr_pair_rowb_prefix
    GENERIC_ARM64_SQR_ADDMUL_LIMB_ROWB x19, x20, x21, x22
    subs    x26, x26, #1
    bne     generic_arm64_sqr_pair_rowb_prefix

    cmp     x7, x2
    beq     generic_arm64_sqr_pair_tail

generic_arm64_sqr_pair_steady
    GENERIC_ARM64_SQR_ADDMUL_4X
    GENERIC_ARM64_SQR_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22
    cmp     x7, x2
    bne     generic_arm64_sqr_pair_steady

generic_arm64_sqr_pair_tail
    str     x6, [x0]
    GENERIC_ARM64_SQR_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22
    GENERIC_ARM64_SQR_ADDMUL_LIMB_ROWB   x19, x20, x21, x22
    str     x22, [x19]

    add     x4, x4, #32
    b       generic_arm64_sqr_pair_check

generic_arm64_sqr_pair_done

    ; Finish whatever rows are left (always at least a few whole rows; see
    ; the gate in the leaf entry) with the same outer loop the solo-row path
    ; uses, then the same diagonal pass, both duplicated here under local
    ; labels: x1 and x4 already point at the next row's multiplier and
    ; result base, exactly as that loop leaves them between its own rows.

    ALIGN   16
generic_arm64_sqr_pairtail_outer_loop

    GENERIC_ARM64_SQR_ROW_SETUP

    cmp     x7, x8
    beq     generic_arm64_sqr_pairtail_inner_4x

generic_arm64_sqr_pairtail_inner_rmdr

    GENERIC_ARM64_SQR_ADDMUL_LIMB

    cmp     x7, x8
    bne     generic_arm64_sqr_pairtail_inner_rmdr

generic_arm64_sqr_pairtail_inner_4x

    cmp     x7, x2
    beq     generic_arm64_sqr_pairtail_outer_end

    ALIGN   16
generic_arm64_sqr_pairtail_inner_unroll

    GENERIC_ARM64_SQR_ADDMUL_4X

    cmp     x7, x2
    bne     generic_arm64_sqr_pairtail_inner_unroll

generic_arm64_sqr_pairtail_outer_end

    str     x6, [x0]
    add     x4, x4, #16

    sub     x9, x2, x1
    cmp     x9, #8
    bhi     generic_arm64_sqr_pairtail_outer_loop

generic_arm64_sqr_pairtail_diag

    str     xzr, [x4]

    sub     x9, x2, x3
    sub     x0, x4, x9, lsl #1
    add     x0, x0, #8
    str     xzr, [x0]

    mov     x6, xzr

    ALIGN   16
generic_arm64_sqr_pairtail_diag_loop

    ldr     x5, [x3], #8

    mul     x9, x5, x5
    umulh   x10, x5, x5

    ldp     x11, x12, [x0]

    lsr     x13, x12, #63
    extr    x12, x12, x11, #63
    lsl     x11, x11, #1

    adds    x9, x9, x11
    adcs    x10, x10, x12
    adc     x13, x13, xzr

    adds    x9, x9, x6
    adcs    x10, x10, xzr
    adc     x13, x13, xzr

    stp     x9, x10, [x0], #16
    mov     x6, x13

    cmp     x3, x2
    bne     generic_arm64_sqr_pairtail_diag_loop

generic_arm64_sqr_pair_end

    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x19, x20, [sp], #64
    ret

    ENDP

    END
