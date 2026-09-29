# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

.intel_syntax noprefix
.altmacro

.section .note.GNU-stack,"",%progbits

# ---------------------------------------------------------------------------
# AVX-512 IFMA (radix 2^52) schoolbook squaring. Contract matches
# beman_big_int_square_long_runtime (see square_long_runtime_x86_64_generic.s
# / _bmi2_adx.s): writes exactly 2n limbs, result NOT pre-zeroed, result
# never aliases a, n==0 writes nothing, never reads outside [a,a+n). n>256
# limbs (na_pad would exceed the native buffer) tail-jumps straight to
# beman_big_int_square_long_runtime_bmi2_adx@PLT, before any frame setup (a
# plain jmp, not call+ret, so it returns straight to our caller).
#
# DIGITS/RECOMPOSE. Same digit format, 64<->52 conversion, and
# carry-lookahead recompose as multiply_long_runtime_x86_64_avx512_ifma.s
# (same rodata tables, up to naming). n<=256 limbs means na_pad<=320
# digits, so the whole operand's digit buffer stays resident in one linear
# array (no a-chunking needed).
#
# CORE. a^2 = sum_i a_i^2*2^(104i) + 2*sum_{i<j} a_i*a_j*2^(52(i+j)): the
# off-diagonal cross terms are each counted once as an unordered pair,
# worth double; the diagonal terms are counted once, undoubled. Block c
# (columns 8c..8c+7) reuses the multiply engine's W/B scan (both W and B
# drawn from the same digit buffer) restricted to j < i (each unordered
# pair {i,j} scanned once, as the instance with j < i): for lane k,
# i = 8c-j+k, so j < i <=> j < 4c+k/2, giving a per-lane cutoff that is
# exactly 4c+3 at its highest (k=7), so the scan only needs j up to 4c+3.
# j in [lo, 4c-1] is uniformly off-diagonal for all 8 lanes (identical
# shape to multiply's inner loop); j in {4c,4c+1,4c+2,4c+3} is a mixed
# boundary needing a fixed per-j merge-mask (solving j < 4c+k/2 per lane):
#   j=4c:   L-mask=0xFE (lanes 1-7)   H-mask=0xFF (lanes 0-7)
#   j=4c+1: L-mask=0xF8 (lanes 3-7)   H-mask=0xFC (lanes 2-7)
#   j=4c+2: L-mask=0xE0 (lanes 5-7)   H-mask=0xF0 (lanes 4-7)
#   j=4c+3: L-mask=0x80 (lane  7)     H-mask=0xC0 (lanes 6-7)
# (H's mask is L's mask for j+1 -- the one-digit shift the H_(j-1) trick
# always carries.) The off-diagonal sum is doubled with a plain vpaddq
# (headroom is ample). The diagonal adds a_(4c)..a_(4c+3), each duplicated
# into two adjacent lanes via a register-index vpermq (lanes 2m,2m+1 <-
# a_(4c+m)), then two masked IFMA ops squaring that duplicated vector: lo
# into even lanes (0b01010101), hi into odd lanes (0b10101010).
#
# FRAME. rbp-based, as in the multiply kernel: `mov rbp,rsp` then
# `and rsp,-64` then one fixed `sub rsp,FRAME_SIZE` (<4KB; a single linear
# digit buffer, no per-chunk reconversion).
#
# REGISTERS. SysV entry: rdi=r, rsi=a, rdx=n. Persistent: r15=r, r14=a
# (read-only), rbx=nbytes_out. zmm assignment mirrors the multiply kernel:
# zmm16-23=L0-3,H0-3 (accumulators). zmm24=scratch (Bprev / diagonal
# duplicate). zmm25=h_prev. zmm26=M52. zmm27=ones. zmm28=idx_unpack.
# zmm29=shift_common. zmm30/31=idx_pack_a/b. zmm0-5=transients. Never uses
# zmm6-15/xmm6-15.
# ---------------------------------------------------------------------------

.set NATIVE_MAX_LIMBS, 256
.set AD_BUF_DIGITS, 384          # >= PAD_HEAD + 320 (na_pad max) + PAD_TAIL
.set PAD_HEAD, 32
.set PAD_TAIL, 32

.set FRAME_AD_OFF, 0
.set FRAME_SCRATCH_OFF, (AD_BUF_DIGITS * 8)
.set SLOT_NBYTES_A,  FRAME_SCRATCH_OFF
.set SLOT_NA_PAD,    FRAME_SCRATCH_OFF + 8
.set SLOT_NBLOCKS,   FRAME_SCRATCH_OFF + 16
.set SLOT_C,         FRAME_SCRATCH_OFF + 24
.set SLOT_CIN,       FRAME_SCRATCH_OFF + 32
.set FRAME_SIZE_RAW, FRAME_SCRATCH_OFF + 40
.set FRAME_SIZE, ((FRAME_SIZE_RAW + 63) & ~63)

.section .rodata
.align 64
LSidx_unpack:
    .byte 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d
    .byte 0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a
    .byte 0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27
    .byte 0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34
.align 64
LSshift_common:
    .quad 0,4,0,4,0,4,0,4
.align 64
LSidx_pack_a:
    .byte 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x10,0x11,0x12
    .byte 0x13,0x14,0x15,0x16,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x20,0x21,0x22,0x23,0x24,0x25
    .byte 0x26,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x39,0x3a
    .byte 0x3b,0x3c,0x3d,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
.align 64
LSidx_pack_b:
    .byte 0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x08,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f
    .byte 0x3f,0x3f,0x3f,0x18,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f
    .byte 0x28,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x38,0x3f,0x3f
    .byte 0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f
.align 64
LSm52_bcst:
    .quad 0xFFFFFFFFFFFFF
LSone_bcst:
    .quad 1
LSseven_bcst:
    .quad 7
.align 64
LSdup_idx:
    .quad 0,0,1,1,2,2,3,3

.section .text

# Masked 64-byte group load helper (identical to the multiply kernel's).
# In: rax=signed byte offset, rdx=source total byte length.
# Out: k1=load mask. Scratch: rcx,r9,r10,rsi,rdi (rax/rdx preserved).
.macro MASK_CALC_64
    mov     r10, rax
    test    r10, r10
    jns     81f
    xor     r10, r10
81:
    lea     r9, [rax + 64]
    cmp     r9, rdx
    jle     82f
    mov     r9, rdx
82:
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

.globl beman_big_int_square_long_runtime_avx512_ifma
.type beman_big_int_square_long_runtime_avx512_ifma, @function
beman_big_int_square_long_runtime_avx512_ifma:
.cfi_startproc
    test    rdx, rdx
    jz      LSret_noop
    cmp     rdx, NATIVE_MAX_LIMBS
    ja      LStail_jump

    push    rbp
.cfi_adjust_cfa_offset 8
.cfi_rel_offset rbp, 0
    mov     rbp, rsp
.cfi_def_cfa_register rbp
    push    rbx
.cfi_rel_offset rbx, -8
    push    r14
.cfi_rel_offset r14, -16
    push    r15
.cfi_rel_offset r15, -24
    and     rsp, -64
    sub     rsp, FRAME_SIZE

    mov     r15, rdi                  # r15 = r (result)
    mov     r14, rsi                  # r14 = a (source base, read-only)

    mov     rax, rdx
    shl     rax, 3
    mov     [rsp + SLOT_NBYTES_A], rax   # nbytes_a = n*8
    mov     rbx, rdx
    shl     rbx, 4                       # rbx = nbytes_out = 2*n*8

    # na = (64*n+51)/52 ; na_pad = round_up8(na)
    mov     rax, rdx
    shl     rax, 6
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    lea     rcx, [rax + 7]
    and     rcx, -8
    mov     [rsp + SLOT_NA_PAD], rcx

    # nblocks = (nbytes_out+51)/52
    mov     rax, rbx
    add     rax, 51
    xor     edx, edx
    mov     rcx, 52
    div     rcx
    mov     [rsp + SLOT_NBLOCKS], rax

    vmovdqa64    zmm28, [rip + LSidx_unpack]
    vmovdqa64    zmm29, [rip + LSshift_common]
    vpbroadcastq zmm26, [rip + LSm52_bcst]
    vpbroadcastq zmm27, [rip + LSone_bcst]

    # zero head pad [0, PAD_HEAD) and tail pad [PAD_HEAD+na_pad, +32) --
    # convert_operand-equivalent below fully overwrites [PAD_HEAD,
    # PAD_HEAD+na_pad).
    vpxorq  zmm0, zmm0, zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF], zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF + 64], zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF + 128], zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF + 192], zmm0
    mov     r8, [rsp + SLOT_NA_PAD]
    shl     r8, 3                        # na_pad is a DIGIT count; need bytes
    add     r8, PAD_HEAD * 8
    vmovdqu64 [rsp + FRAME_AD_OFF + r8], zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF + r8 + 64], zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF + r8 + 128], zmm0
    vmovdqu64 [rsp + FRAME_AD_OFF + r8 + 192], zmm0

    # convert operand: ngroups = na_pad/8 groups of 8 digits (52 bytes) each
    mov     rax, 0                       # byte_off (group 0)
    mov     rdx, [rsp + SLOT_NBYTES_A]
    lea     r8, [rsp + FRAME_AD_OFF + PAD_HEAD*8]
    mov     r11, [rsp + SLOT_NA_PAD]
    shr     r11, 3                       # ngroups
LSconv_loop:
    MASK_CALC_64
    lea     r10, [r14 + rax]
    vmovdqu8   zmm0{k1}{z}, [r10]
    vpermb     zmm0, zmm28, zmm0
    vpsrlvq    zmm0, zmm0, zmm29
    vmovdqu64  [r8], zmm0
    add     rax, 52
    add     r8, 64
    dec     r11
    jnz     LSconv_loop

    vpxorq  zmm25, zmm25, zmm25            # h_prev = 0
    mov     dword ptr [rsp + SLOT_CIN], 0
    mov     qword ptr [rsp + SLOT_C], 0

# ----------------------------------- block loop ----------------------------
LSblock_loop:
    mov     rax, [rsp + SLOT_C]
    cmp     rax, [rsp + SLOT_NBLOCKS]
    jae     LSblock_done

    lea     rdi, [rsp + FRAME_AD_OFF + PAD_HEAD*8]   # rdi = ad (base of digit 0)
    mov     r10, rax
    shl     r10, 3                          # r10 = col0 = 8c
    mov     r11, rax
    shl     r11, 2                          # r11 = fourc = 4c

    vpxorq  zmm16, zmm16, zmm16
    vpxorq  zmm17, zmm17, zmm17
    vpxorq  zmm18, zmm18, zmm18
    vpxorq  zmm19, zmm19, zmm19
    vpxorq  zmm20, zmm20, zmm20
    vpxorq  zmm21, zmm21, zmm21
    vpxorq  zmm22, zmm22, zmm22
    vpxorq  zmm23, zmm23, zmm23

    # unrestricted region: j in [lo, fourc-1]
    mov     r8, r10
    sub     r8, [rsp + SLOT_NA_PAD]
    inc     r8
    test    r8, r8
    jns     83f
    xor     r8, r8
83:
    lea     r9, [r11 - 1]                    # r9 = end = fourc-1
    cmp     r9, r8
    jl      LSno_unrestricted
    mov     rax, r9
    sub     rax, r8
    inc     rax
    and     rax, 3
    jz      84f
    mov     rcx, 4
    sub     rcx, rax
    sub     r8, rcx                          # extend lo backward (safe: head zero-pad)
84:
    mov     rcx, r8                          # rcx = j
LSj_loop:
    vpbroadcastq zmm24, qword ptr [rdi + rcx*8 - 8]
    vpbroadcastq zmm0,  qword ptr [rdi + rcx*8]
    vpbroadcastq zmm1,  qword ptr [rdi + rcx*8 + 8]
    vpbroadcastq zmm2,  qword ptr [rdi + rcx*8 + 16]
    vpbroadcastq zmm3,  qword ptr [rdi + rcx*8 + 24]

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
    jle     LSj_loop
LSno_unrestricted:

    # boundary region: j = fourc, fourc+1, fourc+2, fourc+3
    mov     rcx, r11                          # j = fourc
    mov     eax, 0xFE
    kmovb   k1, eax
    mov     eax, 0xFF
    kmovb   k2, eax
    vpbroadcastq zmm0, qword ptr [rdi + rcx*8]
    vpbroadcastq zmm1, qword ptr [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, [rdi + rax*8]
    vpmadd52luq zmm16{k1}, zmm4, zmm0
    vpmadd52huq zmm20{k2}, zmm4, zmm1

    inc     rcx
    mov     eax, 0xF8
    kmovb   k1, eax
    mov     eax, 0xFC
    kmovb   k2, eax
    vpbroadcastq zmm0, qword ptr [rdi + rcx*8]
    vpbroadcastq zmm1, qword ptr [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, [rdi + rax*8]
    vpmadd52luq zmm17{k1}, zmm4, zmm0
    vpmadd52huq zmm21{k2}, zmm4, zmm1

    inc     rcx
    mov     eax, 0xE0
    kmovb   k1, eax
    mov     eax, 0xF0
    kmovb   k2, eax
    vpbroadcastq zmm0, qword ptr [rdi + rcx*8]
    vpbroadcastq zmm1, qword ptr [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, [rdi + rax*8]
    vpmadd52luq zmm18{k1}, zmm4, zmm0
    vpmadd52huq zmm22{k2}, zmm4, zmm1

    inc     rcx
    mov     eax, 0x80
    kmovb   k1, eax
    mov     eax, 0xC0
    kmovb   k2, eax
    vpbroadcastq zmm0, qword ptr [rdi + rcx*8]
    vpbroadcastq zmm1, qword ptr [rdi + rcx*8 - 8]
    mov     rax, r10
    sub     rax, rcx
    vmovdqu64 zmm4, [rdi + rax*8]
    vpmadd52luq zmm19{k1}, zmm4, zmm0
    vpmadd52huq zmm23{k2}, zmm4, zmm1

    vpaddq  zmm0, zmm16, zmm17
    vpaddq  zmm1, zmm18, zmm19
    vpaddq  zmm2, zmm20, zmm21
    vpaddq  zmm3, zmm22, zmm23
    vpaddq  zmm0, zmm0, zmm1
    vpaddq  zmm2, zmm2, zmm3
    vpaddq  zmm0, zmm0, zmm2               # zmm0 = S_od
    vpaddq  zmm0, zmm0, zmm0               # zmm0 = 2*S_od

    # diagonal: a[fourc..fourc+3], duplicated into lane pairs
    vmovdqu64 zmm4, [rdi + r11*8]
    vmovdqa64 zmm5, [rip + LSdup_idx]
    vpermq    zmm4, zmm5, zmm4             # zmm4 = Ddup
    vpxorq    zmm3, zmm3, zmm3
    mov       eax, 0b01010101
    kmovb     k1, eax
    mov       eax, 0b10101010
    kmovb     k2, eax
    vpmadd52luq zmm3{k1}, zmm4, zmm4
    vpmadd52huq zmm3{k2}, zmm4, zmm4
    vpaddq    zmm0, zmm0, zmm3             # zmm0 = S (final raw column sum)

# ------------------------------- recompose ---------------------------------
    vpsrlq   zmm1, zmm0, 52
    vpandq   zmm2, zmm0, zmm26
    valignq  zmm3, zmm1, zmm25, 7
    vpaddq   zmm2, zmm2, zmm3

    vpcmpuq   k3, zmm2, zmm26, 6            # G: n>M (predicate 6 = unsigned NLE/greater)
    vpcmpeqq  k4, zmm2, zmm26               # P: n==M
    kmovb   r8d, k3
    kmovb   r9d, k4
    mov     eax, [rsp + SLOT_CIN]
    mov     ecx, r8d
    shl     ecx, 1
    or      ecx, eax
    add     ecx, r9d
    mov     eax, ecx
    xor     eax, r9d
    and     eax, 0xFF
    kmovb   k5, eax
    shr     ecx, 8
    and     ecx, 1
    mov     [rsp + SLOT_CIN], ecx

    vpaddq  zmm2{k5}, zmm2, zmm27
    vpandq  zmm2, zmm2, zmm26

    vpsllvq   zmm4, zmm2, zmm29
    vmovdqa64 zmm30, [rip + LSidx_pack_a]
    vmovdqa64 zmm31, [rip + LSidx_pack_b]
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
    jle     85f
    mov     rcx, 52
85:
    mov     r8, -1
    bzhi    r8, r8, rcx
    kmovq   k1, r8
    vmovdqu8 [r15 + rdx]{k1}, zmm5
LSskip_store:
    vpbroadcastq zmm4, qword ptr [rip + LSseven_bcst]
    vpermq  zmm25, zmm4, zmm1

    mov     rax, [rsp + SLOT_C]
    inc     rax
    mov     [rsp + SLOT_C], rax
    jmp     LSblock_loop

LSblock_done:
    vzeroupper
    lea     rsp, [rbp - 24]
    pop     r15
.cfi_restore r15
    pop     r14
.cfi_restore r14
    pop     rbx
.cfi_restore rbx
    pop     rbp
.cfi_def_cfa rsp, 8
.cfi_restore rbp
LSret_noop:
    ret
LStail_jump:
    jmp     beman_big_int_square_long_runtime_bmi2_adx@PLT
.cfi_endproc
.size beman_big_int_square_long_runtime_avx512_ifma, .-beman_big_int_square_long_runtime_avx512_ifma
