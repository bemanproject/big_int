; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

.code

; Schoolbook long multiplication, x86-64 with BMI2 (mulx) and ADX (adcx/adox).
; See multiply_long_runtime_x86_64_bmi2_adx.s for the full design (the two
; tiers and the LA_EXACT_MAX split, the dual CF/OF carry chain and the
; row-end fold, base+displacement addressing). This is a straight port to
; the Win64 calling convention.
;
; MASM's "&" pastes macro-argument text, so it cannot be used as
; bitwise-and here; "is odd" is computed as "x - (x/2)*2" throughout.
;
; Unlike the SysV port's single function, this is two PROC FRAME procs:
; the exported symbol handles the exact tier and tail-jumps (plain jmp,
; before any push) to a private general-tier proc for len_a > LA_EXACT_MAX,
; so each proc gets its own correct, independent unwind info.
;
; Microsoft x64 calling convention: rcx=p_result, rdx=p_a, r8=len_a,
; r9=p_b, [rsp+40]=len_b (5th argument; read before any push).
;
; Register roles once each proc's own prologue has run and operands are
; ordered (len_a >= len_b):
;   rsi -> a base (general tier: reset from r14 each row)
;   rdi -> result + j, current row base, advances one limb per row
;   r12 -> rows left, starts as len_b
;   rdx -> b[j], mulx's multiplier for the row
;   rax -> zero for the row (clears/folds CF+OF)
;   r8,r9 / r10,r11 -> the two alternating (lo, hi) pairs
;   exact tier only: rcx = b walk (the incoming b pointer arrives in r9,
;     which collides with pair-register use, so it is moved into rcx once)
;   general tier only: rbp=b walk, r14=a base, rbx=(len_a-1)*8 row-end
;     rewind, r13=len_a/BLOCK passes, r15=this call's addmul entry

BLOCK = 16
LOG2_BLOCK = 4
LA_EXACT_MAX = 16

; Core per-limb macros, shared by both tiers.

MULSLOT MACRO off, hi, lo, hiprev
    mulx    hi, lo, QWORD PTR [rsi + off]
    adcx    lo, hiprev
    mov     QWORD PTR [rdi + off], lo
ENDM

ADDMULSLOT MACRO off, hi, lo, hiprev, scratch
    mulx    hi, lo, QWORD PTR [rsi + off]
    mov     scratch, QWORD PTR [rdi + off]
    adcx    lo, hiprev
    adox    lo, scratch
    mov     QWORD PTR [rdi + off], lo
ENDM

MULSLOT0 MACRO off, hi, lo
    mulx    hi, lo, QWORD PTR [rsi + off]
    mov     QWORD PTR [rdi + off], lo
ENDM

ADDMULSLOT0 MACRO off, hi, lo, scratch
    mulx    hi, lo, QWORD PTR [rsi + off]
    mov     scratch, QWORD PTR [rdi + off]
    adox    lo, scratch
    mov     QWORD PTR [rdi + off], lo
ENDM

; Exact-length tier: len_a in [1, LA_EXACT_MAX].

EXACT_MUL_SLOT MACRO n
    IF n EQ 0
        MULSLOT0 0, r9, r8
    ELSEIF (n) - ((n)/2)*2
        MULSLOT %(n*8), r11, r10, r9
    ELSE
        MULSLOT %(n*8), r9, r8, r11
    ENDIF
ENDM

EXACT_ADDMUL_SLOT MACRO n
    IF n EQ 0
        ADDMULSLOT0 0, r9, r8, r10
    ELSEIF (n) - ((n)/2)*2
        ADDMULSLOT %(n*8), r11, r10, r9, r8
    ELSE
        ADDMULSLOT %(n*8), r9, r8, r11, r10
    ENDIF
ENDM

EXACT_FOLD_MUL MACRO k
    IF k EQ 1
        mov     QWORD PTR [rdi + k*8], r9
    ELSEIF ((k-1) - ((k-1)/2)*2)
        adcx    r11, rax
        mov     QWORD PTR [rdi + k*8], r11
    ELSE
        adcx    r9, rax
        mov     QWORD PTR [rdi + k*8], r9
    ENDIF
ENDM

EXACT_FOLD_ADDMUL MACRO k
    IF k EQ 1
        adox    r9, rax
        mov     QWORD PTR [rdi + k*8], r9
    ELSEIF ((k-1) - ((k-1)/2)*2)
        adcx    r11, rax
        adox    r11, rax
        mov     QWORD PTR [rdi + k*8], r11
    ELSE
        adcx    r9, rax
        adox    r9, rax
        mov     QWORD PTR [rdi + k*8], r9
    ENDIF
ENDM

EXACT_LEN MACRO k
exact_la&k&:
    xor     eax, eax
    mov     rdx, QWORD PTR [rcx]
    _i = 0
    REPT k
        EXACT_MUL_SLOT %_i
        _i = _i + 1
    ENDM
    EXACT_FOLD_MUL k
    lea     rdi, [rdi + 8]
    lea     rcx, [rcx + 8]
    dec     r12
    jz      exact_end
exact_la&k&_addmul:
    xor     eax, eax
    mov     rdx, QWORD PTR [rcx]
    _i = 0
    REPT k
        EXACT_ADDMUL_SLOT %_i
        _i = _i + 1
    ENDM
    EXACT_FOLD_ADDMUL k
    lea     rdi, [rdi + 8]
    lea     rcx, [rcx + 8]
    dec     r12
    jnz     exact_la&k&_addmul
    jmp     exact_end
ENDM

EXACT_TABLE_ENTRY MACRO k
    DD      exact_la&k& - exact_table
ENDM

; General tier: len_a > LA_EXACT_MAX. rsi/rdi walk forward per block via
; lea; rsi is reset and rdi is rewound at row end, both outside the row's
; carry chains.

BULK_MUL_SLOT MACRO n
    IF (n) - ((n)/2)*2
        MULSLOT %(n*8), r11, r10, r9
    ELSE
        MULSLOT %(n*8), r9, r8, r11
    ENDIF
ENDM

BULK_ADDMUL_SLOT MACRO n
    IF (n) - ((n)/2)*2
        ADDMULSLOT %(n*8), r11, r10, r9, r8
    ELSE
        ADDMULSLOT %(n*8), r9, r8, r11, r10
    ENDIF
ENDM

BULK_FOLD_MUL MACRO
    IF ((BLOCK-1) - ((BLOCK-1)/2)*2)
        adcx    r11, rax
        mov     QWORD PTR [rdi], r11
    ELSE
        adcx    r9, rax
        mov     QWORD PTR [rdi], r9
    ENDIF
ENDM

BULK_FOLD_ADDMUL MACRO
    IF ((BLOCK-1) - ((BLOCK-1)/2)*2)
        adcx    r11, rax
        adox    r11, rax
        mov     QWORD PTR [rdi], r11
    ELSE
        adcx    r9, rax
        adox    r9, rax
        mov     QWORD PTR [rdi], r9
    ENDIF
ENDM

; Remainder copies: length r in [0, BLOCK). Slot 0 (when r > 0) is the
; row's true first slot, so it uses the no-op-skip MULSLOT0/ADDMULSLOT0.
; A copy starts its alternation at pair A (r even) or pair B (r odd), so
; the following bulk loop's first slot is always pair A regardless of r.

REMAINDER_MUL_SLOT MACRO n, start_phase
    IF n EQ 0
        IF start_phase
            MULSLOT0 0, r11, r10
        ELSE
            MULSLOT0 0, r9, r8
        ENDIF
    ELSE
        IF ((n + start_phase) - ((n + start_phase)/2)*2)
            MULSLOT %(n*8), r11, r10, r9
        ELSE
            MULSLOT %(n*8), r9, r8, r11
        ENDIF
    ENDIF
ENDM

REMAINDER_ADDMUL_SLOT MACRO n, start_phase
    IF n EQ 0
        IF start_phase
            ADDMULSLOT0 0, r11, r10, r8
        ELSE
            ADDMULSLOT0 0, r9, r8, r10
        ENDIF
    ELSE
        IF ((n + start_phase) - ((n + start_phase)/2)*2)
            ADDMULSLOT %(n*8), r11, r10, r9, r8
        ELSE
            ADDMULSLOT %(n*8), r9, r8, r11, r10
        ENDIF
    ENDIF
ENDM

REMAINDER_MUL_LEN MACRO r
gen_rem_mul&r&:
    xor     eax, eax
    mov     rdx, QWORD PTR [rbp]
    IF r EQ 0
        ; r == 0: the bulk loop's slot 0 is the row's true first slot, so
        ; its hiprev (r11) needs a real zero here.
        xor     r11d, r11d
    ENDIF
    _startphase = r - (r/2)*2
    IF r GT 0
        _i = 0
        REPT r
            REMAINDER_MUL_SLOT %_i, %_startphase
            _i = _i + 1
        ENDM
        lea     rsi, [rsi + r*8]
        lea     rdi, [rdi + r*8]
    ENDIF
    jmp     gen_bulk_mul
ENDM

REMAINDER_ADDMUL_LEN MACRO r
gen_rem_addmul&r&:
    xor     eax, eax
    mov     rdx, QWORD PTR [rbp]
    IF r EQ 0
        xor     r11d, r11d
    ENDIF
    _startphase = r - (r/2)*2
    IF r GT 0
        _i = 0
        REPT r
            REMAINDER_ADDMUL_SLOT %_i, %_startphase
            _i = _i + 1
        ENDM
        lea     rsi, [rsi + r*8]
        lea     rdi, [rdi + r*8]
    ENDIF
    jmp     gen_bulk_addmul
ENDM

GEN_MUL_TABLE_ENTRY MACRO r
    DD      gen_rem_mul&r& - gen_mul_table
ENDM

GEN_ADDMUL_TABLE_ENTRY MACRO r
    DD      gen_rem_addmul&r& - gen_addmul_table
ENDM

; Exported entry point: swap + size gate (no pushes yet), then either the
; exact tier directly or a tail-jmp to the general tier's own proc.
beman_big_int_multiply_long_runtime_bmi2_adx PROC FRAME
    mov     r10, QWORD PTR [rsp + 40]   ; len_b (5th arg; no pushes yet, offset = 40)

    cmp     r8, r10
    jae     main_sizes_ordered
    xchg    rdx, r9
    xchg    r8, r10
main_sizes_ordered:

    cmp     r8, LA_EXACT_MAX
    ja      general_tier_impl           ; tail-jmp: rcx,rdx,r8,r9,r10 stay live for it

    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    push    r12
    .pushreg r12
    .endprolog

    test    r10, r10                    ; an empty operand leaves nothing to do
    jz      exact_end

    mov     rdi, rcx                     ; rdi = p_result
    mov     rsi, rdx                     ; rsi = p_a
    mov     r12, r10                     ; r12 = rows = min(len_a, len_b)
    mov     rcx, r9                      ; b walk into rcx (r9 collides with pair-register use)

    cmp     r8, 1
    je      exact_la1

    lea     rax, exact_table
    movsxd  r10, DWORD PTR [rax + r8 * 4 - 4]
    add     rax, r10
    jmp     rax

exact_table:
_n = 1
REPT LA_EXACT_MAX
    EXACT_TABLE_ENTRY %_n
    _n = _n + 1
ENDM

_n = 1
REPT LA_EXACT_MAX
    EXACT_LEN %_n
    _n = _n + 1
ENDM

exact_end:
    pop     r12
    pop     rdi
    pop     rsi
    ret
beman_big_int_multiply_long_runtime_bmi2_adx ENDP

; Reached only via the tail-jmp above, so it owns its own prologue and
; unwind info. Incoming: rcx=p_result, rdx=p_a, r8=len_a (the longer one),
; r9=p_b, r10=len_b (the shorter one).
general_tier_impl PROC FRAME
    push    rbx
    .pushreg rbx
    push    rbp
    .pushreg rbp
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

    test    r10, r10
    jz      general_end

    mov     rdi, rcx                     ; rdi = p_result
    mov     rsi, rdx                     ; rsi = p_a
    mov     r12, r10                     ; r12 = rows = min(len_a, len_b)
    mov     rbp, r9                      ; rbp = b walk (persistent)
    mov     r14, rsi                     ; r14 = a_base (persistent)
    lea     rbx, [r8 * 8 - 8]            ; rbx = (len_a - 1) * 8 (row-end rewind distance)

    mov     r13, r8
    shr     r13, LOG2_BLOCK              ; r13 = q, full BLOCK passes per row (persistent)
    mov     r9, r8
    and     r9, (BLOCK - 1)              ; r9 = r, this call's remainder length (0..BLOCK-1)

    lea     rax, gen_addmul_table
    movsxd  r11, DWORD PTR [rax + r9 * 4]
    add     rax, r11
    mov     r15, rax                     ; r15 = this call's addmul remainder entry (persistent)

    mov     rcx, r13                     ; pass counter ready for row 0

    lea     rax, gen_mul_table
    movsxd  r11, DWORD PTR [rax + r9 * 4]
    add     rax, r11
    jmp     rax                          ; one-time direct jump into row 0's remainder entry

gen_mul_table:
_n = 0
REPT BLOCK
    GEN_MUL_TABLE_ENTRY %_n
    _n = _n + 1
ENDM

gen_addmul_table:
_n = 0
REPT BLOCK
    GEN_ADDMUL_TABLE_ENTRY %_n
    _n = _n + 1
ENDM

_n = 0
REPT BLOCK
    REMAINDER_MUL_LEN %_n
    _n = _n + 1
ENDM

    align 16
gen_bulk_mul:
    _i = 0
    REPT BLOCK
        BULK_MUL_SLOT %_i
        _i = _i + 1
    ENDM
    lea     rsi, [rsi + BLOCK * 8]
    lea     rdi, [rdi + BLOCK * 8]
    lea     rcx, [rcx - 1]
    jrcxz   gen_mul_row_end
    jmp     gen_bulk_mul

gen_mul_row_end:
    BULK_FOLD_MUL
    sub     rdi, rbx
    add     rbp, 8
    dec     r12
    jz      general_end
    mov     rsi, r14
    mov     rcx, r13
    jmp     r15

_n = 0
REPT BLOCK
    REMAINDER_ADDMUL_LEN %_n
    _n = _n + 1
ENDM

    align 16
gen_bulk_addmul:
    _i = 0
    REPT BLOCK
        BULK_ADDMUL_SLOT %_i
        _i = _i + 1
    ENDM
    lea     rsi, [rsi + BLOCK * 8]
    lea     rdi, [rdi + BLOCK * 8]
    lea     rcx, [rcx - 1]
    jrcxz   gen_addmul_row_end
    jmp     gen_bulk_addmul

gen_addmul_row_end:
    BULK_FOLD_ADDMUL
    sub     rdi, rbx
    add     rbp, 8
    dec     r12
    jz      general_end
    mov     rsi, r14
    mov     rcx, r13
    jmp     r15

general_end:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    ret
general_tier_impl ENDP

END
