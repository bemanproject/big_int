#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# classify.py <profiles_dir> <gap_csv>: bucket .folded stacks by the frame nearest the leaf, scale by median_ns.
NAMES = {'wide': ['borrowing_sub', 'carrying_add', 'divide_wide_by_wide', 'divide_wide_by_wide_portable', 'from_int', 'funnel_shr', 'high_mul', 'high_mul_portable', 'narrowing_div', 'narrowing_div_portable', 'overflowing_add', 'overflowing_mul', 'overflowing_sub', 'reciprocal_word', 'reciprocal_word_3by2', 'select', 'to_int', 'volatile', 'widening_mul'], 'div': ['barrett_blocks', 'barrett_preinv_storage_size', 'barrett_storage_size', 'burnikel_ziegler_storage_size', 'divappr_quotient_slack', 'divide_barrett', 'divide_barrett_preinv', 'divide_burnikel_ziegler', 'divide_dc_2n1n', 'divide_dc_3n2n', 'divide_dc_basecase', 'divide_dc_divappr', 'divide_dispatch', 'divide_dispatch_q', 'divide_quotient', 'divide_quotient_appr', 'divide_quotient_storage_size', 'divide_unsigned', 'divide_unsigned_approx', 'divide_unsigned_storage_size', 'reciprocal_span', 'reciprocal_span_storage_size'], 'conv': ['append_squared_power', 'approximate_ceil_div_log2', 'approximate_ceil_mul_log2', 'base_conversion_chunk_count', 'base_conversion_digit_bound', 'base_conversion_limb_bound', 'basecase_digits_to_limbs', 'basecase_limbs_to_digits', 'build_power_table_for_chunks', 'build_power_table_for_value', 'combine_level', 'digit_value_buffer', 'digits_to_limbs', 'digits_to_limbs_storage_size', 'divide_short_preinv_in_place', 'emit_digits', 'fast_digits_to_limbs_profitable', 'fast_input_group_size', 'fast_limbs_to_digits_profitable', 'fast_output_group_size', 'is_fast_conversion_base', 'limb_max_input_digits', 'limb_max_input_digits_naive', 'limb_max_power', 'limb_pow_naive', 'limbs_to_digits', 'limbs_to_digits_storage_size', 'make_short_divisor', 'power_table_storage_size', 'span', 'square_into', 'value_bit_width', 'value_reaches_power', 'write_chunk_digits'], 'gcd': ['first', 'gcd_lehmer_step', 'gcd_limbs', 'gcd_short', 'gcd_unsigned_spans', 'lehmer_simulate', 'value'], 'toom': ['add_mod_bnm1', 'fft_choose_coeff_bits', 'fft_coeff_count', 'fft_cyclic_fp_storage_size', 'fft_cyclic_int_storage_size', 'fft_cyclic_next_size_properties', 'fft_cyclic_storage_size', 'fft_model_worthwhile', 'fft_mul_fp_storage_size', 'fft_mul_int_storage_size', 'fft_mul_storage_size', 'fft_mul_worthwhile', 'fft_transform_length', 'fold_mod_bnm1', 'fold_mod_bnp1', 'hooks', 'icbrt_floor', 'isqrt_floor', 'karatsuba_storage_size', 'mul_should_slice', 'mul_slice_zone_ratio', 'mul_slice_zones_valid', 'multiply_dispatch', 'multiply_fft', 'multiply_fft_cyclic', 'multiply_fft_cyclic_next_size', 'multiply_karatsuba', 'multiply_long', 'multiply_mod_bnm1', 'multiply_mod_bnm1_next_size', 'multiply_mod_bnm1_storage_size', 'multiply_power_of_two', 'multiply_runtime', 'multiply_runtime_any', 'multiply_runtime_sliced', 'multiply_runtime_unsliced', 'multiply_toom_cook_3', 'multiply_toom_cook_4', 'multiply_toom_cook_6_5', 'multiply_toom_cook_8_5', 'signed_axpy', 'solve_subsystem', 'solve_subsystem_85', 'square_dispatch', 'square_fft', 'square_fft_fp_storage_size', 'square_fft_int_storage_size', 'square_fft_storage_size', 'square_fft_worthwhile', 'square_karatsuba', 'square_long', 'square_runtime', 'square_toom_cook_3', 'square_toom_cook_4', 'square_toom_cook_6_5', 'square_toom_cook_8_5', 'stage_pow2_scaled', 'toom_cook_3_refuses_shape', 'toom_cook_3_storage_size', 'toom_cook_4_storage_size', 'toom_cook_6_5_storage_size', 'toom_cook_8_5_storage_size', 'toom_ladder_storage_size'], 'fft': ['add', 'cpu_has_avx2_fma', 'crt3_make_constants', 'crt_inverse', 'fft_cyclic_fp_storage_size', 'fft_cyclic_int_storage_size', 'fft_cyclic_storage_size', 'fft_min_adicity', 'fft_pack', 'fft_pack_fp', 'fft_recompose', 'fft_recompose_cyclic', 'fp_center', 'fp_mulmod', 'fp_reduce_to_0n', 'fp_reduce_to_pm1n', 'fp_ws', 'from_mont', 'heap', 'int_ws', 'inv', 'make_n_prime', 'make_r_squared', 'mont_mul', 'mul', 'multiply_fft', 'multiply_fft_cyclic', 'multiply_mod_bnm1', 'multiply_mod_bnp1', 'ntt_build_twiddles', 'ntt_forward', 'ntt_fp_build_twiddles', 'ntt_fp_dispatch', 'ntt_fp_forward_avx2', 'ntt_fp_forward_impl', 'ntt_fp_forward_neon', 'ntt_fp_forward_scalar', 'ntt_fp_inverse_avx2', 'ntt_fp_inverse_impl', 'ntt_fp_inverse_neon', 'ntt_fp_inverse_scalar', 'ntt_fp_pointwise_avx2', 'ntt_fp_pointwise_impl', 'ntt_fp_pointwise_neon', 'ntt_fp_pointwise_scalar', 'ntt_inverse', 'ntt_pointwise', 'pow', 'reduce', 'root', 'select_kernels', 'square_fft', 'sub', 'to_mont', 'vmulmod', 'vreduce_to_pm1n', 'ws'], 'span': ['add_into_tmp', 'add_shifted', 'add_unsigned_spans', 'add_unsigned_spans_and_shift_right_n', 'add_unsigned_spans_no_carry', 'decrement_span', 'div_2by1_preinv', 'funnel_shl', 'increment_span', 'is_power_of_two_span', 'is_span_zero', 'limb_or_zero', 'mod_unsigned_short', 'mul_add_single_limb_in_place', 'multiply_single_limb', 'recompose', 'recover_pair', 'shift_left_n', 'shift_left_one', 'shift_right_n', 'shift_right_one', 'submul_single_limb', 'submul_single_limb_wide', 'subtract_shifted_unsigned', 'subtract_unsigned_spans', 'subtract_unsigned_spans_and_shift_right_n', 'subtract_unsigned_spans_no_borrow', 'subtract_unsigned_spans_signed', 'trailing_zero_bits_span', 'trimmed_size_span']}

import os
import re
import sys

STRIP_TARGS = re.compile(r"<[^<>]*>")


def short(frame):
    f = frame
    for _ in range(8):
        g = STRIP_TARGS.sub("", f)
        if g == f:
            break
        f = g
    f = re.sub(r"\(.*$", "", f)
    f = re.sub(r"\s*\[.*\]$", "", f)
    f = f.strip()
    return f.split("::")[-1] if "::" in f else f


ALLOC = re.compile(
    r"^(malloc|free|cfree|calloc|realloc|_?_?libc_(malloc|free|calloc)|_int_malloc|_int_free|malloc_consolidate|"
    r"unlink_chunk|operator new|operator delete|new|delete|_Znwm|_ZdlPv.*|_ZnwmSt.*|aligned_alloc|posix_memalign|"
    r"__memset.*|__memcpy.*|__memmove.*|memset|memcpy|memmove|__libc_.*|_dl_.*|__gmp_default_(allocate|free|reallocate)|"
    r"__gmp_tmp_.*|.*tmp_alloc.*|.*scratch.*heap.*|_?_?alloc.*|.*_free|.*allocate.*|.*deallocate.*|__clone.*|brk|sbrk|"
    r"page_fault|asm_exc_page_fault|_?_?mmap|munmap|madvise)$"
)
SPANRE = re.compile(r"(unsigned_spans|_shifted|into_tmp|shift_(left|right)_n|single_limb|recompose|div_[23]by[12]|"
                    r"mod_unsigned|_span$|_span_|divide_unsigned_short|borrow_out)")
MEMBER = {"construct_at", "add_into", "sub_into", "add_in_place", "sub_in_place", "limb_ptr", "grow", "basic_big_int",
          "operator+", "operator-", "representation", "compare_limb_magnitudes", "alloc_limbs", "alloc_limbs_from"}
SKIP = re.compile(r"^(bit_cast|_mm\d*_.*|__builtin.*|reduce|select|volatile|to_int|from_int)$")
ASM = re.compile(r".*(long_runtime|multiply_long|square_long|ifma|bmi2_adx|_mulx|adx_).*", re.I)
OURS = [
    ("alloc_mem", lambda n: bool(ALLOC.match(n))),
    ("asm_kernel", lambda n: bool(ASM.match(n))),
    ("span_ops", lambda n: n in NAMES["span"] or bool(SPANRE.search(n))),
    ("wide_ops", lambda n: n in NAMES["wide"]),
    ("fft_ntt", lambda n: n in NAMES["fft"] or bool(re.search(r"fft|ntt|bnm1|fp_", n))),
    ("toom_karatsuba", lambda n: n in NAMES["toom"] or bool(re.search(r"toom|karatsuba|basecase_mul|multiply", n))),
    ("division", lambda n: n in NAMES["div"] or bool(re.search(r"divid|barrett|reciprocal|burnikel", n))),
    ("base_conv", lambda n: n in NAMES["conv"] or bool(re.search(r"to_chars|from_chars|base_conv|radix|digit|chunk", n))),
    ("gcd", lambda n: n in NAMES["gcd"] or bool(re.search(r"gcd|lehmer", n))),
    ("member_inline", lambda n: n in MEMBER),
]
G = lambda p: re.compile(p)
GMP = [
    ("alloc_mem", G(r"^(malloc|free|cfree|calloc|realloc|_?_?libc_.*|_int_malloc|_int_free|malloc_consolidate|unlink_chunk|"
                    r"__memset.*|__memcpy.*|__memmove.*|memset|memcpy|memmove|__gmp_default_.*|__gmp_tmp_.*|"
                    r"__gmpz_(init|clear|realloc|realloc2|init2)|_?_?gmpz_realloc.*|_?_?gmp_.*alloc.*|brk|page_fault|"
                    r"asm_exc_page_fault)$")),
    ("linear", G(r"bdiv_q_1|divexact_by3c|pi1_bdiv_q_1")),
    ("fft", G(r"fft|^mpn_mul_fft|mulmod_bnm1|mullo|sqrmod")),
    ("toom", G(r"toom|interpolate|mpn_toom|mul_n$|sqr_n$|^_?_?gmpn_(mul|sqr)$|_?_?gmpn_nussbaumer|mul_basecase_n")),
    ("basecase", G(r"mul_basecase|sqr_basecase|addmul|submul|mul_1|mul_2|mul_3|mul_4|sqr_diag|mul_n_basecase|^_?_?gmpn_mul$")),
    ("gcd", G(r"hgcd|gcd|matrix22|gcdext|jacobi|binvert|mpn_sub_ndiv|^div2$")),
    ("division", G(r"sbpi1|dcpi1|mu_div|mu_bdiv|divrem|invert|tdiv|div_qr|bdiv|pi1|div_q|divexact|preinv|redc|^_?_?gmpn_.*_div")),
    ("conv", G(r"get_str|set_str|mpn_.*bc|mp_bases|get_digit|powm|dc_get|dc_set|mpz_.*str|mpn_compute_powtab|powtab")),
    ("linear", G(r"(^|_)(add_n|sub_n|add|sub|lshift|rshift|addlsh|sublsh|rsh1|lsh1|add_n_sub_n|copyi|copyd|com|zero|"
                 r"divexact_by3c|bdiv_q_1|cnd_|cmp|zero_p|neg|abs_sub|add_1|sub_1|incr_u|decr_u|rsblsh|rsh1add|rsh1sub|"
                 r"sumdiff|lsh2|rsh2|addcnd|divrem_1|mod_1|mod_34lsub1|divexact_1|norm|sec_)\b|gmpz_(add|sub|cmp|mul_2exp|tdiv_q_2exp)")),
]


def bucket_of(frames, kind):
    lst = OURS if kind == "ours" else GMP
    shorts = [short(f) for f in frames]
    for fr in reversed(frames):
        n = short(fr)
        if kind == "ours":
            if SKIP.match(n):
                continue
            if n == "operator" and any(x in ("add_into", "sub_into", "operator+", "operator-") for x in shorts):
                return "member_inline"
        if kind == "gmp":
            n = re.sub(r"^_?_?gmpn_|^_?_?gmpz_", lambda m: m.group(0), n)
            nn = re.sub(r"^__gmp[nz]_", "", n)
        else:
            nn = n
        for b, t in lst:
            if kind == "ours":
                if t(n):
                    return b
            elif t.search(n) or t.search(nn):
                return b
    return "other"


def fold_load(path):
    rows = []
    for line in open(path):
        line = line.rstrip("\n")
        if not line:
            continue
        stack, _, cnt = line.rpartition(" ")
        rows.append((stack.split(";"), int(cnt)))
    return rows


def hist(path, kind):
    h = {}
    tot = 0
    nomain = 0
    if kind == "ours":
        h["(span_ops inclusive)"] = 0
    for frames, c in fold_load(path):
        b = bucket_of(frames, kind)
        if kind == "ours" and any(short(f) in NAMES["span"] or SPANRE.search(short(f)) for f in frames):
            h["(span_ops inclusive)"] += c
        h[b] = h.get(b, 0) + c
        tot += c
        if not any(short(f) == "main" for f in frames):
            nomain += c
    return h, tot, nomain


def csv_median(path, op, row, la, lb):
    try:
        for l in open(path):
            f = l.strip().split(",")
            if len(f) >= 9 and f[0] == op and f[1] == row and f[2] == la and f[3] == lb:
                return float(f[6])
    except OSError:
        pass
    return None


def out_median(path):
    try:
        for l in open(path):
            f = l.strip().split(",")
            if len(f) >= 9 and f[0] != "op":
                return float(f[6])
    except OSError:
        pass
    return None


def main():
    pdir, csv = sys.argv[1], sys.argv[2]
    out = []
    for name in sorted(os.listdir(pdir)):
        if not name.endswith("_auto.folded"):
            continue
        base = name[: -len("_auto.folded")]
        op, shape = base.split("_", 1)
        la, _, lb = shape.partition("x")
        lb = lb or la
        o_med = csv_median(csv, op, "auto", la, lb)
        g_med = csv_median(csv, op, "gmpz", la, lb)
        o_src = g_src = "gap csv"
        if o_med is None:
            o_med, o_src = out_median(os.path.join(pdir, base + "_auto.out")), "profile run"
        if g_med is None:
            g_med, g_src = out_median(os.path.join(pdir, base + "_gmpz.out")), "profile run"
        gpath = os.path.join(pdir, base + "_gmpz.folded")
        oh, o_tot, onm = hist(os.path.join(pdir, name), "ours")
        gh, gt, gnm = hist(gpath, "gmp") if os.path.exists(gpath) else ({}, 0, 0)
        out.append("## %s %s  (auto median %s ns [%s], gmpz median %s ns [%s]; no-main stacks ours %.1f%%, GMP %.1f%%)\n" % (
            op, shape, o_med, o_src, g_med, g_src, 100.0 * onm / max(o_tot, 1), 100.0 * gnm / max(gt, 1)))
        out.append("| bucket | ours % | ours ns | GMP bucket | GMP % | GMP ns |\n|---|---|---|---|---|---|")
        incl = oh.pop("(span_ops inclusive)", 0)
        gh.pop("(span_ops inclusive)", None)
        ob = sorted(oh.items(), key=lambda kv: -kv[1])
        gb = sorted(gh.items(), key=lambda kv: -kv[1])
        for i in range(max(len(ob), len(gb))):
            o = ob[i] if i < len(ob) else None
            g = gb[i] if i < len(gb) else None
            os_ = ("%s | %.1f | %s" % (o[0], 100.0 * o[1] / o_tot, "%.0f" % (o_med * o[1] / o_tot) if o_med else "-")) if o else " | | "
            gs = ("%s | %.1f | %s" % (g[0], 100.0 * g[1] / gt, "%.0f" % (g_med * g[1] / gt) if g_med else "-")) if g else " | | "
            out.append("| %s | %s |" % (os_, gs))
        out.append("")
        out.append("span_ops inclusive (any span_ops frame on the stack, incl. wide_ops leaves beneath it): ours %.1f%%\n" % (
            100.0 * incl / max(o_tot, 1)))
    for name in sorted(os.listdir(pdir)):
        if not name.endswith("_inplace.folded"):
            continue
        base = name[: -len("_inplace.folded")]
        op, shape = base.split("_", 1)
        la, _, lb = shape.partition("x")
        med = csv_median(csv, op, "inplace", la, lb or la)
        if med is None:
            med = out_median(os.path.join(pdir, base + "_inplace.out"))
        h, t, nm = hist(os.path.join(pdir, name), "ours")
        h.pop("(span_ops inclusive)", None)
        out.append("## %s %s inplace (ours only, median %s ns; no-main %.1f%%)\n" % (op, shape, med, 100.0 * nm / max(t, 1)))
        out.append("| bucket | ours % | ours ns |\n|---|---|---|")
        for b, c in sorted(h.items(), key=lambda kv: -kv[1]):
            out.append("| %s | %.1f | %s |" % (b, 100.0 * c / t, "%.0f" % (med * c / t) if med else "-"))
        out.append("")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
