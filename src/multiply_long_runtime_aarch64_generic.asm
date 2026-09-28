; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

; Schoolbook long multiplication, baseline AArch64 only (ARMv8.0-A A64; no NEON/SVE/LSE).

    AREA |.text|, CODE, READONLY, ALIGN=4, CODEALIGN

    EXPORT beman_big_int_multiply_long_runtime

;   x0 -> p_result
;   x1 -> p_a
;   x2 -> len_a
;   x3 -> p_b
;   x4 -> len_b
;
; Writes exactly len_a + len_b limbs. The first row stores instead of
; accumulating, so p_result need not be pre-zeroed (matching multiply_long).
; Writes nothing when either length is 0.
;
; Register allocation once the prologue has run:
;
;   x0 -> result walk: advances one limb per inner-loop step, then gets
;         rewound to result + j + 1 (the next row's start) at row end
;   x1 -> a base, fixed; reloaded into x7 at the start of every row
;   x2 -> a_end = a + 8 * len_a, fixed
;   x3 -> b walk, post-incremented by one limb at the start of every row
;   x4 -> rows left to do, starts as len_b and counts down to 0
;   x5 -> b[j], the row's multiplier
;   x6 -> the row's running carry
;   x7 -> a walk, from a base to a_end over the course of a row
;   x8 -> a + 8 * (len_a & 3), fixed: the boundary between the scalar
;         remainder loop and the 4x unrolled body
;   x9-x17 -> temporaries, reused by every macro below
;
; Both walking pointers (x0 and x7) start each row at the row's base and
; advance in lockstep, so no index has to be rescaled between rows: only
; x0 needs an explicit rewind (row length back, then one limb forward)
; once a row's carry has been stored.
;
; This is the SOLO-ROW path: one row at a time, exactly as before. It is
; used directly for tiny operands (see the size gate below); the row-pair
; path below (a separate PROC, so this entry never adjusts sp) never
; branches back into it, so it stays a plain frameless leaf: only x0-x17,
; no stack, no calls.

; One limb of the first row: result[i] = a[i] * b[0] + carry.
    MACRO
GENERIC_ARM64_MUL_LIMB
    ldr     x9, [x7], #8    ; x9 = a[i], a walk advances
    mul     x11, x9, x5     ; x11 = low64(a[i] * b[j])
    umulh   x12, x9, x5     ; x12 = high64(a[i] * b[j])

    adds    x11, x11, x6    ; low64 += carry
    adc     x6, x12, xzr    ; carry = high64 + carry_flag

    str     x11, [x0], #8   ; result[i] = low64, result walk advances
    MEND

; One limb of a later row: result[i + j] += a[i] * b[j] + carry.
;
; The load of the stored limb goes in before the carry, mirroring the x64
; "stored limb first, incoming carry last" ordering: a[i] * b[j] + result[i + j]
; + carry is at most 2^128 - 1 (single-limb induction: carry <= limb_max), so
; neither adc can carry out of high64.
    MACRO
GENERIC_ARM64_ADDMUL_LIMB
    ldr     x9, [x7], #8    ; x9 = a[i], a walk advances
    ldr     x10, [x0]       ; x10 = result[i + j]
    mul     x11, x9, x5     ; x11 = low64(a[i] * b[j])
    umulh   x12, x9, x5     ; x12 = high64(a[i] * b[j])

    adds    x11, x11, x10   ; low64 += result[i + j]
    adc     x12, x12, xzr   ; high64 += carry_flag
    adds    x11, x11, x6    ; low64 += carry
    adc     x6, x12, xzr    ; carry = high64 + carry_flag

    str     x11, [x0], #8   ; result[i + j] = low64, result walk advances
    MEND

; Four limbs of the first row, taking the place of GENERIC_ARM64_MUL_LIMB run
; four times. There is no stored result to fold in, so the low-half sum
; (chain A on the accumulating version below) is never loop-carried here:
; each l_k is simply lo_k, and the shifted high halves plus the incoming
; carry are added in one pass (chain B), closed with xzr in place of a
; chain-A carry that cannot occur.
    MACRO
GENERIC_ARM64_MUL_BLOCK4
    ldp     x9, x10, [x7], #16    ; x9, x10  = a[i], a[i+1]
    ldp     x11, x12, [x7], #16   ; x11, x12 = a[i+2], a[i+3]; a walk advances by 32

    mul     x13, x9, x5           ; x13 = lo(a[i]   * b[j])
    mul     x14, x10, x5          ; x14 = lo(a[i+1] * b[j])
    mul     x15, x11, x5          ; x15 = lo(a[i+2] * b[j])
    mul     x16, x12, x5          ; x16 = lo(a[i+3] * b[j])

    umulh   x9, x9, x5            ; x9  = hi(a[i]   * b[j])
    umulh   x10, x10, x5          ; x10 = hi(a[i+1] * b[j])
    umulh   x11, x11, x5          ; x11 = hi(a[i+2] * b[j])
    umulh   x12, x12, x5          ; x12 = hi(a[i+3] * b[j])

    adds    x13, x13, x6          ; l0 += carry
    adcs    x14, x14, x9          ; l1 += hi0 + carry_flag
    adcs    x15, x15, x10         ; l2 += hi1 + carry_flag
    adcs    x16, x16, x11         ; l3 += hi2 + carry_flag
    adc     x6, x12, xzr          ; carry = hi3 + carry_flag

    stp     x13, x14, [x0], #16   ; result[i .. i+1]   = l0, l1
    stp     x15, x16, [x0], #16   ; result[i+2 .. i+3] = l2, l3; result walk advances by 32
    MEND

; Four limbs of a later row, taking the place of GENERIC_ARM64_ADDMUL_LIMB run
; four times.
;
; Chain A folds the four low halves into the stored result limbs r0..r3
; (a plain 4-limb ripple-carry add, independent of the incoming carry),
; closed by capturing its carry-out t in {0, 1}. Chain B then folds in the
; incoming carry at r0 and the shifted high halves at r1..r3, closed by
; adding t. Treating a[i..i+3] as one 4-limb number A and r0..r3 as one
; 4-limb number R, the whole block computes R + A * b[j] + carry, which is
; at most B^5 - 1 for B = 2^64 (induction: carry <= limb_max), so the new
; carry out of the block (hi3 + t + carry_flag) never exceeds a limb either.
    MACRO
GENERIC_ARM64_ADDMUL_BLOCK4
    ldp     x9, x10, [x7], #16    ; x9, x10  = a[i], a[i+1]
    ldp     x11, x12, [x7], #16   ; x11, x12 = a[i+2], a[i+3]; a walk advances by 32
    ldp     x13, x14, [x0]        ; x13, x14 = result[i], result[i+1]
    ldp     x15, x16, [x0, #16]   ; x15, x16 = result[i+2], result[i+3]

    mul     x17, x9, x5           ; x17 = lo(a[i] * b[j])
    adds    x13, x13, x17         ; r0 += lo0
    mul     x17, x10, x5          ; x17 = lo(a[i+1] * b[j])
    adcs    x14, x14, x17         ; r1 += lo1 + carry_flag
    mul     x17, x11, x5          ; x17 = lo(a[i+2] * b[j])
    adcs    x15, x15, x17         ; r2 += lo2 + carry_flag
    mul     x17, x12, x5          ; x17 = lo(a[i+3] * b[j])
    adcs    x16, x16, x17         ; r3 += lo3 + carry_flag
    adc     x17, xzr, xzr         ; t = carry_flag (0 or 1)

    umulh   x9, x9, x5            ; x9  = hi(a[i]   * b[j])
    umulh   x10, x10, x5          ; x10 = hi(a[i+1] * b[j])
    umulh   x11, x11, x5          ; x11 = hi(a[i+2] * b[j])
    umulh   x12, x12, x5          ; x12 = hi(a[i+3] * b[j])

    adds    x13, x13, x6          ; r0 += carry
    adcs    x14, x14, x9          ; r1 += hi0 + carry_flag
    adcs    x15, x15, x10         ; r2 += hi1 + carry_flag
    adcs    x16, x16, x11         ; r3 += hi2 + carry_flag
    adc     x6, x12, x17          ; carry = hi3 + t + carry_flag

    stp     x13, x14, [x0], #16   ; result[i .. i+1]   = r0, r1
    stp     x15, x16, [x0], #16   ; result[i+2 .. i+3] = r2, r3; result walk advances by 32
    MEND

; ---------------------------------------------------------------------
; Row-pair path: rows j and j+1 run interleaved, each an independent
; ADDMUL_BLOCK4/LIMB carry chain, so the out-of-order engine can hide one
; row's load-use and carry latency behind the other's independent work.
;
; Row j ("row A") is always the plain solo-row code above, unchanged, on
; its usual registers (x0, x5, x6, x7). Row j+1 ("row B") runs on a second
; register set (x19 result walk, x20 a walk, x21 multiplier, x22 carry) via
; the two macros below, which are byte-for-byte the solo-row ADDMUL bodies
; with the persistent registers parameterized; the temporaries (x9-x17)
; stay hardcoded and are shared with row A's macro invocations that
; surround them; each invocation's temps are dead before the next one
; reuses the names, so register renaming resolves the reuse without
; introducing a real dependency between the two rows.
;
; Row B's own natural 4-limb blocks (its a-walk starts one limb after row
; A's) would straddle two of row A's stp pairs -- a load spanning two
; stores defeats store-to-load forwarding. Shifting row B's block
; boundaries back by one limb fixes this: row B's block m covers
; a[4m - 1 .. 4m + 2], which lands on result[4m .. 4m + 3] -- exactly row
; A's block m stp pair -- instead of straddling row A's blocks m and m + 1.
; Row B's block m is always issued one loop iteration after row A's block m
; (row A's block m + 1 runs in between), so row B never reads anything row
; A has not already committed, and row A never reads anything row B wrote:
; the dependency is one-directional, with a whole iteration's worth of
; independent row-A work to absorb the forwarding latency.
;
; Block index 0 does not exist for row B (a[-1] is invalid); the first
; (len_a & 3) + 3 elements -- row A's own remainder, plus the three
; elements block 0 would have covered -- are instead handled by single-limb
; steps, run once row A has produced that much data (its remainder and its
; first 4-limb block). This prefix length only depends on len_a, so it is
; computed once per call, not once per pair. Symmetrically, row B's last
; block cannot reach the final element of the row; that one element and
; row B's carry are handled after row A is completely done.
;
; This path is only entered once len_a is large enough to guarantee at
; least two 4-limb blocks (see the size gate below), so the "row A block 0,
; then row A block 1, then row B's prefix" sequence and the "at least one
; more row A block after that" checks below never see fewer blocks than
; they assume.
;
;   Row A (row j):   rp=x0,  ap=x7,  mult=x5,  carry=x6  (solo-row registers)
;   Row B (row j+1): rp=x19, ap=x20, mult=x21, carry=x22
;   Fixed across the whole call: a_base=x1, a_end=x2, b walk=x3,
;     len_a in bytes=x23, row B prefix length=x25
;   Per pair: pair result base=x24, row B prefix counter=x26
;   Temporaries x9-x17, shared by every row A and row B macro invocation.
; ---------------------------------------------------------------------

    MACRO
GENERIC_ARM64_ADDMUL_LIMB_ROWB $rp, $ap, $mult, $carry
    ldr     x9, [$ap], #8    ; x9 = a[i], a walk advances
    ldr     x10, [$rp]       ; x10 = result[pos]
    mul     x11, x9, $mult   ; x11 = low64(a[i] * mult)
    umulh   x12, x9, $mult   ; x12 = high64(a[i] * mult)

    adds    x11, x11, x10    ; low64 += result[pos]
    adc     x12, x12, xzr    ; high64 += carry_flag
    adds    x11, x11, $carry ; low64 += carry
    adc     $carry, x12, xzr ; carry = high64 + carry_flag

    str     x11, [$rp], #8   ; result[pos] = low64, result walk advances
    MEND

    MACRO
GENERIC_ARM64_ADDMUL_BLOCK4_ROWB $rp, $ap, $mult, $carry
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

beman_big_int_multiply_long_runtime PROC

    ; a * b is symmetric, so run the inner loop over the longer operand: that
    ; makes the number of rows, and with it all per-row overhead, min(len_a, len_b).

    cmp     x2, x4
    bhs     generic_arm64_sizes_ordered

    mov     x5, x1   ; swap p_a and p_b through x5
    mov     x1, x3
    mov     x3, x5
    mov     x5, x2   ; swap len_a and len_b through x5
    mov     x2, x4
    mov     x4, x5

generic_arm64_sizes_ordered

    cbz     x4, generic_arm64_end   ; an empty length leaves nothing to do

    ; Row pairing needs at least one full row pair, and needs len_a big
    ; enough that row A always has at least two 4-limb blocks (so "block 0,
    ; then block 1, then row B's prefix" and the "any blocks left after
    ; that" check are always valid). Anything smaller falls straight through
    ; into the solo-row path below, unchanged and immediately following, so
    ; tiny operands cost exactly one not-taken branch more than before.
    ; Both conditions are checked here, before the tail call, so the row-pair
    ; PROC below is only ever entered when it will do real pairing.

    cmp     x2, #8
    bhs     generic_arm64_row_pair_check_rows

generic_arm64_solo_path

    and     x9, x2, #3           ; x9 = len_a & 3
    add     x8, x1, x9, lsl #3   ; x8 = a + 8 * (len_a & 3): remainder/4x boundary
    add     x2, x1, x2, lsl #3   ; x2 = a_end = a + 8 * len_a

    ; First row (j = 0) stores instead of accumulating. That drops a load and
    ; an add/adc pair per limb, and it is what lets the caller hand over a
    ; buffer it has not zeroed.

    ldr     x5, [x3], #8   ; x5 = b[0], b walk advances
    mov     x7, x1         ; a walk = a base
    mov     x6, xzr        ; carry = 0

    cmp     x7, x8
    beq     generic_arm64_mul_row_4x

generic_arm64_mul_row_rmdr

    GENERIC_ARM64_MUL_LIMB

    cmp     x7, x8
    bne     generic_arm64_mul_row_rmdr

generic_arm64_mul_row_4x

    cmp     x7, x2
    beq     generic_arm64_mul_row_end

    ALIGN   16
generic_arm64_mul_row_4x_unroll

    GENERIC_ARM64_MUL_BLOCK4

    cmp     x7, x2
    bne     generic_arm64_mul_row_4x_unroll

generic_arm64_mul_row_end

    str     x6, [x0]     ; result[len_a] = carry

    sub     x9, x2, x1   ; x9 = a_end - a_base = 8 * len_a
    sub     x0, x0, x9   ; rewind the result walk back to this row's start
    add     x0, x0, #8   ; ...then step it to the next row's start

    subs    x4, x4, #1   ; rows left--
    beq     generic_arm64_end

    ALIGN   16
generic_arm64_outer_loop_start

    ldr     x5, [x3], #8   ; x5 = b[j], b walk advances
    mov     x7, x1         ; a walk = a base
    mov     x6, xzr        ; carry = 0

    cmp     x7, x8
    beq     generic_arm64_inner_loop_4x

generic_arm64_inner_loop_rmdr

    GENERIC_ARM64_ADDMUL_LIMB

    cmp     x7, x8
    bne     generic_arm64_inner_loop_rmdr

generic_arm64_inner_loop_4x

    cmp     x7, x2
    beq     generic_arm64_outer_loop_end

    ALIGN   16
generic_arm64_inner_loop_4x_unroll

    GENERIC_ARM64_ADDMUL_BLOCK4

    cmp     x7, x2
    bne     generic_arm64_inner_loop_4x_unroll

generic_arm64_outer_loop_end

    str     x6, [x0]        ; result[len_a + j] = carry

    sub     x9, x2, x1
    sub     x0, x0, x9
    add     x0, x0, #8

    subs    x4, x4, #1
    bne     generic_arm64_outer_loop_start

generic_arm64_end

    ret

generic_arm64_row_pair_check_rows

    cmp     x4, #2
    blo     generic_arm64_solo_path

    b       generic_arm64_multiply_row_pair

    ENDP

; ---------------------------------------------------------------------
; Row-pair path (see the macro block comment above for the design), split
; into its own PROC so its stack use gets its own unwind region: Windows
; ARM64 unwind data only describes a prolog at the start of a function, and
; the exported leaf above never adjusts sp. Reached only by the plain "b"
; above (never "bl"), so x30 still holds the original caller's return
; address throughout; the epilogue's "ret" returns straight to it.
;
; No explicit unwind directives are emitted for the prologue below.
; ---------------------------------------------------------------------

generic_arm64_multiply_row_pair PROC

    stp     x19, x20, [sp, #-64]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]

    and     x9, x2, #3            ; x9 = len_a & 3 (row remainder length)
    add     x8, x1, x9, lsl #3    ; x8 = a + 8 * (len_a & 3): remainder/4x boundary
    add     x25, x9, #3           ; x25 = row B's prefix length (constant for every pair)
    lsl     x23, x2, #3           ; x23 = len_a * 8 (bytes), constant
    add     x2, x1, x23           ; x2 = a_end

    ; ---- Pair 0: row A is row 0, so it stores instead of accumulating. ----

    ldr     x5, [x3], #8   ; x5 = b[0]
    ldr     x21, [x3], #8  ; x21 = b[1]
    mov     x24, x0        ; x24 = this pair's result base
    mov     x7, x1         ; row A a-walk = a base
    mov     x6, xzr        ; row A carry = 0

    cmp     x7, x8
    beq     generic_arm64_pair0_mul_4x

generic_arm64_pair0_mul_rmdr
    GENERIC_ARM64_MUL_LIMB
    cmp     x7, x8
    bne     generic_arm64_pair0_mul_rmdr

generic_arm64_pair0_mul_4x
    GENERIC_ARM64_MUL_BLOCK4    ; row A block 0
    GENERIC_ARM64_MUL_BLOCK4    ; row A block 1 (guaranteed to exist)

    mov     x20, x1             ; row B a-walk = a base
    add     x19, x24, #8        ; row B result walk = pair base + 1 limb
    mov     x22, xzr            ; row B carry = 0
    mov     x26, x25            ; row B prefix counter = prefix length

generic_arm64_pair0_rowb_prefix
    GENERIC_ARM64_ADDMUL_LIMB_ROWB x19, x20, x21, x22
    subs    x26, x26, #1
    bne     generic_arm64_pair0_rowb_prefix

    cmp     x7, x2
    beq     generic_arm64_pair0_tail

generic_arm64_pair0_steady
    GENERIC_ARM64_MUL_BLOCK4                             ; row A's next block
    GENERIC_ARM64_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22   ; row B's lagging block
    cmp     x7, x2
    bne     generic_arm64_pair0_steady

generic_arm64_pair0_tail
    str     x6, [x0]                                     ; row A's carry
    GENERIC_ARM64_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22   ; row B's last block
    GENERIC_ARM64_ADDMUL_LIMB_ROWB   x19, x20, x21, x22   ; row B's leftover element
    str     x22, [x19]                                   ; row B's carry

    sub     x0, x0, x23   ; rewind the result walk back to this pair's start...
    add     x0, x0, #16   ; ...then two limbs on, to the next pair's row A base

    subs    x4, x4, #2

    ; ---- Every later pair: both rows accumulate. ----

generic_arm64_pair_check
    cmp     x4, #2
    blo     generic_arm64_pair_leftover

    ldr     x5, [x3], #8
    ldr     x21, [x3], #8
    mov     x24, x0
    mov     x7, x1
    mov     x6, xzr

    cmp     x7, x8
    beq     generic_arm64_pair_addmul_4x

generic_arm64_pair_addmul_rmdr
    GENERIC_ARM64_ADDMUL_LIMB
    cmp     x7, x8
    bne     generic_arm64_pair_addmul_rmdr

generic_arm64_pair_addmul_4x
    GENERIC_ARM64_ADDMUL_BLOCK4    ; row A block 0
    GENERIC_ARM64_ADDMUL_BLOCK4    ; row A block 1

    mov     x20, x1
    add     x19, x24, #8
    mov     x22, xzr
    mov     x26, x25

generic_arm64_pair_rowb_prefix
    GENERIC_ARM64_ADDMUL_LIMB_ROWB x19, x20, x21, x22
    subs    x26, x26, #1
    bne     generic_arm64_pair_rowb_prefix

    cmp     x7, x2
    beq     generic_arm64_pair_tail

generic_arm64_pair_steady
    GENERIC_ARM64_ADDMUL_BLOCK4
    GENERIC_ARM64_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22
    cmp     x7, x2
    bne     generic_arm64_pair_steady

generic_arm64_pair_tail
    str     x6, [x0]
    GENERIC_ARM64_ADDMUL_BLOCK4_ROWB x19, x20, x21, x22
    GENERIC_ARM64_ADDMUL_LIMB_ROWB   x19, x20, x21, x22
    str     x22, [x19]

    sub     x0, x0, x23
    add     x0, x0, #16

    subs    x4, x4, #2
    b       generic_arm64_pair_check

generic_arm64_pair_leftover

    ; len_b was odd: one row left over (never row 0, so it accumulates).
    ; x0/x1/x2/x3/x8 are already exactly where the solo-row loop expects
    ; them, so this is that loop's body for a single row.

    cbz     x4, generic_arm64_pair_done

    ldr     x5, [x3], #8
    mov     x7, x1
    mov     x6, xzr

    cmp     x7, x8
    beq     generic_arm64_pair_leftover_4x

generic_arm64_pair_leftover_rmdr
    GENERIC_ARM64_ADDMUL_LIMB
    cmp     x7, x8
    bne     generic_arm64_pair_leftover_rmdr

generic_arm64_pair_leftover_4x
    cmp     x7, x2
    beq     generic_arm64_pair_leftover_end

generic_arm64_pair_leftover_unroll
    GENERIC_ARM64_ADDMUL_BLOCK4
    cmp     x7, x2
    bne     generic_arm64_pair_leftover_unroll

generic_arm64_pair_leftover_end
    str     x6, [x0]

generic_arm64_pair_done

    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x19, x20, [sp], #64
    ret

    ENDP

    END
