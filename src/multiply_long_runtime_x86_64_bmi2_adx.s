# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

.intel_syntax noprefix
.altmacro

# .altmacro's "&" pastes macro-argument text, including mid-expansion inside
# .set/.if, so it cannot be used as bitwise-and here; "is odd" is computed
# as "x - (x/2)*2" throughout instead.

# Empty, but present, GNU-stack section
.section .note.GNU-stack,"",%progbits

# Enter the text section for code/symbols
.section .text

# Schoolbook long multiplication, x86-64 with BMI2 (mulx) and ADX (adcx/adox).
# Contract matches beman_big_int_multiply_long_runtime (see
# multiply_long_runtime_x86_64_generic.s).
#
# Two tiers, split on len_a: [1, LA_EXACT_MAX] gets one straight-line
# unrolled row body per length via a jump table; above that, one shared
# BLOCK-wide bulk loop plus one straight-line remainder copy per len_a mod
# BLOCK value. LA_EXACT_MAX must be >= BLOCK - 1 so every general-tier call
# gets at least one full bulk pass.
#
# Each row runs a dual carry chain: CF (adcx) carries the multiply's high
# half limb to limb; OF (adox) carries the accumulation onto the stored
# result, both loop-carried but independent. The row closes by folding the
# last limb's leftover CF and OF into its own high half for the carry-out;
# a[i]*b[j]+result[i+j]+carry <= 2^128-1, so neither fold can overflow.
# Nothing inside a row may touch the flags: lea for index/pointer steps,
# jrcxz for the loop test. Addressing is base+displacement only, never
# indexed: an indexed 3-operand mulx or store un-laminates into an extra
# uop on Intel.
#
# Registers once ordered (len_a >= len_b) and the prologue has run:
#   rsi -> a base (general tier: reset from r14 each row)
#   rdi -> result + j, current row base, advances one limb per row
#   r12 -> rows left, starts as len_b
#   rdx -> b[j], mulx's multiplier for the row
#   rax -> zero for the row (clears/folds CF+OF)
#   r8,r9 / r10,r11 -> the two alternating (lo, hi) pairs
#   rcx -> b walk (exact tier) / bulk-loop pass counter (general tier)
#   general tier only: rbp=b walk, r14=a base, rbx=(len_a-1)*8 row-end
#     rewind, r13=len_a/BLOCK passes, r15=this call's addmul entry

.set LA_EXACT_MAX, 16
.set BLOCK, 16
.set LOG2_BLOCK, 4

.globl beman_big_int_multiply_long_runtime_bmi2_adx
.type beman_big_int_multiply_long_runtime_bmi2_adx, @function

# Core per-limb macros, shared by both tiers.

.macro MULSLOT off, hi, lo, hiprev
    mulx    hi, lo, QWORD PTR [rsi + off]
    adcx    lo, hiprev
    mov     QWORD PTR [rdi + off], lo
.endm

.macro ADDMULSLOT off, hi, lo, hiprev, scratch
    mulx    hi, lo, QWORD PTR [rsi + off]
    mov     scratch, QWORD PTR [rdi + off]
    adcx    lo, hiprev
    adox    lo, scratch
    mov     QWORD PTR [rdi + off], lo
.endm

.macro MULSLOT0 off, hi, lo
    mulx    hi, lo, QWORD PTR [rsi + off]
    mov     QWORD PTR [rdi + off], lo
.endm

.macro ADDMULSLOT0 off, hi, lo, scratch
    mulx    hi, lo, QWORD PTR [rsi + off]
    mov     scratch, QWORD PTR [rdi + off]
    adox    lo, scratch
    mov     QWORD PTR [rdi + off], lo
.endm

# Exact-length tier: len_a in [1, LA_EXACT_MAX].

.macro EXACT_MUL_SLOT n
    .if \n == 0
        MULSLOT0 0, r9, r8
    .elseif (\n) - ((\n)/2)*2
        MULSLOT %(\n*8), r11, r10, r9
    .else
        MULSLOT %(\n*8), r9, r8, r11
    .endif
.endm

.macro EXACT_ADDMUL_SLOT n
    .if \n == 0
        ADDMULSLOT0 0, r9, r8, r10
    .elseif (\n) - ((\n)/2)*2
        ADDMULSLOT %(\n*8), r11, r10, r9, r8
    .else
        ADDMULSLOT %(\n*8), r9, r8, r11, r10
    .endif
.endm

.macro EXACT_FOLD_MUL k
    .if \k == 1
        mov     QWORD PTR [rdi + \k*8], r9
    .elseif ((\k-1) - ((\k-1)/2)*2)
        adcx    r11, rax
        mov     QWORD PTR [rdi + \k*8], r11
    .else
        adcx    r9, rax
        mov     QWORD PTR [rdi + \k*8], r9
    .endif
.endm

.macro EXACT_FOLD_ADDMUL k
    .if \k == 1
        adox    r9, rax
        mov     QWORD PTR [rdi + \k*8], r9
    .elseif ((\k-1) - ((\k-1)/2)*2)
        adcx    r11, rax
        adox    r11, rax
        mov     QWORD PTR [rdi + \k*8], r11
    .else
        adcx    r9, rax
        adox    r9, rax
        mov     QWORD PTR [rdi + \k*8], r9
    .endif
.endm

.macro EXACT_LEN k
Lexact_la\k:
    xor     eax, eax
    mov     rdx, [rcx]
    .set _i, 0
    .rept \k
        EXACT_MUL_SLOT %_i
        .set _i, _i+1
    .endr
    EXACT_FOLD_MUL \k
    lea     rdi, [rdi + 8]
    lea     rcx, [rcx + 8]
    dec     r12
    jz      Lend_short
Lexact_la\k\()_addmul:
    xor     eax, eax
    mov     rdx, [rcx]
    .set _i, 0
    .rept \k
        EXACT_ADDMUL_SLOT %_i
        .set _i, _i+1
    .endr
    EXACT_FOLD_ADDMUL \k
    lea     rdi, [rdi + 8]
    lea     rcx, [rcx + 8]
    dec     r12
    jnz     Lexact_la\k\()_addmul
    jmp     Lend_short
.endm

.macro EXACT_TABLE_ENTRY k
    .long   Lexact_la\k - Lexact_table
.endm

# General tier: len_a > LA_EXACT_MAX. rsi/rdi walk forward per block via
# lea; rsi is reset and rdi is rewound at row end, both outside the row's
# carry chains.

.macro BULK_MUL_SLOT n
    .if (\n) - ((\n)/2)*2
        MULSLOT %(\n*8), r11, r10, r9
    .else
        MULSLOT %(\n*8), r9, r8, r11
    .endif
.endm

.macro BULK_ADDMUL_SLOT n
    .if (\n) - ((\n)/2)*2
        ADDMULSLOT %(\n*8), r11, r10, r9, r8
    .else
        ADDMULSLOT %(\n*8), r9, r8, r11, r10
    .endif
.endm

.macro BULK_FOLD_MUL
    .if ((BLOCK-1) - ((BLOCK-1)/2)*2)
        adcx    r11, rax
        mov     QWORD PTR [rdi], r11
    .else
        adcx    r9, rax
        mov     QWORD PTR [rdi], r9
    .endif
.endm

.macro BULK_FOLD_ADDMUL
    .if ((BLOCK-1) - ((BLOCK-1)/2)*2)
        adcx    r11, rax
        adox    r11, rax
        mov     QWORD PTR [rdi], r11
    .else
        adcx    r9, rax
        adox    r9, rax
        mov     QWORD PTR [rdi], r9
    .endif
.endm

# Remainder copies: length r in [0, BLOCK). Slot 0 (when r > 0) is the
# row's true first slot, so it uses the no-op-skip MULSLOT0/ADDMULSLOT0.
# A copy starts its alternation at pair A (r even) or pair B (r odd), so
# the following bulk loop's first slot is always pair A regardless of r.

.macro REMAINDER_MUL_SLOT n, start_phase
    .if \n == 0
        .if \start_phase
            MULSLOT0 0, r11, r10
        .else
            MULSLOT0 0, r9, r8
        .endif
    .else
        .if ((\n + \start_phase) - ((\n + \start_phase)/2)*2)
            MULSLOT %(\n*8), r11, r10, r9
        .else
            MULSLOT %(\n*8), r9, r8, r11
        .endif
    .endif
.endm

.macro REMAINDER_ADDMUL_SLOT n, start_phase
    .if \n == 0
        .if \start_phase
            ADDMULSLOT0 0, r11, r10, r8
        .else
            ADDMULSLOT0 0, r9, r8, r10
        .endif
    .else
        .if ((\n + \start_phase) - ((\n + \start_phase)/2)*2)
            ADDMULSLOT %(\n*8), r11, r10, r9, r8
        .else
            ADDMULSLOT %(\n*8), r9, r8, r11, r10
        .endif
    .endif
.endm

.macro REMAINDER_MUL_LEN r
Lgen_rem_mul\r:
    xor     eax, eax
    mov     rdx, [rbp]
    .if \r == 0
        # r == 0: the bulk loop's slot 0 is the row's true first slot, so
        # its hiprev (r11) needs a real zero here.
        xor     r11d, r11d
    .endif
    .set _startphase, \r - (\r/2)*2
    .if \r > 0
        .set _i, 0
        .rept \r
            REMAINDER_MUL_SLOT %_i, %_startphase
            .set _i, _i+1
        .endr
        lea     rsi, [rsi + \r*8]
        lea     rdi, [rdi + \r*8]
    .endif
    jmp     Lgen_bulk_mul
.endm

.macro REMAINDER_ADDMUL_LEN r
Lgen_rem_addmul\r:
    xor     eax, eax
    mov     rdx, [rbp]
    .if \r == 0
        xor     r11d, r11d
    .endif
    .set _startphase, \r - (\r/2)*2
    .if \r > 0
        .set _i, 0
        .rept \r
            REMAINDER_ADDMUL_SLOT %_i, %_startphase
            .set _i, _i+1
        .endr
        lea     rsi, [rsi + \r*8]
        lea     rdi, [rdi + \r*8]
    .endif
    jmp     Lgen_bulk_addmul
.endm

.macro GEN_MUL_TABLE_ENTRY r
    .long   Lgen_rem_mul\r - Lgen_mul_table
.endm

.macro GEN_ADDMUL_TABLE_ENTRY r
    .long   Lgen_rem_addmul\r - Lgen_addmul_table
.endm

beman_big_int_multiply_long_runtime_bmi2_adx:
.cfi_startproc

    push    r12
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r12, 0
.cfi_remember_state

    cmp     rdx, r8
    jae     Lstart_sizes_ordered
    xchg    rsi, rcx
    xchg    rdx, r8
Lstart_sizes_ordered:

    test    r8, r8
    jz      Lend_short

    mov     r12, r8

    cmp     rdx, 1
    je      Lexact_la1

    cmp     rdx, LA_EXACT_MAX
    ja      Lgeneral_path

    lea     rax, [rip + Lexact_table]
    movsxd  r10, DWORD PTR [rax + rdx * 4 - 4]
    add     rax, r10
    jmp     rax

Lexact_table:
.set _n, 1
.rept LA_EXACT_MAX
    EXACT_TABLE_ENTRY %_n
    .set _n, _n+1
.endr

.set _n, 1
.rept LA_EXACT_MAX
    EXACT_LEN %_n
    .set _n, _n+1
.endr

Lgeneral_path:
.cfi_restore_state
    push    rbx
.cfi_adjust_cfa_offset 8
.cfi_rel_offset rbx, 0
    push    rbp
.cfi_adjust_cfa_offset 8
.cfi_rel_offset rbp, 0
    push    r13
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r13, 0
    push    r14
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r14, 0
    push    r15
.cfi_adjust_cfa_offset 8
.cfi_rel_offset r15, 0

    mov     rbp, rcx                # rbp = b walk (persistent)
    mov     r14, rsi                # r14 = a_base (persistent; rsi is reset from it each row)
    lea     rbx, [rdx * 8 - 8]      # rbx = (len_a - 1) * 8 (persistent row-end rewind distance)

    mov     r13, rdx
    shr     r13, LOG2_BLOCK         # r13 = q, full BLOCK passes per row (persistent)
    mov     r10, rdx
    and     r10, (BLOCK - 1)        # r10 = r, this call's remainder length (0..BLOCK-1)

    lea     rax, [rip + Lgen_addmul_table]
    movsxd  r11, DWORD PTR [rax + r10 * 4]
    add     rax, r11
    mov     r15, rax                # r15 = this call's addmul remainder entry (persistent)

    mov     rcx, r13                # pass counter ready for row 0

    lea     rax, [rip + Lgen_mul_table]
    movsxd  r11, DWORD PTR [rax + r10 * 4]
    add     rax, r11
    jmp     rax                     # one-time direct jump into row 0's remainder entry

Lgen_mul_table:
.set _n, 0
.rept BLOCK
    GEN_MUL_TABLE_ENTRY %_n
    .set _n, _n+1
.endr

Lgen_addmul_table:
.set _n, 0
.rept BLOCK
    GEN_ADDMUL_TABLE_ENTRY %_n
    .set _n, _n+1
.endr

.set _n, 0
.rept BLOCK
    REMAINDER_MUL_LEN %_n
    .set _n, _n+1
.endr

.p2align 4
Lgen_bulk_mul:
    .set _i, 0
    .rept BLOCK
        BULK_MUL_SLOT %_i
        .set _i, _i+1
    .endr
    lea     rsi, [rsi + BLOCK * 8]
    lea     rdi, [rdi + BLOCK * 8]
    lea     rcx, [rcx - 1]
    jrcxz   Lgen_mul_row_end
    jmp     Lgen_bulk_mul

Lgen_mul_row_end:
    BULK_FOLD_MUL
    sub     rdi, rbx
    add     rbp, 8
    dec     r12
    jz      Lend_long
    mov     rsi, r14
    mov     rcx, r13
    jmp     r15

.set _n, 0
.rept BLOCK
    REMAINDER_ADDMUL_LEN %_n
    .set _n, _n+1
.endr

.p2align 4
Lgen_bulk_addmul:
    .set _i, 0
    .rept BLOCK
        BULK_ADDMUL_SLOT %_i
        .set _i, _i+1
    .endr
    lea     rsi, [rsi + BLOCK * 8]
    lea     rdi, [rdi + BLOCK * 8]
    lea     rcx, [rcx - 1]
    jrcxz   Lgen_addmul_row_end
    jmp     Lgen_bulk_addmul

Lgen_addmul_row_end:
    BULK_FOLD_ADDMUL
    sub     rdi, rbx
    add     rbp, 8
    dec     r12
    jz      Lend_long
    mov     rsi, r14
    mov     rcx, r13
    jmp     r15

Lend_long:
    pop     r15
.cfi_adjust_cfa_offset -8
    pop     r14
.cfi_adjust_cfa_offset -8
    pop     r13
.cfi_adjust_cfa_offset -8
    pop     rbp
.cfi_adjust_cfa_offset -8
    pop     rbx
.cfi_adjust_cfa_offset -8
    pop     r12
.cfi_adjust_cfa_offset -8
    ret

Lend_short:
.cfi_def_cfa_offset 16
    pop     r12
.cfi_adjust_cfa_offset -8
    ret

.cfi_endproc
.size beman_big_int_multiply_long_runtime_bmi2_adx, .-beman_big_int_multiply_long_runtime_bmi2_adx
