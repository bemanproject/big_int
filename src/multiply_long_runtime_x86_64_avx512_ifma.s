# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

.intel_syntax noprefix
.altmacro

# Empty, but present, GNU-stack section
.section .note.GNU-stack,"",%progbits

# ---------------------------------------------------------------------------
# AVX-512 IFMA (radix 2^52) schoolbook multiply. Contract matches
# beman_big_int_multiply_long_runtime (see
# multiply_long_runtime_x86_64_generic.s): writes exactly la+lb limbs,
# result NOT pre-zeroed, result never aliases a/b, a==b allowed, la==0 or
# lb==0 writes nothing, never reads outside [a,a+la)/[b,b+lb).
#
# DIGITS. Base-2^52, packed as a little-endian bit stream (same convention
# as the 64-bit limb array, re-chunked into 52-bit windows, so the product
# is built directly in this form with no base-2^64 recompose pass). 64->52
# conversion: masked 64-byte load + vpermb + vpsrlvq per 8-digit (52-byte)
# group. Recompose: the inverse (vpsllvq + two vpermb passes + or), packing
# 8 digits into 52 bytes per masked store.
#
# CORE. For output block c (columns 8c..8c+7), j ranges over b-digits with
# lo=max(0,8c-ma_pad+1), hi=min(mb_true,8c+7) -- mb_true, not mb_true-1: the
# H-term at j=mb_true still needs B_(j-1)=bb[mb_true-1] even though L's own
# bb[mb_true]=0 drops out -- rounded up to a multiple of 4 (the extra reads
# land in bb's zero pad). Per j: W=a-digit window [8c-j..8c-j+7]; L+=lo(W*Bj)
# lands in column 8c+lane; H+=hi(W*B_(j-1)) also lands in column 8c+lane
# (the -1 compensates hi's +1-column carry). 8 named zmm accumulators
# (4 L + 4 H), unrolled by 4 j's to hide the 4-cycle IFMA latency.
#
# RECOMPOSE. S = sum of the 8 accumulators; h=S>>52, d=S&M; n = d + (this
# block's h shifted up one lane, lane 0 fed by the previous block's raw
# h[7]); G=(n>M), P=(n==M) as 8-bit k-masks; x=((G<<1)|cin)+P (G/P disjoint
# so x<0x200 -- carry-lookahead); carries=x^P, cout=x>>8; masked +1 on
# carried lanes, &M; pack; masked store clamped to bytes still owed.
# h_prev'=h[7] (raw magnitude; the pending +1 is carried separately as cin).
#
# FRAME (<4KB, independent of la/lb). `a`'s digits are never all resident:
# blocks are grouped into a-chunks of CA=64 digits, each reconverted from
# the original a limbs into a bounded window (ACHUNK_BUF_DIGITS=224 digits)
# anchored to cover mb_true's look-back. mb>128 digits (lb>~104 limbs) runs
# one pass per 128-digit slice of b; passes after the first add into
# blocks already covered by an earlier pass (tracked by frontier_blocks)
# via masked read-back + AND-mask + accumulate, since the frontier only
# ever advances. rbp-based frame: `and rsp,-64` then one fixed
# `sub rsp,FRAME_SIZE`; no further push/pop, so every frame slot is a
# compile-time rsp-relative offset for the rest of the function.
#
# REGISTERS. SysV entry: rdi=r, rsi=a, rdx=la, rcx=b, r8=lb. Persistent:
# r15=r, r14=a, r13=b, r12=frontier_blocks, rbx=nbytes_out; other scalars
# live in the frame (SLOT_*), not pinned. zmm16-23=L0-3,H0-3 (accumulators).
# zmm24=scratch. zmm25=h_prev. zmm26=M52. zmm27=ones. zmm28=idx_unpack.
# zmm29=shift_common. zmm30/31=idx_pack_a/b. zmm0-5=transients. Never uses
# zmm6-15/xmm6-15 (Win64 callee-saved there).
# ---------------------------------------------------------------------------

.set CA, 64                       # a-chunk size, digits (multiple of 8)
.set MB_MAX, 128                  # digits per b-chunk (<=104 limbs)
.set SLACK, 16                    # edge margin, digits (multiple of 8)
.set ACHUNK_BUF_DIGITS, (CA + MB_MAX + 2*SLACK)   # 224 digits

.set FRAME_ACHUNK_OFF, 0
.set FRAME_BB_OFF, (ACHUNK_BUF_DIGITS * 8)                # bb[-1] slot
.set FRAME_BB_BODY_OFF, (FRAME_BB_OFF + 8)                # bb[0]
.set FRAME_SCRATCH_OFF, (FRAME_BB_OFF + (1 + MB_MAX + 8) * 8)
.set SLOT_LA,           FRAME_SCRATCH_OFF
.set SLOT_LB,           FRAME_SCRATCH_OFF + 8
.set SLOT_MB,           FRAME_SCRATCH_OFF + 16
.set SLOT_MA_PAD,       FRAME_SCRATCH_OFF + 24
.set SLOT_NBLOCKS,      FRAME_SCRATCH_OFF + 32
.set SLOT_CHUNKSTART,   FRAME_SCRATCH_OFF + 40
.set SLOT_MBTRUE,       FRAME_SCRATCH_OFF + 48
.set SLOT_OUTBLOCKOFF,  FRAME_SCRATCH_OFF + 56
.set SLOT_LOCALNBLOCKS, FRAME_SCRATCH_OFF + 64
.set SLOT_SKIPDIGITS,   FRAME_SCRATCH_OFF + 72
.set SLOT_ACHUNKBASE,   FRAME_SCRATCH_OFF + 80
.set SLOT_CBEGIN,       FRAME_SCRATCH_OFF + 88
.set SLOT_CEND,         FRAME_SCRATCH_OFF + 96
.set SLOT_C,            FRAME_SCRATCH_OFF + 104
.set SLOT_CIN,          FRAME_SCRATCH_OFF + 112
.set FRAME_SIZE_RAW,    FRAME_SCRATCH_OFF + 120
.set FRAME_SIZE, ((FRAME_SIZE_RAW + 63) & ~63)

.section .rodata
.align 64
Lidx_unpack:
    .byte 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d
    .byte 0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a
    .byte 0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27
    .byte 0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34
.align 64
Lshift_common:
    .quad 0,4,0,4,0,4,0,4
.align 64
Lidx_pack_a:
    .byte 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x10,0x11,0x12
    .byte 0x13,0x14,0x15,0x16,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x20,0x21,0x22,0x23,0x24,0x25
    .byte 0x26,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x39,0x3a
    .byte 0x3b,0x3c,0x3d,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
.align 64
Lidx_pack_b:
    .byte 0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x08,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f
    .byte 0x3f,0x3f,0x3f,0x18,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f
    .byte 0x28,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x38,0x3f,0x3f
    .byte 0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f
.align 64
Lm52_bcst:
    .quad 0xFFFFFFFFFFFFF
Lone_bcst:
    .quad 1
Lseven_bcst:
    .quad 7

.section .text

# Masked 64-byte group load helper. In: rax=signed byte offset (relative to
# a source base), rdx=source total byte length. Out: k1 = load mask
# (fault-suppressing: bytes outside [0,rdx) are masked off, never
# touched). Scratch: rcx,r9,r10,rsi,rdi (rax/rdx preserved). Used by both
# conversion loops and the add-mode read-back; not perf-critical (once per
# 8-digit group, not per limb), so kept general rather than specialized.
.macro MASK_CALC_64
    mov     r10, rax
    test    r10, r10
    jns     71f
    xor     r10, r10
71:
    lea     r9, [rax + 64]
    cmp     r9, rdx
    jle     72f
    mov     r9, rdx
72:
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
.endm

.globl beman_big_int_multiply_long_runtime_avx512_ifma
.type beman_big_int_multiply_long_runtime_avx512_ifma, @function
beman_big_int_multiply_long_runtime_avx512_ifma:
.cfi_startproc
    test    rdx, rdx
    jz      Lret_noop
    test    r8, r8
    jz      Lret_noop

    push    rbp
.cfi_adjust_cfa_offset 8
.cfi_rel_offset rbp, 0
    mov     rbp, rsp
.cfi_def_cfa_register rbp
    push    rbx
.cfi_rel_offset rbx, -8
    push    r12
.cfi_rel_offset r12, -16
    push    r13
.cfi_rel_offset r13, -24
    push    r14
.cfi_rel_offset r14, -32
    push    r15
.cfi_rel_offset r15, -40
    and     rsp, -64
    sub     rsp, FRAME_SIZE

    cmp     rdx, r8
    jae     Lsizes_ok
    xchg    rsi, rcx
    xchg    rdx, r8
Lsizes_ok:
    mov     r15, rdi
    mov     r14, rsi
    mov     r13, rcx
    mov     [rsp + SLOT_LA], rdx
    mov     [rsp + SLOT_LB], r8

    mov     rax, rdx
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

    vmovdqa64    zmm28, [rip + Lidx_unpack]
    vmovdqa64    zmm29, [rip + Lshift_common]
    vpbroadcastq zmm26, [rip + Lm52_bcst]
    vpbroadcastq zmm27, [rip + Lone_bcst]

    xor     r12, r12
    mov     qword ptr [rsp + SLOT_CHUNKSTART], 0

# =========================== b-chunk pass loop ============================
Lbchunk_loop:
    mov     rax, [rsp + SLOT_CHUNKSTART]
    cmp     rax, [rsp + SLOT_MB]
    jae     Lbchunk_done

    # mb_true = min(MB_MAX, mb - chunk_start)
    mov     rax, [rsp + SLOT_MB]
    sub     rax, [rsp + SLOT_CHUNKSTART]
    mov     rcx, MB_MAX
    cmp     rax, rcx
    cmovg   rax, rcx
    mov     [rsp + SLOT_MBTRUE], rax

    # lookback = round_up8(mb_true+SLACK); skip_digits = (MB_MAX+SLACK)-lookback
    add     rax, SLACK
    add     rax, 7
    and     rax, -8
    mov     rcx, (MB_MAX + SLACK)
    sub     rcx, rax
    mov     [rsp + SLOT_SKIPDIGITS], rcx

    # out_block_offset = chunk_start/8
    mov     rax, [rsp + SLOT_CHUNKSTART]
    shr     rax, 3
    mov     [rsp + SLOT_OUTBLOCKOFF], rax

    # local_nblocks = min((ma_pad+mb_true+7)/8, nblocks_total-out_block_offset)
    mov     rax, [rsp + SLOT_MA_PAD]
    add     rax, [rsp + SLOT_MBTRUE]
    add     rax, 7
    shr     rax, 3
    mov     rcx, [rsp + SLOT_NBLOCKS]
    sub     rcx, [rsp + SLOT_OUTBLOCKOFF]
    cmp     rax, rcx
    cmovg   rax, rcx
    mov     [rsp + SLOT_LOCALNBLOCKS], rax

    # convert bb: 16 groups (128 digits) from b at digit offset chunk_start
    mov     qword ptr [rsp + FRAME_BB_OFF], 0
    vpxorq  zmm0, zmm0, zmm0
    vmovdqu64 [rsp + FRAME_BB_BODY_OFF + 1024], zmm0

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
    vmovdqu8   zmm0{k1}{z}, [r10]
    vpermb     zmm0, zmm28, zmm0
    vpsrlvq    zmm0, zmm0, zmm29
    vmovdqu64  [r8], zmm0
    add     rax, 52
    add     r8, 64
    dec     r11
    jnz     Lbb_conv_loop

    vpxorq  zmm25, zmm25, zmm25            # h_prev = 0
    mov     dword ptr [rsp + SLOT_CIN], 0
    mov     rax, [rsp + SLOT_OUTBLOCKOFF]
    mov     [rsp + SLOT_CBEGIN], rax

# ------------------------------- a-chunk loop ------------------------------
Lachunk_loop:
    mov     rax, [rsp + SLOT_CBEGIN]
    mov     rcx, [rsp + SLOT_OUTBLOCKOFF]
    mov     rdx, [rsp + SLOT_LOCALNBLOCKS]
    add     rdx, rcx                        # rdx = out_block_offset + local_nblocks (end, absolute)
    cmp     rax, rdx
    jae     Lachunk_done

    mov     rax, [rsp + SLOT_CBEGIN]
    sub     rax, [rsp + SLOT_OUTBLOCKOFF]    # rax = c_local (local block index within this pass)
    lea     r10, [rax*8 - (MB_MAX + SLACK)]  # achunk_base (local digit numbering)
    mov     [rsp + SLOT_ACHUNKBASE], r10

    mov     rcx, [rsp + SLOT_SKIPDIGITS]
    mov     r11, rcx
    shr     r11, 3
    xor     r9, r9
    test    r11, r11
    jz      Lachunk_zero_done
    vpxorq  zmm0, zmm0, zmm0
Lachunk_zero_loop:
    vmovdqu64 [rsp + FRAME_ACHUNK_OFF + r9], zmm0
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
    vmovdqu8   zmm0{k1}{z}, [r10]
    vpermb     zmm0, zmm28, zmm0
    vpsrlvq    zmm0, zmm0, zmm29
    vmovdqu64  [r8], zmm0
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
    jle     73f
    mov     r8, rdx
73:
    mov     [rsp + SLOT_CEND], r8

# --------------------------------- block loop ------------------------------
Lblock_loop:
    mov     rax, [rsp + SLOT_C]
    cmp     rax, [rsp + SLOT_CEND]
    jae     Lblock_loop_done

    mov     r10, [rsp + SLOT_ACHUNKBASE]
    shl     r10, 3
    mov     rdi, rsp
    sub     rdi, r10                        # rdi = EFF
    lea     rsi, [rsp + FRAME_BB_BODY_OFF]  # rsi = bb pointer

    mov     r10, rax
    sub     r10, [rsp + SLOT_OUTBLOCKOFF]
    shl     r10, 3                           # r10 = col0 (LOCAL, relative to this pass)

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
    jns     74f
    xor     r8, r8
74:
    lea     r9, [r10 + 7]
    mov     rax, [rsp + SLOT_MBTRUE]
    cmp     r9, rax
    jle     75f
    mov     r9, rax
75:
    cmp     r9, r8
    jl      Lblock_empty
    mov     rax, r9
    sub     rax, r8
    inc     rax
    and     rax, 3
    jz      76f
    mov     rcx, 4
    sub     rcx, rax
    add     r9, rcx
76:
    mov     rcx, r8
Lj_loop:
    vpbroadcastq zmm24, qword ptr [rsi + rcx*8 - 8]
    vpbroadcastq zmm0,  qword ptr [rsi + rcx*8]
    vpbroadcastq zmm1,  qword ptr [rsi + rcx*8 + 8]
    vpbroadcastq zmm2,  qword ptr [rsi + rcx*8 + 16]
    vpbroadcastq zmm3,  qword ptr [rsi + rcx*8 + 24]

    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, [rdi + rax*8]
    vpmadd52luq zmm16, zmm4, zmm0
    vpmadd52huq zmm20, zmm4, zmm24

    dec     rax
    vmovdqu64 zmm5, [rdi + rax*8]
    vpmadd52luq zmm17, zmm5, zmm1
    vpmadd52huq zmm21, zmm5, zmm0

    dec     rax
    vmovdqu64 zmm4, [rdi + rax*8]
    vpmadd52luq zmm18, zmm4, zmm2
    vpmadd52huq zmm22, zmm4, zmm1

    dec     rax
    vmovdqu64 zmm5, [rdi + rax*8]
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
    jle     77f
    mov     r9, rbx
77:
    mov     rcx, r9
    sub     rcx, r10
    mov     r8, -1
    bzhi    r8, r8, rcx
    kmovq   k1, r8
    vmovdqu8   zmm4{k1}{z}, [r15 + rdx]
    vpermb     zmm4, zmm28, zmm4
    vpsrlvq    zmm4, zmm4, zmm29
    vpandq     zmm4, zmm4, zmm26
    vpaddq     zmm0, zmm0, zmm4
Lno_seed:
    vpsrlq   zmm1, zmm0, 52
    vpandq   zmm2, zmm0, zmm26
    valignq  zmm3, zmm1, zmm25, 7
    vpaddq   zmm2, zmm2, zmm3

    vpcmpuq   k2, zmm2, zmm26, 6      # predicate 6 = NLE = unsigned greater-than
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
    and     eax, 0xFF
    kmovb   k4, eax
    shr     ecx, 8
    and     ecx, 1
    mov     [rsp + SLOT_CIN], ecx

    vpaddq  zmm2{k4}, zmm2, zmm27
    vpandq  zmm2, zmm2, zmm26

    vpsllvq   zmm4, zmm2, zmm29
    vmovdqa64 zmm30, [rip + Lidx_pack_a]
    vmovdqa64 zmm31, [rip + Lidx_pack_b]
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
    jle     78f
    mov     rcx, 52
78:
    mov     r8, -1
    bzhi    r8, r8, rcx
    kmovq   k1, r8
    vmovdqu8 [r15 + rdx]{k1}, zmm5
Lskip_store:
    vpbroadcastq zmm4, qword ptr [rip + Lseven_bcst]
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
    jle     79f
    mov     r12, rax
79:
    mov     rax, [rsp + SLOT_CHUNKSTART]
    add     rax, MB_MAX
    mov     [rsp + SLOT_CHUNKSTART], rax
    jmp     Lbchunk_loop

Lbchunk_done:
    vzeroupper
    lea     rsp, [rbp - 40]
    pop     r15
.cfi_restore r15
    pop     r14
.cfi_restore r14
    pop     r13
.cfi_restore r13
    pop     r12
.cfi_restore r12
    pop     rbx
.cfi_restore rbx
    pop     rbp
.cfi_def_cfa rsp, 8
.cfi_restore rbp
Lret_noop:
    ret
.cfi_endproc
.size beman_big_int_multiply_long_runtime_avx512_ifma, .-beman_big_int_multiply_long_runtime_avx512_ifma
