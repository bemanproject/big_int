; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

.code

; Schoolbook squaring, baseline x86-64 only (no SSE/AVX, no BMI2/ADX).

; Microsoft x64 calling convention: rcx, rdx, r8 for the three arguments
;
;   rcx -> p_result
;   rdx -> p_a
;   r8  -> len_a
;
; Writes exactly 2 * len_a limbs, so p_result need not be pre-zeroed (matching
; multiply_long_runtime). p_result must not alias p_a.
;
; a^2 = D + 2 * T, where D = sum a[i]^2 * B^(2i) is the diagonal and
; T = sum_{i < j} a[i] * a[j] * B^(i + j) the off-diagonal triangle. The first
; phase builds T with one row per a[i], the same mul/addmul rows as
; multiply_long_runtime but each only as long as a[i + 1 .. len_a - 1]: half the
; widening multiplies of a general product. The second phase is a single pass
; that doubles T and adds D.
;
; The prologue moves the arguments into rdi, rsi and rdx, where the System V
; version (square_long_runtime_x86_64_generic.s) receives them, so the body
; below matches that version register for register. Register allocation while
; building the triangle:
;
;   rdi -> result + len_a + i, walking forward one limb per row
;   rsi -> a + len_a, fixed
;   r8  -> the row's length, len_a - 1 - i
;   r9  -> len_a, kept for the diagonal pass
;   r10 -> a[i], the row's multiplier
;   r11 -> the row's running carry
;   rbx -> inner loop index, running -(len_a - 1 - i) .. 0
;   rcx -> -(row length & ~3), the index where the 4x unrolled body takes over
;
; Row i multiplies a[i] into a[j] for j = i + 1 .. len_a - 1 and accumulates at
; result[i + j], so with the index at j - len_a both arrays are addressed as
; [ptr + rbx * 8]. The multiplier sits just below the row, at [rsi + rbx * 8 - 8]
; when rbx starts the row.

; One limb of the first row: result[i + j] = a[j] * a[0] + carry.
GENERIC_X64_MUL_LIMB MACRO off

    mov     rax, QWORD PTR [rsi + rbx * 8 + off]    ; rax = a[j]
    mul     r10                                     ; rdx : rax = a[j] * a[i]

    add     rax, r11                                ; low64 += temp
    adc     rdx, 0                                  ; high64 += carry_flag

    mov     QWORD PTR [rdi + rbx * 8 + off], rax    ; result[i + j] = low64
    mov     r11, rdx                                ; temp = high64

ENDM

; One limb of a later row: result[i + j] += a[j] * a[i] + carry.
;
; Same ordering as multiply_long_runtime: the stored product goes in first and
; the incoming carry last, which leaves just the final add/adc pair
; loop-carried.
GENERIC_X64_ADDMUL_LIMB MACRO off

    mov     rax, QWORD PTR [rsi + rbx * 8 + off]    ; rax = a[j]
    mul     r10                                     ; rdx : rax = a[j] * a[i]

    add     rax, QWORD PTR [rdi + rbx * 8 + off]    ; low64 += result[i + j]
    adc     rdx, 0                                  ; high64 += carry_flag
    add     rax, r11                                ; low64 += temp
    adc     rdx, 0                                  ; high64 += carry_flag

    mov     QWORD PTR [rdi + rbx * 8 + off], rax    ; result[i + j] = low64
    mov     r11, rdx                                ; temp = high64

ENDM

; Per-row setup: index, 4x boundary, multiplier and carry for a row of length r8.
GENERIC_X64_SQR_ROW_SETUP MACRO

    mov     rbx, r8
    neg     rbx                                     ; rbx = -row_len

    mov     rcx, r8
    and     rcx, -4                                 ; rcx = row_len & ~3
    neg     rcx                                     ; rcx = -(row_len & ~3)

    mov     r10, QWORD PTR [rsi + rbx * 8 - 8]      ; r10 = a[i]
    xor     r11d, r11d                              ; temp = 0

ENDM

; rbx, rsi and rdi are nonvolatile in this convention; the FRAME prologue
; records their saves so the function unwinds correctly.
beman_big_int_square_long_runtime_generic PROC FRAME

    push    rbx
    .pushreg rbx
    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    .endprolog

    mov     rdi, rcx                ; rdi = p_result
    mov     rsi, rdx                ; rsi = p_a
    mov     rdx, r8                 ; rdx = len_a

    cmp     rdx, 1
    jbe     generic_x64_sqr_tiny    ; with at most one limb there is no triangle

    mov     r9, rdx                 ; free up rdx for the mul instruction

    lea     rsi, [rsi + r9 * 8]     ; rsi = a + len_a
    lea     rdi, [rdi + r9 * 8]     ; rdi = result + len_a (row 0)
    lea     r8, [r9 - 1]            ; r8  = row 0's length, len_a - 1

    ; Row 0 stores instead of accumulating, writing result[1 .. len_a]. Each
    ; later row starts one limb further on and ends one limb further on, so
    ; its accumulating span is always already written and its carry-out slot
    ; never is: no limb of the triangle needs pre-zeroing.

    GENERIC_X64_SQR_ROW_SETUP

    cmp     rbx, rcx
    je      generic_x64_sqr_mul_row_4x

generic_x64_sqr_mul_row_rmdr:

    GENERIC_X64_MUL_LIMB 0

    add     rbx, 1                                  ; j++
    cmp     rbx, rcx
    jne     generic_x64_sqr_mul_row_rmdr            ; if (j != -(row_len & ~3)) { goto rmdr_loop_start; }

generic_x64_sqr_mul_row_4x:

    test    rbx, rbx
    jz      generic_x64_sqr_mul_row_end

    align 16
generic_x64_sqr_mul_row_4x_unroll:

    GENERIC_X64_MUL_LIMB 0
    GENERIC_X64_MUL_LIMB 8
    GENERIC_X64_MUL_LIMB 16
    GENERIC_X64_MUL_LIMB 24

    add     rbx, 4                                  ; j += 4
    jnz     generic_x64_sqr_mul_row_4x_unroll       ; if (j != 0) { goto unroll_loop_start; }

generic_x64_sqr_mul_row_end:

    mov     QWORD PTR [rdi], r11    ; result[len_a] = temp

    add     rdi, 8                  ; increment the result ptr for next iter
    sub     r8, 1                   ; row_len--
    jz      generic_x64_sqr_diag    ; len_a == 2: the triangle is a single limb

    align 16
generic_x64_sqr_outer_loop_start:

    GENERIC_X64_SQR_ROW_SETUP

    cmp     rbx, rcx
    je      generic_x64_sqr_inner_loop_4x

generic_x64_sqr_inner_loop_rmdr:

    GENERIC_X64_ADDMUL_LIMB 0

    add     rbx, 1                                  ; j++
    cmp     rbx, rcx
    jne     generic_x64_sqr_inner_loop_rmdr         ; if (j != -(row_len & ~3)) { goto rmdr_loop_start; }

generic_x64_sqr_inner_loop_4x:

    test    rbx, rbx
    jz      generic_x64_sqr_outer_loop_end

    align 16
generic_x64_sqr_inner_loop_4x_unroll:

    GENERIC_X64_ADDMUL_LIMB 0
    GENERIC_X64_ADDMUL_LIMB 8
    GENERIC_X64_ADDMUL_LIMB 16
    GENERIC_X64_ADDMUL_LIMB 24

    add     rbx, 4                                  ; j += 4
    jnz     generic_x64_sqr_inner_loop_4x_unroll    ; if (j != 0) { goto unroll_loop_start; }

generic_x64_sqr_outer_loop_end:

    mov     QWORD PTR [rdi], r11    ; result[len_a + i] = temp

    add     rdi, 8                                  ; increment the result ptr for next iter
    sub     r8, 1                                   ; row_len--
    jnz     generic_x64_sqr_outer_loop_start        ; if (row_len != 0) { goto outer_loop_start; }

generic_x64_sqr_diag:

    ; The triangle occupies result[1 .. 2 * len_a - 2]; zero its two missing
    ; end limbs so the pass below can treat every limb pair alike. rdi now
    ; points at result[2 * len_a - 1].

    mov     QWORD PTR [rdi], 0      ; result[2 * len_a - 1] = 0
    add     rdi, 8                  ; rdi = result + 2 * len_a

    lea     rbx, [r9 + r9]
    neg     rbx                     ; rbx = -2 * len_a, running in steps of 2
    mov     QWORD PTR [rdi + rbx * 8], 0    ; result[0] = 0

    xor     r11d, r11d              ; extra = 0

    ; Limb pair (2i, 2i + 1) becomes a[i]^2 + 2 * T[2i + 1 : 2i] + extra, where
    ; extra (at most 2) carries the bit doubling shifts out of T[2i + 1] plus
    ; the carry out of the pair's sum. a[i]^2 + 2 * T[2i + 1 : 2i] + 2 < 2^129,
    ; so that carry out is at most 1. As in the rows, extra is added last so
    ; only one add/adc/adc run is loop-carried; a[i] sits at [rsi + rbx * 4].

    align 16
generic_x64_sqr_diag_loop:

    mov     rax, QWORD PTR [rsi + rbx * 4]      ; rax = a[i]
    mul     rax                                 ; rdx : rax = a[i]^2

    mov     r8, QWORD PTR [rdi + rbx * 8]       ; r8  = T[2i]
    mov     rcx, QWORD PTR [rdi + rbx * 8 + 8]  ; rcx = T[2i + 1]

    xor     r10d, r10d
    add     r8, r8
    adc     rcx, rcx                            ; rcx : r8 = 2 * T[2i + 1 : 2i] mod 2^128
    adc     r10, 0                              ; next_extra = bit shifted out

    add     rax, r8
    adc     rdx, rcx                            ; pair += doubled triangle
    adc     r10, 0                              ; next_extra += carry_flag

    add     rax, r11
    adc     rdx, 0                              ; pair += extra
    adc     r10, 0                              ; next_extra += carry_flag

    mov     QWORD PTR [rdi + rbx * 8], rax      ; result[2i] = low64
    mov     QWORD PTR [rdi + rbx * 8 + 8], rdx  ; result[2i + 1] = high64
    mov     r11, r10                            ; extra = next_extra

    add     rbx, 2                              ; i++
    jnz     generic_x64_sqr_diag_loop           ; if (i != len_a) { goto diag_loop_start; }

    jmp     generic_x64_sqr_end

generic_x64_sqr_tiny:

    test    rdx, rdx                ; an empty operand leaves nothing to do
    jz      generic_x64_sqr_end

    mov     rax, QWORD PTR [rsi]    ; rax = a[0]
    mul     rax                     ; rdx : rax = a[0]^2
    mov     QWORD PTR [rdi], rax    ; result[0] = low64
    mov     QWORD PTR [rdi + 8], rdx    ; result[1] = high64

generic_x64_sqr_end:

    pop     rdi                     ; restore in reverse push order (push was rbx, rsi, rdi)
    pop     rsi
    pop     rbx
    ret

beman_big_int_square_long_runtime_generic ENDP

END
