; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SPDX-License-Identifier: BSL-1.0

; AVX-512 IFMA (radix 2^52) schoolbook multiply. See
; multiply_long_runtime_x86_64_avx512_ifma.s for the full design (the
; digit-domain algorithm, the bounded a-chunk/b-chunk frame, the carry
; lookahead). This is a straight port to the Win64 calling convention.
;
; Microsoft x64 calling convention: rcx=p_result, rdx=p_a, r8=len_a,
; r9=p_b, [rsp+40]=len_b (5th argument; read before any push).
;
; Win64-specific differences from the SysV .s:
;   - rsi and rdi are callee-saved on Win64 (caller-saved in SysV), so both
;     are pushed alongside rbx, rbp, r12-r15; the .s already avoids
;     xmm6-15/zmm6-15 entirely (only zmm0-5 and zmm16-31), so no xmm save
;     area is needed.
;   - rbp is pushed last and serves as the frame pointer (.setframe rbp,0);
;     the epilogue recovers rsp with `lea rsp, [rbp]` (rbp, pushed last,
;     already points at its own saved slot, the lowest address pushed) --
;     one of the two documented-legal Win64 epilogue forms (`add rsp,imm`
;     or `lea rsp,[fpreg+disp]`; a bare `mov rsp,rbp` is not), so unwinding
;     stays valid at every instruction across the `and rsp,-64` / fixed
;     `sub rsp,FRAME_SIZE` realignment.
;   - the 4 lookup tables and 3 broadcast constants move into a 64-byte
;     aligned CONST segment, referenced RIP-relative (ml64 does this
;     automatically for a bare-label memory operand).
;   - GAS's reusable numeric local labels (71:..79:) become named labels;
;     the two reused inside MASK_CALC_64 (expanded twice) become MASM
;     LOCAL labels so each expansion gets its own symbol.
;   - the la==0/lb==0 early-outs each `ret` in place, right where they are
;     tested (before any push), instead of jumping forward to a shared
;     return past .endprolog: a PC there would be treated as "fully
;     pushed" by the unwinder even though nothing has been pushed yet.

CA = 64
MB_MAX = 128
SLACK = 16
ACHUNK_BUF_DIGITS = CA + MB_MAX + 2*SLACK

FRAME_ACHUNK_OFF = 0
FRAME_BB_OFF = ACHUNK_BUF_DIGITS * 8
FRAME_BB_BODY_OFF = FRAME_BB_OFF + 8
FRAME_SCRATCH_OFF = FRAME_BB_OFF + (1 + MB_MAX + 8) * 8
SLOT_LA = FRAME_SCRATCH_OFF
SLOT_LB = FRAME_SCRATCH_OFF + 8
SLOT_MB = FRAME_SCRATCH_OFF + 16
SLOT_MA_PAD = FRAME_SCRATCH_OFF + 24
SLOT_NBLOCKS = FRAME_SCRATCH_OFF + 32
SLOT_CHUNKSTART = FRAME_SCRATCH_OFF + 40
SLOT_MBTRUE = FRAME_SCRATCH_OFF + 48
SLOT_OUTBLOCKOFF = FRAME_SCRATCH_OFF + 56
SLOT_LOCALNBLOCKS = FRAME_SCRATCH_OFF + 64
SLOT_SKIPDIGITS = FRAME_SCRATCH_OFF + 72
SLOT_ACHUNKBASE = FRAME_SCRATCH_OFF + 80
SLOT_CBEGIN = FRAME_SCRATCH_OFF + 88
SLOT_CEND = FRAME_SCRATCH_OFF + 96
SLOT_C = FRAME_SCRATCH_OFF + 104
SLOT_CIN = FRAME_SCRATCH_OFF + 112
FRAME_SIZE_RAW = FRAME_SCRATCH_OFF + 120
FRAME_SIZE = (FRAME_SIZE_RAW + 63) AND (NOT 63)

CONST SEGMENT READONLY ALIGN(64) 'CONST'
ALIGN 64
Lidx_unpack DB 000h, 001h, 002h, 003h, 004h, 005h, 006h, 007h, 006h, 007h, 008h, 009h, 00Ah, 00Bh, 00Ch, 00Dh
DB 00Dh, 00Eh, 00Fh, 010h, 011h, 012h, 013h, 014h, 013h, 014h, 015h, 016h, 017h, 018h, 019h, 01Ah
DB 01Ah, 01Bh, 01Ch, 01Dh, 01Eh, 01Fh, 020h, 021h, 020h, 021h, 022h, 023h, 024h, 025h, 026h, 027h
DB 027h, 028h, 029h, 02Ah, 02Bh, 02Ch, 02Dh, 02Eh, 02Dh, 02Eh, 02Fh, 030h, 031h, 032h, 033h, 034h
ALIGN 64
Lshift_common DQ 0, 4, 0, 4, 0, 4, 0, 4
ALIGN 64
Lidx_pack_a DB 000h, 001h, 002h, 003h, 004h, 005h, 006h, 009h, 00Ah, 00Bh, 00Ch, 00Dh, 00Eh, 010h, 011h, 012h
DB 013h, 014h, 015h, 016h, 019h, 01Ah, 01Bh, 01Ch, 01Dh, 01Eh, 020h, 021h, 022h, 023h, 024h, 025h
DB 026h, 029h, 02Ah, 02Bh, 02Ch, 02Dh, 02Eh, 030h, 031h, 032h, 033h, 034h, 035h, 036h, 039h, 03Ah
DB 03Bh, 03Ch, 03Dh, 03Eh, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h, 000h
ALIGN 64
Lidx_pack_b DB 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 008h, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh
DB 03Fh, 03Fh, 03Fh, 018h, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh
DB 028h, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 038h, 03Fh, 03Fh
DB 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh, 03Fh
ALIGN 64
Lm52_bcst DQ 0FFFFFFFFFFFFFh
Lone_bcst DQ 1
Lseven_bcst DQ 7
CONST ENDS

.code

; Masked 64-byte group load helper. In: rax=signed byte offset (relative to
; a source base), rdx=source total byte length. Out: k1 = load mask
; (fault-suppressing: bytes outside [0,rdx) are masked off, never
; touched). Scratch: rcx,r9,r10,rsi,rdi (rax/rdx preserved).
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

beman_big_int_multiply_long_runtime_avx512_ifma PROC FRAME
    mov     r10, QWORD PTR [rsp + 40]   ; len_b (5th arg; no pushes yet, offset = 40)
    test    r8, r8
    jnz     Lmul_la_nonzero
    ret                                 ; early return; still before any push
Lmul_la_nonzero:
    test    r10, r10
    jnz     Lmul_go
    ret                                 ; ditto
Lmul_go:
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
    push    rbp
    .pushreg rbp
    mov     rbp, rsp
    .setframe rbp, 0
    .endprolog
    and     rsp, -64
    sub     rsp, FRAME_SIZE

    cmp     r8, r10
    jae     Lsizes_ok
    xchg    rdx, r9
    xchg    r8, r10
Lsizes_ok:
    mov     r15, rcx
    mov     r14, rdx
    mov     r13, r9
    mov     [rsp + SLOT_LA], r8
    mov     [rsp + SLOT_LB], r10

    mov     rax, r8
    shl     rax, 6
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    lea     rcx, [rax + 7]
    and     rcx, -8
    mov     [rsp + SLOT_MA_PAD], rcx

    mov     rax, [rsp + SLOT_LB]
    shl     rax, 6
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    mov     [rsp + SLOT_MB], rax

    mov     rax, [rsp + SLOT_LA]
    add     rax, [rsp + SLOT_LB]
    shl     rax, 3
    mov     rbx, rax
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    mov     [rsp + SLOT_NBLOCKS], rax

    vmovdqa64    zmm28, ZMMWORD PTR [Lidx_unpack]
    vmovdqa64    zmm29, ZMMWORD PTR [Lshift_common]
    vpbroadcastq zmm26, QWORD PTR [Lm52_bcst]
    vpbroadcastq zmm27, QWORD PTR [Lone_bcst]

    xor     r12, r12
    mov     QWORD PTR [rsp + SLOT_CHUNKSTART], 0

; =========================== b-chunk pass loop ============================
Lbchunk_loop:
    mov     rax, [rsp + SLOT_CHUNKSTART]
    cmp     rax, [rsp + SLOT_MB]
    jae     Lbchunk_done

    ; mb_true = min(MB_MAX, mb - chunk_start)
    mov     rax, [rsp + SLOT_MB]
    sub     rax, [rsp + SLOT_CHUNKSTART]
    mov     rcx, MB_MAX
    cmp     rax, rcx
    cmovg   rax, rcx
    mov     [rsp + SLOT_MBTRUE], rax

    ; lookback = round_up8(mb_true+SLACK); skip_digits = (MB_MAX+SLACK)-lookback
    add     rax, SLACK
    add     rax, 7
    and     rax, -8
    mov     rcx, (MB_MAX + SLACK)
    sub     rcx, rax
    mov     [rsp + SLOT_SKIPDIGITS], rcx

    ; out_block_offset = chunk_start/8
    mov     rax, [rsp + SLOT_CHUNKSTART]
    shr     rax, 3
    mov     [rsp + SLOT_OUTBLOCKOFF], rax

    ; local_nblocks = min((ma_pad+mb_true+7)/8, nblocks_total-out_block_offset)
    mov     rax, [rsp + SLOT_MA_PAD]
    add     rax, [rsp + SLOT_MBTRUE]
    add     rax, 7
    shr     rax, 3
    mov     rcx, [rsp + SLOT_NBLOCKS]
    sub     rcx, [rsp + SLOT_OUTBLOCKOFF]
    cmp     rax, rcx
    cmovg   rax, rcx
    mov     [rsp + SLOT_LOCALNBLOCKS], rax

    ; convert bb: 16 groups (128 digits) from b at digit offset chunk_start
    mov     QWORD PTR [rsp + FRAME_BB_OFF], 0
    vpxorq  zmm0, zmm0, zmm0
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_BB_BODY_OFF + 1024], zmm0

    mov     rax, [rsp + SLOT_CHUNKSTART]
    shr     rax, 3
    imul    rax, rax, 52
    mov     rdx, [rsp + SLOT_LB]
    shl     rdx, 3
    lea     r8, [rsp + FRAME_BB_BODY_OFF]
    mov     r11, 16
Lbb_conv_loop:
    MASK_CALC_64
    lea     r10, [r13 + rax]
    vmovdqu8   zmm0{k1}{z}, ZMMWORD PTR [r10]
    vpermb     zmm0, zmm28, zmm0
    vpsrlvq    zmm0, zmm0, zmm29
    vmovdqu64  ZMMWORD PTR [r8], zmm0
    add     rax, 52
    add     r8, 64
    dec     r11
    jnz     Lbb_conv_loop

    vpxorq  zmm25, zmm25, zmm25            ; h_prev = 0
    mov     DWORD PTR [rsp + SLOT_CIN], 0
    mov     rax, [rsp + SLOT_OUTBLOCKOFF]
    mov     [rsp + SLOT_CBEGIN], rax

; ------------------------------- a-chunk loop ------------------------------
Lachunk_loop:
    mov     rax, [rsp + SLOT_CBEGIN]
    mov     rcx, [rsp + SLOT_OUTBLOCKOFF]
    mov     rdx, [rsp + SLOT_LOCALNBLOCKS]
    add     rdx, rcx                        ; rdx = out_block_offset + local_nblocks (end, absolute)
    cmp     rax, rdx
    jae     Lachunk_done

    mov     rax, [rsp + SLOT_CBEGIN]
    sub     rax, [rsp + SLOT_OUTBLOCKOFF]    ; rax = c_local (local block index within this pass)
    lea     r10, [rax*8 - (MB_MAX + SLACK)]  ; achunk_base (local digit numbering)
    mov     [rsp + SLOT_ACHUNKBASE], r10

    mov     rcx, [rsp + SLOT_SKIPDIGITS]
    mov     r11, rcx
    shr     r11, 3
    xor     r9, r9
    test    r11, r11
    jz      Lachunk_zero_done
    vpxorq  zmm0, zmm0, zmm0
Lachunk_zero_loop:
    vmovdqu64 ZMMWORD PTR [rsp + FRAME_ACHUNK_OFF + r9], zmm0
    add     r9, 64
    dec     r11
    jnz     Lachunk_zero_loop
Lachunk_zero_done:
    mov     rax, [rsp + SLOT_ACHUNKBASE]
    add     rax, rcx
    sar     rax, 3
    imul    rax, rax, 52
    mov     rdx, [rsp + SLOT_LA]
    shl     rdx, 3
    mov     r9, rcx
    shl     r9, 3
    lea     r8, [rsp + FRAME_ACHUNK_OFF + r9]
    mov     r11, ACHUNK_BUF_DIGITS
    sub     r11, rcx
    shr     r11, 3
    test    r11, r11
    jz      Lachunk_conv_done
Lachunk_conv_loop:
    MASK_CALC_64
    lea     r10, [r14 + rax]
    vmovdqu8   zmm0{k1}{z}, ZMMWORD PTR [r10]
    vpermb     zmm0, zmm28, zmm0
    vpsrlvq    zmm0, zmm0, zmm29
    vmovdqu64  ZMMWORD PTR [r8], zmm0
    add     rax, 52
    add     r8, 64
    dec     r11
    jnz     Lachunk_conv_loop
Lachunk_conv_done:

    mov     rax, [rsp + SLOT_CBEGIN]
    mov     [rsp + SLOT_C], rax
    lea     r8, [rax + 8]
    mov     rcx, [rsp + SLOT_OUTBLOCKOFF]
    mov     rdx, [rsp + SLOT_LOCALNBLOCKS]
    add     rdx, rcx
    cmp     r8, rdx
    jle     Lcend_clip
    mov     r8, rdx
Lcend_clip:
    mov     [rsp + SLOT_CEND], r8

; --------------------------------- block loop ------------------------------
Lblock_loop:
    mov     rax, [rsp + SLOT_C]
    cmp     rax, [rsp + SLOT_CEND]
    jae     Lblock_loop_done

    mov     r10, [rsp + SLOT_ACHUNKBASE]
    shl     r10, 3
    mov     rdi, rsp
    sub     rdi, r10                        ; rdi = EFF
    lea     rsi, [rsp + FRAME_BB_BODY_OFF]  ; rsi = bb pointer

    mov     r10, rax
    sub     r10, [rsp + SLOT_OUTBLOCKOFF]
    shl     r10, 3                           ; r10 = col0 (LOCAL, relative to this pass)

    vpxorq  zmm16, zmm16, zmm16
    vpxorq  zmm17, zmm17, zmm17
    vpxorq  zmm18, zmm18, zmm18
    vpxorq  zmm19, zmm19, zmm19
    vpxorq  zmm20, zmm20, zmm20
    vpxorq  zmm21, zmm21, zmm21
    vpxorq  zmm22, zmm22, zmm22
    vpxorq  zmm23, zmm23, zmm23

    mov     r8, r10
    sub     r8, [rsp + SLOT_MA_PAD]
    inc     r8
    test    r8, r8
    jns     Llo_clip
    xor     r8, r8
Llo_clip:
    lea     r9, [r10 + 7]
    mov     rax, [rsp + SLOT_MBTRUE]
    cmp     r9, rax
    jle     Lhi_clip
    mov     r9, rax
Lhi_clip:
    cmp     r9, r8
    jl      Lblock_empty
    mov     rax, r9
    sub     rax, r8
    inc     rax
    and     rax, 3
    jz      Lround4_done
    mov     rcx, 4
    sub     rcx, rax
    add     r9, rcx
Lround4_done:
    mov     rcx, r8
Lj_loop:
    vpbroadcastq zmm24, QWORD PTR [rsi + rcx*8 - 8]
    vpbroadcastq zmm0,  QWORD PTR [rsi + rcx*8]
    vpbroadcastq zmm1,  QWORD PTR [rsi + rcx*8 + 8]
    vpbroadcastq zmm2,  QWORD PTR [rsi + rcx*8 + 16]
    vpbroadcastq zmm3,  QWORD PTR [rsi + rcx*8 + 24]

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
    jle     Lj_loop

    vpaddq  zmm0, zmm16, zmm17
    vpaddq  zmm1, zmm18, zmm19
    vpaddq  zmm2, zmm20, zmm21
    vpaddq  zmm3, zmm22, zmm23
    vpaddq  zmm0, zmm0, zmm1
    vpaddq  zmm2, zmm2, zmm3
    vpaddq  zmm0, zmm0, zmm2
    jmp     Lblock_recompose
Lblock_empty:
    vpxorq  zmm0, zmm0, zmm0
Lblock_recompose:
    mov     rax, [rsp + SLOT_C]
    cmp     rax, r12
    jae     Lno_seed
    imul    rdx, rax, 52
    mov     r10, rdx
    lea     r9, [rdx + 64]
    cmp     r9, rbx
    jle     Lseed_clip
    mov     r9, rbx
Lseed_clip:
    mov     rcx, r9
    sub     rcx, r10
    mov     r8, -1
    bzhi    r8, r8, rcx
    kmovq   k1, r8
    vmovdqu8   zmm4{k1}{z}, ZMMWORD PTR [r15 + rdx]
    vpermb     zmm4, zmm28, zmm4
    vpsrlvq    zmm4, zmm4, zmm29
    vpandq     zmm4, zmm4, zmm26
    vpaddq     zmm0, zmm0, zmm4
Lno_seed:
    vpsrlq   zmm1, zmm0, 52
    vpandq   zmm2, zmm0, zmm26
    valignq  zmm3, zmm1, zmm25, 7
    vpaddq   zmm2, zmm2, zmm3

    vpcmpuq   k2, zmm2, zmm26, 6      ; predicate 6 = NLE = unsigned greater-than
    vpcmpeqq  k3, zmm2, zmm26
    kmovb   r8d, k2
    kmovb   r9d, k3
    mov     eax, [rsp + SLOT_CIN]
    mov     ecx, r8d
    shl     ecx, 1
    or      ecx, eax
    add     ecx, r9d
    mov     eax, ecx
    xor     eax, r9d
    and     eax, 0FFh
    kmovb   k4, eax
    shr     ecx, 8
    and     ecx, 1
    mov     [rsp + SLOT_CIN], ecx

    vpaddq  zmm2{k4}, zmm2, zmm27
    vpandq  zmm2, zmm2, zmm26

    vpsllvq   zmm4, zmm2, zmm29
    vmovdqa64 zmm30, ZMMWORD PTR [Lidx_pack_a]
    vmovdqa64 zmm31, ZMMWORD PTR [Lidx_pack_b]
    vpermb    zmm5, zmm30, zmm4
    vpermb    zmm4, zmm31, zmm4
    vporq     zmm5, zmm5, zmm4

    mov     rax, [rsp + SLOT_C]
    imul    rdx, rax, 52
    mov     rcx, rbx
    sub     rcx, rdx
    test    rcx, rcx
    jle     Lskip_store
    cmp     rcx, 52
    jle     Lstorelen_clip
    mov     rcx, 52
Lstorelen_clip:
    mov     r8, -1
    bzhi    r8, r8, rcx
    kmovq   k1, r8
    vmovdqu8 ZMMWORD PTR [r15 + rdx]{k1}, zmm5
Lskip_store:
    vpbroadcastq zmm4, QWORD PTR [Lseven_bcst]
    vpermq  zmm25, zmm4, zmm1

    mov     rax, [rsp + SLOT_C]
    inc     rax
    mov     [rsp + SLOT_C], rax
    jmp     Lblock_loop
Lblock_loop_done:
    mov     rax, [rsp + SLOT_CBEGIN]
    add     rax, 8
    mov     [rsp + SLOT_CBEGIN], rax
    jmp     Lachunk_loop

Lachunk_done:
    mov     rax, [rsp + SLOT_OUTBLOCKOFF]
    add     rax, [rsp + SLOT_LOCALNBLOCKS]
    cmp     rax, r12
    jle     Lfrontier_max
    mov     r12, rax
Lfrontier_max:
    mov     rax, [rsp + SLOT_CHUNKSTART]
    add     rax, MB_MAX
    mov     [rsp + SLOT_CHUNKSTART], rax
    jmp     Lbchunk_loop

Lbchunk_done:
    vzeroupper
    lea     rsp, [rbp]
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret
beman_big_int_multiply_long_runtime_avx512_ifma ENDP

END
