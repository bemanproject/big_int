; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

.code

; Schoolbook long multiplication, baseline x86-64 only (no SSE/AVX, no BMI2/ADX).

; Microsoft x64 calling convention: rcx, rdx, r8, r9 for first four args
;
;   rcx -> p_result
;   rdx -> p_a
;   r8  -> len_a
;   r9  -> p_b
;   [rsp+40] -> len_b (5th argument; read before sub rsp 8, so offset = 40 + 4*8 = 72 at time of read)
;
; Writes exactly len_a + len_b limbs. The first row stores instead of
; accumulating, so p_result need not be pre-zeroed (matching multiply_long).
;
; Register allocation during execution:
;
;   rcx -> result + len_a, walking forward one limb per row
;   r8  -> a + len_a, fixed (rdx is kept free for the mul rdx:rax output)
;   r9  -> b, walking forward one limb per row
;   r10 -> len_b / rows left to do
;   r11 -> b[j], the row's multiplier
;   r12 -> the row's running carry
;   rbx -> inner loop index, running -len_a .. 0
;   r13 -> -(len_a & ~3), the index where the 4x unrolled body takes over
;   rsi -> -len_a, the index each row starts from
;
; Both arrays are addressed as [ptr + rbx * 8], so no pointer has to be rewound
; between rows: the index is just reloaded from rsi.

; One limb of the first row: result[i] = a[i] * b[0] + carry.
GENERIC_X64_MUL_LIMB MACRO off

    mov     rax, QWORD PTR [r8 + rbx * 8 + off]     ; rax = a[i]
    mul     r11                                     ; rdx : rax = a[i] * b[j]

    add     rax, r12                                ; low64 += temp
    adc     rdx, 0                                  ; high64 += carry_flag

    mov     QWORD PTR [rcx + rbx * 8 + off], rax    ; result[i + j] = low64
    mov     r12, rdx                                ; temp = high64

ENDM

; One limb of a later row: result[i + j] += a[i] * b[j] + carry.
;
; The stored product goes in first and the incoming carry last, which leaves
; just the final add/adc pair loop-carried (two cycles); the widening multiply
; and the load of result[i + j] hang off that chain instead of sitting on it.
; a[i] * b[j] + result[i + j] + temp is at most 2^128 - 1, so neither adc can
; carry out of high64.
GENERIC_X64_ADDMUL_LIMB MACRO off

    mov     rax, QWORD PTR [r8 + rbx * 8 + off]     ; rax = a[i]
    mul     r11                                     ; rdx : rax = a[i] * b[j]

    add     rax, QWORD PTR [rcx + rbx * 8 + off]    ; low64 += result[i + j]
    adc     rdx, 0                                  ; high64 += carry_flag
    add     rax, r12                                ; low64 += temp
    adc     rdx, 0                                  ; high64 += carry_flag

    mov     QWORD PTR [rcx + rbx * 8 + off], rax    ; result[i + j] = low64
    mov     r12, rdx                                ; temp = high64

ENDM

beman_big_int_multiply_long_runtime_generic PROC

    ; Microsoft x64 calling convention:
    ; rcx = p_result, rdx = p_a, r8 = len_a, r9 = p_b
    ; 5th parameter at [rsp+40]

    push    rbx
    push    r12
    push    r13
    push    rsi

    ; After 4 pushes (32 bytes), the 5th argument is at [rsp + 40 + 32] = [rsp + 72]
    mov     r10, QWORD PTR [rsp + 72]

    ; no need to align rsp to 16-byte
    ; boundary as this is a leaf function

    ; a * b is symmetric, so run the inner loop over the longer operand

    ; a * b is symmetric, so run the inner loop over the longer operand: that
    ; makes the number of rows, and with it all per-row overhead, min(len_a, len_b).

    cmp     r8, r10                 ; compare len_a with len_b
    jae     generic_x64_sizes_ordered

    xchg    rdx, r9                 ; swap p_a and p_b
    xchg    r8, r10                 ; swap len_a and len_b

generic_x64_sizes_ordered:

    test    r10, r10                ; an empty operand leaves nothing to do
    jz      generic_x64_end

    ; Now: r8 = len_a (the longer length), r10 = len_b (rows to process)
    ;      rdx = p_a, r9 = p_b, rcx = p_result

    mov     rsi, r8                 ; rsi = len_a (r8 free to hold a-pointer; rdx freed for mul)

    ; now len_a in rsi, r8 will hold a + len_a (safe from mul clobber)

    lea     r8, [rdx + rsi * 8]     ; r8 = a + len_a  (rdx left free for mul rdx:rax output)
    lea     rcx, [rcx + rsi * 8]    ; rcx = result + len_a

    mov     r13, rsi
    and     r13, -4                 ; r13 = len_a & ~3
    neg     r13                     ; r13 = -(len_a & ~3)
    neg     rsi                     ; rsi = -len_a

    ; rsi now contains -len_a (for the loop index)

    ; First row (j = 0) stores instead of accumulating. That drops a load and
    ; an add/adc pair per limb, and it is what lets the caller hand over a
    ; buffer it has not zeroed.

    mov     r11, QWORD PTR [r9]     ; r11 = b[0]
    mov     rbx, rsi                ; rbx = i = -len_a
    xor     r12d, r12d              ; r12 = temp = 0

    cmp     rbx, r13
    je      generic_x64_mul_row_4x

generic_x64_mul_row_rmdr:

    GENERIC_X64_MUL_LIMB 0

    add     rbx, 1                                  ; i++
    cmp     rbx, r13
    jne     generic_x64_mul_row_rmdr                ; if (i != -(len_a & ~3)) { goto rmdr_loop_start; }

generic_x64_mul_row_4x:

    test    rbx, rbx
    jz      generic_x64_mul_row_end

    align 16
generic_x64_mul_row_4x_unroll:

    GENERIC_X64_MUL_LIMB 0
    GENERIC_X64_MUL_LIMB 8
    GENERIC_X64_MUL_LIMB 16
    GENERIC_X64_MUL_LIMB 24

    add     rbx, 4                                  ; i += 4
    jnz     generic_x64_mul_row_4x_unroll           ; if (i != 0) { goto unroll_loop_start; }

generic_x64_mul_row_end:

    mov     QWORD PTR [rcx], r12    ; result[len_a] = temp

    add     rcx, 8                  ; increment the result ptr for next iter
    add     r9, 8                   ; increment the b ptr for next iter
    sub     r10, 1                  ; len_b--
    jz      generic_x64_end

    align 16
generic_x64_outer_loop_start:

    mov     r11, QWORD PTR [r9]     ; r11 = b[j]
    mov     rbx, rsi                ; rbx = i = -len_a
    xor     r12d, r12d              ; r12 = temp = 0

    cmp     rbx, r13
    je      generic_x64_inner_loop_4x

generic_x64_inner_loop_rmdr:

    GENERIC_X64_ADDMUL_LIMB 0

    add     rbx, 1                                  ; i++
    cmp     rbx, r13
    jne     generic_x64_inner_loop_rmdr             ; if (i != -(len_a & ~3)) { goto rmdr_loop_start; }

generic_x64_inner_loop_4x:

    test    rbx, rbx
    jz      generic_x64_outer_loop_end

    align 16
generic_x64_inner_loop_4x_unroll:

    GENERIC_X64_ADDMUL_LIMB 0
    GENERIC_X64_ADDMUL_LIMB 8
    GENERIC_X64_ADDMUL_LIMB 16
    GENERIC_X64_ADDMUL_LIMB 24

    add     rbx, 4                                  ; i += 4
    jnz     generic_x64_inner_loop_4x_unroll        ; if (i != 0) { goto unroll_loop_start; }

generic_x64_outer_loop_end:

    mov     QWORD PTR [rcx], r12    ; result[len_a + j] = temp

    add     rcx, 8                                  ; increment the result ptr for next iter
    add     r9, 8                                   ; increment the b ptr for next iter
    sub     r10, 1                                  ; len_b--
    jnz     generic_x64_outer_loop_start            ; if (len_b != 0) { goto outer_loop_start; }

generic_x64_end:

    pop     rsi                     ; restore in reverse push order (push was rbx,r12,r13,rsi)
    pop     r13
    pop     r12
    pop     rbx
    ret

beman_big_int_multiply_long_runtime_generic ENDP

END
