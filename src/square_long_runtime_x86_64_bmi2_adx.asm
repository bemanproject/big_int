; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

.code

; Schoolbook squaring, x86-64 with BMI2 (mulx) and ADX (adcx/adox). See
; square_long_runtime_x86_64_bmi2_adx.s for the SysV version; this is a
; straight port to the Win64 calling convention.

; Microsoft x64 calling convention: rcx, rdx, r8 for the three arguments
;
;   rcx -> p_result
;   rdx -> p_a
;   r8  -> len_a
;
; The prologue moves the arguments into rdi, rsi and rdx, where the System V
; version receives them, so the body below matches that version register for
; register. Win64 additionally makes rsi and rdi callee-saved (SysV does not,
; since they are argument registers there), so both are saved alongside
; rbx, r12, r13, r14 and r15.
;
; Contract as square_long_runtime_x86_64_generic.asm; requires BMI2 and ADX.
;
; a^2 = D + 2T (T the off-diagonal triangle, D the diagonal squares). Row
; i's multiplier is u'_i = (a[i] << 1) | c_{i-1}, c_i = a[i]'s top bit, so
; its products sum directly to 2T instead of T; the diagonal term
; a[i] * (a[i] + c_{i-1}) is added once at the row's own start
; (DIAG_ROW0/DIAG_OUTER), not in a separate pass. a[i] + c_{i-1} overflows
; 64 bits only when a[i] is all-ones and c_{i-1} = 1: mulx computes the
; wrapped product and cmovc fixes the high half from the pre-overflow CF;
; the low half needs no fix (it is exactly 0 then), which also proves the
; carry this produces cannot itself overflow. The last row has no
; multiplies, only its diagonal term, injected directly after the main
; loop.
;
; Each row runs two ADX chains: CF the previous product's high half, OF
; the accumulated result limb (row 0 only stores, so it skips OF). Row end
; folds CF and OF into the final high half, the row's carry-out, which
; cannot overflow a limb. No flag writes inside a row: `lea` and `jrcxz`
; only. Every hot-loop operand is base + displacement, never indexed, to
; avoid mulx/store un-lamination at rename.
;
; Rows of length >= 4 use a computed entry (BLOCK = 16, see ROW_SETUP)
; into a shared unrolled body, via a table of assemble-time label deltas
; needing no relocation. Lengths 1-3 -- every call's last few rows, and
; the whole triangle for len_a <= 4 -- run straight-line instead.
;
; Register roles: rsi = a + len_a; rdi = result + len_a + i, fixed per
; row; rbx/r14 = a_walk/r_walk; rdx = u'_i; rax = 0 for the row; r8 = row
; length; r9 = c_{i-1} in, c_i out; r15 = the diagonal injection's
; row-start carry; rcx = BLOCK-iteration count; r10 = -row_len (shared by
; the diagonal injection and ROW_SETUP); r11-r13 = alternating (lo, hi)
; pairs.

BLOCK = 16
LOG2_BLOCK = 4

; One limb of a later row: result[i + j] += a[j] * a[i]. The result limb
; is preloaded into scratch (borrowed from the other slot's dead `lo`) to
; keep the load off the flag chain.
ASLOT MACRO off, lo, hi, prev, scratch

    mulx    hi, lo, QWORD PTR [rbx + off]
    mov     scratch, QWORD PTR [r14 + off]
    adcx    lo, prev
    adox    lo, scratch

    mov     QWORD PTR [r14 + off], lo

ENDM

; One limb of row 0: result[i + j] = a[j] * a[i]. Store-only; CF chain
; only, since row 0 has nothing to accumulate onto.
MSLOT MACRO off, lo, hi, prev

    mulx    hi, lo, QWORD PTR [rbx + off]
    adcx    lo, prev

    mov     QWORD PTR [r14 + off], lo

ENDM

; Duff's-device dispatch: given r10 = true_index and rdx = u'_i (set up by
; DIAG_ROW0/DIAG_OUTER), computes entry_index, the a_walk/r_walk start,
; the trip count, and the jump target.
ROW_SETUP MACRO tag

    mov     rax, r10
    and     rax, (BLOCK - 1)          ; rax = entry_index = (-row_len) & (BLOCK - 1)

    sub     r10, rax                  ; r10 = idx_start = true_index - entry_index
    lea     rbx, [rsi + r10 * 8]      ; a_walk start
    lea     r14, [rdi + r10 * 8]      ; r_walk start

    lea     rcx, [r8 + BLOCK - 1]
    shr     rcx, LOG2_BLOCK           ; rcx = trip_count = ceil(row_len / BLOCK)

    lea     r10, entry_table_&tag&
    movsxd  rax, DWORD PTR [r10 + rax * 4]
    add     r10, rax                  ; r10 = jump target address

ENDM

; Straight-line row lengths 1-3: no entry_index adjustment, a_walk/r_walk
; just start at the row's true first limb.
ROW_SETUP_SIMPLE MACRO

    lea     rbx, [rsi + r10 * 8]
    lea     r14, [rdi + r10 * 8]

ENDM

; Row 0's diagonal injection: D'_0 = a[0]^2 (c_{-1} = 0, no correction
; term). Fresh-writes position 0 and seeds r15/rdx/r9 for the row.
DIAG_ROW0 MACRO
    mulx    r11, r12, rdx                        ; r11:r12 = a[0]^2
    mov     QWORD PTR [rdi + r10 * 8 - 8], r12   ; result[0] = d_lo (fresh)
    mov     r15, r11                             ; prev_seed = d_hi

    mov     r9, rdx                              ; r9 = a[0] (copy, to extract c_0)
    shr     r9, 63                                ; r9 = c_0, persists as c_prev for row 1
    shl     rdx, 1                                 ; rdx = u'_0 = a[0] << 1
ENDM

; A later row's diagonal injection: D'_i = a[i] * (a[i] + c_prev), added
; into result[2i]; seeds r15 with the carry into the row's own first slot,
; and updates rdx = u'_i, r9 = c_i for the next row.
DIAG_OUTER MACRO
    mov     r11, rdx
    add     r11, r9                              ; r11 = a[i] + c_prev; CF = overflow
    mulx    r13, r12, r11                        ; tentative r13:r12 = a[i] * (a[i] + c_prev)
    cmovc   r13, rdx                             ; fix the high half if it overflowed

    mov     r11, QWORD PTR [rdi + r10 * 8 - 8]   ; existing result[2i]
    add     r11, r12
    mov     QWORD PTR [rdi + r10 * 8 - 8], r11
    adc     r13, 0                                ; r13 = prev_seed (proven safe, see file header)
    mov     r15, r13

    mov     r11, rdx                              ; r11 = a[i] (copy, to extract c_i)
    shr     r11, 63                                 ; r11 = c_i
    shl     rdx, 1                                   ; rdx = a[i] << 1
    or      rdx, r9                                   ; rdx = u'_i = (a[i] << 1) | c_prev
    mov     r9, r11                                    ; c_prev := c_i, for the next row
ENDM

; rbx, rsi, rdi, r12, r13, r14 and r15 are nonvolatile in this convention;
; the FRAME prologue records their saves so the function unwinds correctly.
beman_big_int_square_long_runtime_bmi2_adx PROC FRAME

    push    rbx
    .pushreg rbx
    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    push    r12
    .pushreg r12
    push    r13
    .pushreg r13
    push    r14
    .pushreg r14
    push    r15
    .pushreg r15
    .endprolog

    mov     rdi, rcx                ; rdi = p_result
    mov     rsi, rdx                ; rsi = p_a
    mov     rdx, r8                 ; rdx = len_a

    cmp     rdx, 1
    jbe     bmi2adx_sqr_tiny        ; with at most one limb there is no triangle

    mov     r9, rdx                 ; rdx currently = len_a

    lea     rsi, [rsi + r9 * 8]     ; rsi = a + len_a
    lea     rdi, [rdi + r9 * 8]     ; rdi = result + len_a (row 0)
    lea     r8, [r9 - 1]            ; r8  = row 0's length, len_a - 1

    ; Row 0 stores instead of accumulating, writing result[1 .. len_a]. Each
    ; later row starts one limb further on and ends one limb further on, so
    ; its accumulating span is always already written and its carry-out slot
    ; never is: no limb of the triangle needs pre-zeroing. r9 (len_a) is
    ; free again after this, so it becomes c_prev (see the file header).

    mov     r10, r8
    neg     r10                              ; r10 = true_index = -row_len
    mov     rdx, QWORD PTR [rsi + r10 * 8 - 8]      ; rdx = a[0]
    DIAG_ROW0

    cmp     r8, 1
    je      row0_len1
    cmp     r8, 2
    je      row0_len2
    cmp     r8, 3
    je      row0_len3

    ROW_SETUP row0

    xor     eax, eax                ; CF = OF = 0; rax stays 0 for the whole row
    mov     r13, r15                ; carry-in seed = prev_seed (read by an even entry slot)
    mov     r11, r15                ; carry-in seed = prev_seed (read by an odd entry slot)
    jmp     r10                     ; Duff's-device entry

row0_len1:
    ROW_SETUP_SIMPLE
    xor     eax, eax
    mov     r13, r15
    MSLOT   0, r10, r11, r13
    mov     r13, r11                ; the row-end code below expects the carry in r13
    jmp     row0_done

row0_len2:
    ROW_SETUP_SIMPLE
    xor     eax, eax
    mov     r13, r15
    MSLOT   0, r10, r11, r13
    MSLOT   8, r12, r13, r11
    jmp     row0_done

row0_len3:
    ROW_SETUP_SIMPLE
    xor     eax, eax
    mov     r13, r15
    MSLOT   0, r10, r11, r13
    MSLOT   8, r12, r13, r11
    MSLOT   16, r10, r11, r13
    mov     r13, r11
    jmp     row0_done

entry_table_row0:
    DD      row0_slot0 - entry_table_row0
    DD      row0_slot1 - entry_table_row0
    DD      row0_slot2 - entry_table_row0
    DD      row0_slot3 - entry_table_row0
    DD      row0_slot4 - entry_table_row0
    DD      row0_slot5 - entry_table_row0
    DD      row0_slot6 - entry_table_row0
    DD      row0_slot7 - entry_table_row0
    DD      row0_slot8 - entry_table_row0
    DD      row0_slot9 - entry_table_row0
    DD      row0_slot10 - entry_table_row0
    DD      row0_slot11 - entry_table_row0
    DD      row0_slot12 - entry_table_row0
    DD      row0_slot13 - entry_table_row0
    DD      row0_slot14 - entry_table_row0
    DD      row0_slot15 - entry_table_row0

    align 16
row0_body:
row0_slot0:
    MSLOT 0, r10, r11, r13
row0_slot1:
    MSLOT 8, r12, r13, r11
row0_slot2:
    MSLOT 16, r10, r11, r13
row0_slot3:
    MSLOT 24, r12, r13, r11
row0_slot4:
    MSLOT 32, r10, r11, r13
row0_slot5:
    MSLOT 40, r12, r13, r11
row0_slot6:
    MSLOT 48, r10, r11, r13
row0_slot7:
    MSLOT 56, r12, r13, r11
row0_slot8:
    MSLOT 64, r10, r11, r13
row0_slot9:
    MSLOT 72, r12, r13, r11
row0_slot10:
    MSLOT 80, r10, r11, r13
row0_slot11:
    MSLOT 88, r12, r13, r11
row0_slot12:
    MSLOT 96, r10, r11, r13
row0_slot13:
    MSLOT 104, r12, r13, r11
row0_slot14:
    MSLOT 112, r10, r11, r13
row0_slot15:
    MSLOT 120, r12, r13, r11

    lea     rbx, [rbx + BLOCK * 8]
    lea     r14, [r14 + BLOCK * 8]
    lea     rcx, [rcx - 1]
    jrcxz   row0_done
    jmp     row0_body
row0_done:

    adcx    r13, rax                                ; r13 += CF
    adox    r13, rax                                ; r13 += OF (r13 + CF + OF <= 2^64 - 1)
    mov     QWORD PTR [rdi], r13                    ; result[len_a] = carry (rdi is fixed for the row)

    add     rdi, 8                  ; increment the result ptr for next iter
    sub     r8, 1                   ; row_len--
    jz      bmi2adx_sqr_diag        ; len_a == 2: the triangle is a single limb

    align 16
bmi2adx_sqr_outer_loop_start:

    mov     r10, r8
    neg     r10                              ; r10 = true_index = -row_len
    mov     rdx, QWORD PTR [rsi + r10 * 8 - 8]      ; rdx = a[i]
    DIAG_OUTER

    cmp     r8, 1
    je      outer_len1
    cmp     r8, 2
    je      outer_len2
    cmp     r8, 3
    je      outer_len3

    ROW_SETUP outer

    xor     eax, eax
    mov     r13, r15
    mov     r11, r15
    jmp     r10

outer_len1:
    ROW_SETUP_SIMPLE
    xor     eax, eax
    mov     r13, r15
    ASLOT   0, r10, r11, r13, r12
    mov     r13, r11                ; the row-end code below expects the carry in r13
    jmp     outer_done

outer_len2:
    ROW_SETUP_SIMPLE
    xor     eax, eax
    mov     r13, r15
    ASLOT   0, r10, r11, r13, r12
    ASLOT   8, r12, r13, r11, r10
    jmp     outer_done

outer_len3:
    ROW_SETUP_SIMPLE
    xor     eax, eax
    mov     r13, r15
    ASLOT   0, r10, r11, r13, r12
    ASLOT   8, r12, r13, r11, r10
    ASLOT   16, r10, r11, r13, r12
    mov     r13, r11
    jmp     outer_done

entry_table_outer:
    DD      outer_slot0 - entry_table_outer
    DD      outer_slot1 - entry_table_outer
    DD      outer_slot2 - entry_table_outer
    DD      outer_slot3 - entry_table_outer
    DD      outer_slot4 - entry_table_outer
    DD      outer_slot5 - entry_table_outer
    DD      outer_slot6 - entry_table_outer
    DD      outer_slot7 - entry_table_outer
    DD      outer_slot8 - entry_table_outer
    DD      outer_slot9 - entry_table_outer
    DD      outer_slot10 - entry_table_outer
    DD      outer_slot11 - entry_table_outer
    DD      outer_slot12 - entry_table_outer
    DD      outer_slot13 - entry_table_outer
    DD      outer_slot14 - entry_table_outer
    DD      outer_slot15 - entry_table_outer

    align 16
outer_body:
outer_slot0:
    ASLOT 0, r10, r11, r13, r12
outer_slot1:
    ASLOT 8, r12, r13, r11, r10
outer_slot2:
    ASLOT 16, r10, r11, r13, r12
outer_slot3:
    ASLOT 24, r12, r13, r11, r10
outer_slot4:
    ASLOT 32, r10, r11, r13, r12
outer_slot5:
    ASLOT 40, r12, r13, r11, r10
outer_slot6:
    ASLOT 48, r10, r11, r13, r12
outer_slot7:
    ASLOT 56, r12, r13, r11, r10
outer_slot8:
    ASLOT 64, r10, r11, r13, r12
outer_slot9:
    ASLOT 72, r12, r13, r11, r10
outer_slot10:
    ASLOT 80, r10, r11, r13, r12
outer_slot11:
    ASLOT 88, r12, r13, r11, r10
outer_slot12:
    ASLOT 96, r10, r11, r13, r12
outer_slot13:
    ASLOT 104, r12, r13, r11, r10
outer_slot14:
    ASLOT 112, r10, r11, r13, r12
outer_slot15:
    ASLOT 120, r12, r13, r11, r10

    lea     rbx, [rbx + BLOCK * 8]
    lea     r14, [r14 + BLOCK * 8]
    lea     rcx, [rcx - 1]
    jrcxz   outer_done
    jmp     outer_body
outer_done:

    adcx    r13, rax
    adox    r13, rax
    mov     QWORD PTR [rdi], r13                    ; result[len_a + i] = carry

    add     rdi, 8                              ; increment the result ptr for next iter
    sub     r8, 1                               ; row_len--
    jnz     bmi2adx_sqr_outer_loop_start        ; if (row_len != 0) { goto outer_loop_start; }

bmi2adx_sqr_diag:

    ; Every row's own diagonal term was folded in at its own start
    ; (DIAG_ROW0/DIAG_OUTER); only the last row's remains, landing on
    ; result[2n - 2 .. 2n - 1]. result[2n - 2] already holds the previous
    ; row's carry-out (rdi points one past it); result[2n - 1], the very
    ; last limb, is untouched by any row and its final carry is exactly 0
    ; (a^2 < B^(2 * len_a)).
    ;
    ; AVX2 was evaluated for the doubling step this replaces (vpsllq/
    ; vpsrlq/vpermq/vpblendd), since AVX2 has no 64x64->128 multiply to
    ; speed up the triangle itself; the vector doubling never beat this
    ; scalar chain by more than noise and was slower at small n. With
    ; doubling folded into the rows there is no separate pass left to
    ; vectorize.

    mov     rdx, QWORD PTR [rsi - 8]     ; rdx = a[len_a - 1]
    mov     r11, rdx
    add     r11, r9                       ; r11 = a[n - 1] + c_prev; CF = overflow
    mulx    r13, r12, r11                 ; tentative D'_{n - 1}
    cmovc   r13, rdx                      ; fix the high half if it overflowed (see DIAG_OUTER)

    mov     r10, QWORD PTR [rdi - 8]      ; existing result[2n - 2]
    add     r10, r12
    mov     QWORD PTR [rdi - 8], r10
    adc     r13, 0                        ; result[2n - 1] (no further carry: a^2 < B^(2 * len_a))
    mov     QWORD PTR [rdi], r13

    jmp     bmi2adx_sqr_end

bmi2adx_sqr_tiny:

    test    rdx, rdx                ; an empty operand leaves nothing to do
    jz      bmi2adx_sqr_end

    mov     rdx, QWORD PTR [rsi]    ; rdx = a[0]
    mulx    rcx, rax, rdx           ; rcx : rax = a[0]^2 (hi : lo)
    mov     QWORD PTR [rdi], rax    ; result[0] = low64
    mov     QWORD PTR [rdi + 8], rcx    ; result[1] = high64

bmi2adx_sqr_end:

    pop     r15                     ; restore in reverse push order
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

beman_big_int_square_long_runtime_bmi2_adx ENDP

END
