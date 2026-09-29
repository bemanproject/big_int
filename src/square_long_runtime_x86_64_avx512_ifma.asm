; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

; AVX-512 IFMA (radix 2^52) schoolbook squaring. See
; square_long_runtime_x86_64_avx512_ifma.s for the full design (the
; digit-domain algorithm, the off-diagonal/diagonal split, the carry
; lookahead). This is a straight port to the Win64 calling convention.
;
; Microsoft x64 calling convention: rcx=p_result, rdx=p_a, r8=len_a.
;
; Win64-specific differences from the SysV .s:
;   - rsi and rdi are callee-saved on Win64 (caller-saved in SysV), so both
;     are pushed alongside rbx, rbp, r14, r15 (this kernel never touches
;     r12/r13); the .s already avoids xmm6-15/zmm6-15 entirely (only
;     zmm0-5 and zmm16-31), so no xmm save area is needed.
;   - rbp is pushed last and serves as the frame pointer (.setframe rbp,0);
;     the epilogue recovers rsp with `lea rsp, [rbp]` (rbp, pushed last,
;     already points at its own saved slot) -- one of the two
;     documented-legal Win64 epilogue forms (`add rsp,imm` or
;     `lea rsp,[fpreg+disp]`), so unwinding stays valid at every
;     instruction across the `and rsp,-64` / fixed `sub rsp,FRAME_SIZE`
;     realignment.
;   - the 5 lookup tables (idx_unpack/shift_common/idx_pack_a/idx_pack_b,
;     shared with the multiply kernel, plus this kernel's own dup_idx) and
;     3 broadcast constants move into a 64-byte aligned CONST segment,
;     referenced RIP-relative.
;   - GAS's reusable numeric local labels (81:,82:,83:,84:,85:) become
;     named labels; 81/82 (inside MASK_CALC_64) become MASM LOCAL labels.
;   - the n==0 early-out and the n>NATIVE_MAX_LIMBS tail-jmp to the
;     bmi2_adx kernel both execute their `ret`/`jmp` right where they are
;     tested, before any push, instead of jumping forward past
;     .endprolog: a PC there would be treated by the unwinder as "fully
;     pushed" even though nothing has been pushed yet. Both proc's
;     argument registers (rcx=p_result, rdx=p_a, r8=len_a) match exactly,
;     so the tail-jmp needs no register shuffling, exactly like the stub
;     it replaces.
;   - the .s's already-fixed digit-count-used-as-byte-offset spot (na_pad,
;     a digit count, needs `shl r8,3` before it is added to a byte
;     pointer) is carried over unchanged; every other digit-index use in
;     this file goes through a `*8`-scaled addressing mode instead, so it
;     is not susceptible to the same mistake.

EXTERN beman_big_int_square_long_runtime_bmi2_adx:PROC

NATIVE_MAX_LIMBS = 256
AD_BUF_DIGITS = 384
PAD_HEAD = 32
PAD_TAIL = 32

FRAME_AD_OFF = 0
FRAME_SCRATCH_OFF = AD_BUF_DIGITS * 8
SLOT_NBYTES_A = FRAME_SCRATCH_OFF
SLOT_NA_PAD = FRAME_SCRATCH_OFF + 8
SLOT_NBLOCKS = FRAME_SCRATCH_OFF + 16
SLOT_C = FRAME_SCRATCH_OFF + 24
SLOT_CIN = FRAME_SCRATCH_OFF + 32
FRAME_SIZE_RAW = FRAME_SCRATCH_OFF + 40
FRAME_SIZE = (FRAME_SIZE_RAW + 63) AND (NOT 63)

CONST SEGMENT READONLY ALIGN(64) 'CONST'
ALIGN 64
LSidx_unpack DB 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 006h, 007h, 008h, 009h, 00Ah, 00Bh, 00Ch, 00Dh
DB 00Dh, 00Eh, 00Fh, 010h, 011h, 012h, 013h, 014h, 013h, 014h, 015h, 016h, 017h, 018h, 019h, 01Ah
DB 01Ah, 01Bh, 01Ch, 01Dh, 01Eh, 01Fh, 020h, 021h, 020h, 021h, 022h, 023h, 024h, 025h, 026h, 027h
DB 027h, 028h, 029h, 02Ah, 02Bh, 02Ch, 02Dh, 02Eh, 02Dh, 02Eh, 02Fh, 030h, 031h, 032h, 033h, 034h
ALIGN 64
LSshift_common DQ 0, 4, 0, 4, 0, 4, 0, 4
ALIGN 64
LSidx_pack_a DB 000h, 001h, 002h, 003h, 004h, 005h, 006h, 009h, 00Ah, 00Bh, 00Ch, 00Dh, 00Eh, 010h, 011h, 012h
DB 013h, 014h, 015h, 016h, 019h, 01Ah, 01Bh, 01Ch, 01Dh, 01Eh, 020h, 021h, 022h, 023h, 024h, 025h
DB 026h, 029h, 02Ah, 02Bh, 02Ch, 02Dh, 02Eh, 030h, 031h, 032h, 033h, 034h, 035h, 036h, 039h, 03Ah
DB 03Bh, 03Ch, 03Dh, 03Eh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
ALIGN 64
LSidx_pack_b DB 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 008h, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh
DB 03Fh, 03Fh, 03Fh, 018h, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh
DB 028h, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 038h, 03Fh, 03Fh
DB 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh
ALIGN 64
LSm52_bcst DQ 0FFFFFFFFFFFFFh
LSone_bcst DQ 1
LSseven_bcst DQ 7
ALIGN 64
LSdup_idx DQ 0, 0, 1, 1, 2, 2, 3, 3
CONST ENDS

.code

; Masked 64-byte group load helper (identical to the multiply kernel's).
; In: rax=signed byte offset, rdx=source total byte length.
; Out: k1=load mask. Scratch: rcx,r9,r10,rsi,rdi (rax/rdx preserved).
MASK_CALC_64 MACRO
    LOCAL m64_nonneg, m64_clipped
    mov     r10, rax
    test    r10, r10
    jns     m64_nonneg
    xor     r10, r10
m64_nonneg:
    lea     r9, [rax + 64]
    cmp     r9, rdx
    jle     m64_clipped
    mov     r9, rdx
m64_clipped:
    mov     rcx, r9
    sub     rcx, r10
    xor     rsi, rsi
    cmp     rcx, 0
    cmovg   rsi, rcx
    mov     rdi, r10
    sub     rdi, rax
    mov     rcx, -1
    bzhi    rcx, rcx, rsi
    shlx    rcx, rcx, rdi
    kmovq   k1, rcx
ENDM

beman_big_int_square_long_runtime_avx512_ifma PROC FRAME
    test    r8, r8
    jnz     Lsqr_nonzero
    ret                                 ; n == 0; still before any push
Lsqr_nonzero:
    cmp     r8, NATIVE_MAX_LIMBS
    jbe     Lsqr_native
    jmp     beman_big_int_square_long_runtime_bmi2_adx  ; args untouched
Lsqr_native:

    push    rbx
    .pushreg rbx
    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    push    r14
    .pushreg r14
    push    r15
    .pushreg r15
    push    rbp
    .pushreg rbp
    mov     rbp, rsp
    .setframe rbp, 0
    .endprolog
    and     rsp, -64
    sub     rsp, FRAME_SIZE

    mov     r15, rcx                  ; r15 = r (result)
    mov     r14, rdx                  ; r14 = a (source base, read-only)

    mov     rax, r8
    shl     rax, 3
    mov     [rsp + SLOT_NBYTES_A], rax   ; nbytes_a = n*8
    mov     rbx, r8
    shl     rbx, 4                       ; rbx = nbytes_out = 2*n*8

    ; na = (64*n+51)/52 ; na_pad = round_up8(na)
    mov     rax, r8
    shl     rax, 6
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    lea     rcx, [rax + 7]
    and     rcx, -8
    mov     [rsp + SLOT_NA_PAD], rcx

    ; nblocks = (nbytes_out+51)/52
    mov     rax, rbx
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    mov     [rsp + SLOT_NBLOCKS], rax

    vmovdqa64    zmm28, ZMMWORD PTR [LSidx_unpack]
    vmovdqa64    zmm29, ZMMWORD PTR [LSshift_common]
    vpbroadcastq zmm26, QWORD PTR [LSm52_bcst]
    vpbroadcastq zmm27, QWORD PTR [LSone_bcst]

    ; zero head pad [0, PAD_HEAD) and tail pad [PAD_HEAD+na_pad, +32) --
    ; the conversion loop below fully overwrites [PAD_HEAD, PAD_HEAD+na_pad).
    vpxorq  zmm0, zmm0, zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF], zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + 64], zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + 128], zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + 192], zmm0
    mov     r8, [rsp + SLOT_NA_PAD]
    shl     r8, 3                        ; na_pad is a DIGIT count; need bytes
    add     r8, PAD_HEAD * 8
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + r8], zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + r8 + 64], zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + r8 + 128], zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_AD_OFF + r8 + 192], zmm0

    ; convert operand: ngroups = na_pad/8 groups of 8 digits (52 bytes) each
    mov     rax, 0                       ; byte_off (group 0)
    mov     rdx, [rsp + SLOT_NBYTES_A]
    lea     r8, [rsp + FRAME_AD_OFF + PAD_HEAD*8]
    mov     r11, [rsp + SLOT_NA_PAD]
    shr     r11, 3                       ; ngroups
LSconv_loop:
    MASK_CALC_64
    lea     r10, [r14 + rax]
    vmovdqu8   zmm0{k1}{z}, ZMMWORD PTR [r10]
    vpermb     zmm0, zmm28, zmm0
    vpsrlvq    zmm0, zmm0, zmm29
    vmovdqu64  ZMMWORD PTR [r8], zmm0
    add     rax, 52
    add     r8, 64
    dec     r11
    jnz     LSconv_loop

    vpxorq  zmm25, zmm25, zmm25            ; h_prev = 0
    mov     DWORD PTR [rsp + SLOT_CIN], 0
    mov     QWORD PTR [rsp + SLOT_C], 0

; ----------------------------------- block loop ----------------------------
LSblock_loop:
    mov     rax, [rsp + SLOT_C]
    cmp     rax, [rsp + SLOT_NBLOCKS]
    jae     LSblock_done

    lea     rdi, [rsp + FRAME_AD_OFF + PAD_HEAD*8]   ; rdi = ad (base of digit 0)
    mov     r10, rax
    shl     r10, 3                          ; r10 = col0 = 8c
    mov     r11, rax
    shl     r11, 2                          ; r11 = fourc = 4c

    vpxorq  zmm16, zmm16, zmm16
    vpxorq  zmm17, zmm17, zmm17
    vpxorq  zmm18, zmm18, zmm18
    vpxorq  zmm19, zmm19, zmm19
    vpxorq  zmm20, zmm20, zmm20
    vpxorq  zmm21, zmm21, zmm21
    vpxorq  zmm22, zmm22, zmm22
    vpxorq  zmm23, zmm23, zmm23

    ; unrestricted region: j in [lo, fourc-1]
    mov     r8, r10
    sub     r8, [rsp + SLOT_NA_PAD]
    inc     r8
    test    r8, r8
    jns     Lsqr_lo_clip
    xor     r8, r8
Lsqr_lo_clip:
    lea     r9, [r11 - 1]                    ; r9 = end = fourc-1
    cmp     r9, r8
    jl      LSno_unrestricted
    mov     rax, r9
    sub     rax, r8
    inc     rax
    and     rax, 3
    jz      Lsqr_round4_done
    mov     rcx, 4
    sub     rcx, rax
    sub     r8, rcx                          ; extend lo backward (safe: head zero-pad)
Lsqr_round4_done:
    mov     rcx, r8                          ; rcx = j
LSj_loop:
    vpbroadcastq zmm24, QWORD PTR [rdi + rcx*8 - 8]
    vpbroadcastq zmm0,  QWORD PTR [rdi + rcx*8]
    vpbroadcastq zmm1,  QWORD PTR [rdi + rcx*8 + 8]
    vpbroadcastq zmm2,  QWORD PTR [rdi + rcx*8 + 16]
    vpbroadcastq zmm3,  QWORD PTR [rdi + rcx*8 + 24]

    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm16, zmm4, zmm0
    vpmadd52huq zmm20, zmm4, zmm24

    dec     rax
    vmovdqu64 zmm5, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm17, zmm5, zmm1
    vpmadd52huq zmm21, zmm5, zmm0

    dec     rax
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm18, zmm4, zmm2
    vpmadd52huq zmm22, zmm4, zmm1

    dec     rax
    vmovdqu64 zmm5, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm19, zmm5, zmm3
    vpmadd52huq zmm23, zmm5, zmm2

    add     rcx, 4
    cmp     rcx, r9
    jle     LSj_loop
LSno_unrestricted:

    ; boundary region: j = fourc, fourc+1, fourc+2, fourc+3
    mov     rcx, r11                          ; j = fourc
    mov     eax, 0FEh
    kmovb   k1, eax
    mov     eax, 0FFh
    kmovb   k2, eax
    vpbroadcastq zmm0, QWORD PTR [rdi + rcx*8]
    vpbroadcastq zmm1, QWORD PTR [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm16{k1}, zmm4, zmm0
    vpmadd52huq zmm20{k2}, zmm4, zmm1

    inc     rcx
    mov     eax, 0F8h
    kmovb   k1, eax
    mov     eax, 0FCh
    kmovb   k2, eax
    vpbroadcastq zmm0, QWORD PTR [rdi + rcx*8]
    vpbroadcastq zmm1, QWORD PTR [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm17{k1}, zmm4, zmm0
    vpmadd52huq zmm21{k2}, zmm4, zmm1

    inc     rcx
    mov     eax, 0E0h
    kmovb   k1, eax
    mov     eax, 0F0h
    kmovb   k2, eax
    vpbroadcastq zmm0, QWORD PTR [rdi + rcx*8]
    vpbroadcastq zmm1, QWORD PTR [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm18{k1}, zmm4, zmm0
    vpmadd52huq zmm22{k2}, zmm4, zmm1

    inc     rcx
    mov     eax, 080h
    kmovb   k1, eax
    mov     eax, 0C0h
    kmovb   k2, eax
    vpbroadcastq zmm0, QWORD PTR [rdi + rcx*8]
    vpbroadcastq zmm1, QWORD PTR [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + rax*8]
    vpmadd52luq zmm19{k1}, zmm4, zmm0
    vpmadd52huq zmm23{k2}, zmm4, zmm1

    vpaddq  zmm0, zmm16, zmm17
    vpaddq  zmm1, zmm18, zmm19
    vpaddq  zmm2, zmm20, zmm21
    vpaddq  zmm3, zmm22, zmm23
    vpaddq  zmm0, zmm0, zmm1
    vpaddq  zmm2, zmm2, zmm3
    vpaddq  zmm0, zmm0, zmm2               ; zmm0 = S_od
    vpaddq  zmm0, zmm0, zmm0               ; zmm0 = 2*S_od

    ; diagonal: a[fourc..fourc+3], duplicated into lane pairs
    vmovdqu64 zmm4, ZMMWORD PTR [rdi + r11*8]
    vmovdqa64 zmm5, ZMMWORD PTR [LSdup_idx]
    vpermq    zmm4, zmm5, zmm4             ; zmm4 = Ddup
    vpxorq    zmm3, zmm3, zmm3
    mov       eax, 055h                    ; 0b01010101
    kmovb     k1, eax
    mov       eax, 0AAh                    ; 0b10101010
    kmovb     k2, eax
    vpmadd52luq zmm3{k1}, zmm4, zmm4
    vpmadd52huq zmm3{k2}, zmm4, zmm4
    vpaddq    zmm0, zmm0, zmm3             ; zmm0 = S (final raw column sum)

; ------------------------------- recompose ---------------------------------
    vpsrlq   zmm1, zmm0, 52
    vpandq   zmm2, zmm0, zmm26
    valignq  zmm3, zmm1, zmm25, 7
    vpaddq   zmm2, zmm2, zmm3

    vpcmpuq   k3, zmm2, zmm26, 6            ; G: n>M (predicate 6 = unsigned NLE/greater)
    vpcmpeqq  k4, zmm2, zmm26               ; P: n==M
    kmovb   r8d, k3
    kmovb   r9d, k4
    mov     eax, [rsp + SLOT_CIN]
    mov     ecx, r8d
    shl     ecx, 1
    or      ecx, eax
    add     ecx, r9d
    mov     eax, ecx
    xor     eax, r9d
    and     eax, 0FFh
    kmovb   k5, eax
    shr     ecx, 8
    and     ecx, 1
    mov     [rsp + SLOT_CIN], ecx

    vpaddq  zmm2{k5}, zmm2, zmm27
    vpandq  zmm2, zmm2, zmm26

    vpsllvq   zmm4, zmm2, zmm29
    vmovdqa64 zmm30, ZMMWORD PTR [LSidx_pack_a]
    vmovdqa64 zmm31, ZMMWORD PTR [LSidx_pack_b]
    vpermb    zmm5, zmm30, zmm4
    vpermb    zmm4, zmm31, zmm4
    vporq     zmm5, zmm5, zmm4

    mov     rax, [rsp + SLOT_C]
    imul    rdx, rax, 52
    mov     rcx, rbx
    sub     rcx, rdx
    test    rcx, rcx
    jle     LSskip_store
    cmp     rcx, 52
    jle     Lsqr_storelen_clip
    mov     rcx, 52
Lsqr_storelen_clip:
    mov     r8, -1
    bzhi    r8, r8, rcx
    kmovq   k1, r8
    vmovdqu8 ZMMWORD PTR [r15 + rdx]{k1}, zmm5
LSskip_store:
    vpbroadcastq zmm4, QWORD PTR [LSseven_bcst]
    vpermq  zmm25, zmm4, zmm1

    mov     rax, [rsp + SLOT_C]
    inc     rax
    mov     [rsp + SLOT_C], rax
    jmp     LSblock_loop

LSblock_done:
    vzeroupper
    lea     rsp, [rbp]
    pop     rbp
    pop     r15
    pop     r14
    pop     rdi
    pop     rsi
    pop     rbx
    ret
beman_big_int_square_long_runtime_avx512_ifma ENDP

END
