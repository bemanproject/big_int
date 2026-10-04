#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
#
# Generates test vectors for division and modulus of huge big_int values.
# Uses Python's arbitrary-precision integers as the reference implementation.
#
# Usage: python3 generate_division_test_data.py > division_huge.test.cpp
#        python3 generate_division_test_data.py dc > division_dc.test.cpp
#
# The "dc" suite targets the Burnikel-Ziegler divide-and-conquer path: sizes
# straddle burnikel_ziegler_cutoff/offset (40/10 in 64-bit limbs on AArch64 and
# portable builds, 160/64 on x86-64) and the block-decomposition shapes of the
# top-level driver. Under 32-bit limbs the
# same vectors remain valid, just at different boundary positions.

import random
import sys

# The dc suite emits four-digit-limb-count literals; lift CPython's default
# int-to-str conversion limit (4300 digits) where present.
if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

LIMB_BITS = 64
LIMB_MAX = (1 << LIMB_BITS) - 1


def random_n_limb(n: int, rng: random.Random) -> int:
    """Generate a random positive integer with exactly n limbs (top limb nonzero)."""
    value = 0
    for i in range(n - 1):
        value |= rng.getrandbits(LIMB_BITS) << (i * LIMB_BITS)
    top = rng.randint(1, LIMB_MAX)
    value |= top << ((n - 1) * LIMB_BITS)
    return value


def trunc_div(a: int, b: int) -> int:
    """C++ truncated division (rounds toward zero)."""
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def trunc_mod(a: int, b: int) -> int:
    """C++ truncated modulus (remainder sign matches dividend)."""
    return a - trunc_div(a, b) * b


# The _n literal parses at compile time; very long literals blow the
# compilers' constexpr step limits, so big values are parsed at runtime
# through from_chars instead (the parse() helper emitted into dc files).
RUNTIME_PARSE_DIGITS = 1200


def cpp_value(value: int) -> str:
    if len(str(abs(value))) <= RUNTIME_PARSE_DIGITS:
        return f"{value}_n"
    return f'parse("{value}")'


def emit_div_case(suite: str, name: str, a: int, b: int) -> str:
    q = trunc_div(a, b)
    r = trunc_mod(a, b)
    lines = []
    lines.append(f"TEST({suite}, {name}) {{")
    lines.append(f"    const big_int a = {cpp_value(a)};")
    lines.append(f"    const big_int b = {cpp_value(b)};")
    lines.append(f"    const big_int expected_q = {cpp_value(q)};")
    lines.append(f"    const big_int expected_r = {cpp_value(r)};")
    lines.append("")
    lines.append("    EXPECT_EQ(a / b, expected_q);")
    lines.append("    EXPECT_EQ(a % b, expected_r);")
    lines.append("    EXPECT_EQ((a / b) * b + (a % b), a);")
    lines.append("}")
    return "\n".join(lines)


def emit_invariant_case(suite: str, name: str, a: int, b: int) -> str:
    """Emit a test that only checks the division invariant, not exact values."""
    lines = []
    lines.append(f"TEST({suite}, {name}) {{")
    lines.append(f"    const big_int a = {cpp_value(a)};")
    lines.append(f"    const big_int b = {cpp_value(b)};")
    lines.append("")
    lines.append("    const big_int q = a / b;")
    lines.append("    const big_int r = a % b;")
    lines.append("    EXPECT_EQ(q * b + r, a);")
    if b > 0:
        lines.append("    EXPECT_TRUE(r >= 0_n);")
        lines.append(f"    EXPECT_TRUE(r < {cpp_value(b)});")
    lines.append("}")
    return "\n".join(lines)


def normalized_n_limb(n: int, rng: random.Random) -> int:
    """Random n-limb value with the top bit of the top limb set."""
    return random_n_limb(n, rng) | (1 << (n * LIMB_BITS - 1))


def emit_consteval_dc_case() -> str:
    """Sizes above the dc gates must still constant-evaluate: divide_dispatch
    routes constant evaluation to the schoolbook kernel. The operands are
    built from shifts (cheap at consteval; long decimal literals would blow
    the compilers' constexpr step limits) with an exactly-known q and r."""
    lines = []
    lines.append("// A 42-limb divisor with a 63-limb dividend sits above both")
    lines.append("// divide-and-conquer gates, yet must still constant-evaluate:")
    lines.append("// divide_dispatch routes constant evaluation to the schoolbook kernel.")
    lines.append("// a = b * 2^1400 + r with r < b, so q and r are known exactly.")
    lines.append("consteval bool ce_dc_sized_division() {")
    lines.append("    const big_int b = (big_int{1} << 2625) + big_int{987654321};")
    lines.append("    const big_int r = (big_int{1} << 1399) + big_int{12345};")
    lines.append("    const big_int a = (b << 1400) + r;")
    lines.append("    return (a / b) == (big_int{1} << 1400) && (a % b) == r;")
    lines.append("}")
    lines.append("static_assert(ce_dc_sized_division());")
    return "\n".join(lines)


def print_file(cases, with_parse_helper: bool = False) -> None:
    print("// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception")
    print("// SPDX-License-Identifier: BSL-1.0")
    print("//")
    print("// Auto-generated by generate_division_test_data.py - do not edit manually.")
    print()
    print("#include <gtest/gtest.h>")
    print()
    print("#include <beman/big_int/big_int.hpp>")
    print()
    print('#include "testing.hpp"')
    print()
    if with_parse_helper:
        print("#include <string_view>")
        print("#include <system_error>")
        print()
    print("using BEMAN_BIG_INT_NAMESPACE::big_int;")
    print("using namespace BEMAN_BIG_INT_NAMESPACE::literals;")
    print()
    if with_parse_helper:
        print("namespace {")
        print()
        print("// Runtime decimal parse for values too long to go through the consteval")
        print("// _n literal (compiler constexpr step limits).")
        print("[[nodiscard]] big_int parse(const std::string_view s) {")
        print("    big_int v;")
        print("    const auto [p, ec] = BEMAN_BIG_INT_NAMESPACE::from_chars(s.data(), s.data() + s.size(), v, 10);")
        print("    if (ec != std::errc{} || p != s.data() + s.size()) {")
        print('        ADD_FAILURE() << "failed to parse test literal";')
        print("    }")
        print("    return v;")
        print("}")
        print()
        print("} // namespace")
        print()
    for case in cases:
        print(case)
        print()


def main_dc():
    rng = random.Random(229)
    cases = []

    cutoff = 40  # burnikel_ziegler_cutoff in 64-bit limbs
    offset = 10  # burnikel_ziegler_offset in 64-bit limbs

    # ---- 1. Dispatch gate boundaries ----
    for s, extra, name in [
        (cutoff - 1, offset + 5, "DivisorJustBelowCutoff"),
        (cutoff, offset - 1, "QuotientJustBelowOffset"),
        (cutoff, offset, "ExactlyAtBothGates"),
        (cutoff, offset + 1, "JustAboveOffset"),
        (cutoff + 1, offset, "JustAboveCutoff"),
        (cutoff + 8, 72, "WellAboveCutoff"),
    ]:
        a = random_n_limb(s + extra, rng)
        b = random_n_limb(s, rng)
        cases.append(emit_div_case("DivisionDc", name, a, b))

    # ---- 2. Block-decomposition shapes of the top-level driver ----
    # Minimal two-block march (t == 2).
    cases.append(emit_div_case("DivisionDc", "MinimalTwoBlocks", random_n_limb(60, rng), random_n_limb(40, rng)))

    # Already-normalized divisor and a dividend of exactly 2n full blocks:
    # the defensive +1 in the block count produces a zero top block (t == 3).
    cases.append(
        emit_div_case("DivisionDc", "ZeroTopBlock", normalized_n_limb(80, rng), normalized_n_limb(40, rng))
    )

    # s = 41 pads to n = 42 with odd leaf size j = 21.
    cases.append(emit_div_case("DivisionDc", "OddLeafSize", random_n_limb(140, rng), random_n_limb(41, rng)))

    # Power-of-two multiples of the threshold: s = 64 -> j = 32, s = 80 -> j = 20.
    cases.append(emit_div_case("DivisionDc", "SixtyFourLimbDivisor", random_n_limb(200, rng), random_n_limb(64, rng)))
    cases.append(emit_div_case("DivisionDc", "EightyLimbDivisor", random_n_limb(300, rng), random_n_limb(80, rng)))

    # Top divisor limb == 1: maximal in-limb normalization shift.
    b = (1 << (43 * LIMB_BITS)) + random_n_limb(40, rng)
    cases.append(emit_div_case("DivisionDc", "MaximalBitShift", random_n_limb(100, rng), b))

    # ---- 3. Value patterns ----
    cases.append(emit_div_case("DivisionDc", "AllOnes96By44", (1 << (96 * LIMB_BITS)) - 1, (1 << (44 * LIMB_BITS)) - 1))

    b = 1 << (42 * LIMB_BITS - 1)  # beta^42 / 2: smallest normalized 42-limb value
    cases.append(emit_div_case("DivisionDc", "MinimalNormalizedDivisor", random_n_limb(90, rng), b))
    cases.append(emit_div_case("DivisionDc", "MinimalNormalizedPlusOne", random_n_limb(90, rng), b + 1))

    b = random_n_limb(45, rng)
    q = random_n_limb(30, rng)
    cases.append(emit_div_case("DivisionDc", "MaximalRemainder", q * b + (b - 1), b))

    b = random_n_limb(48, rng)
    q = random_n_limb(25, rng)
    cases.append(emit_div_case("DivisionDc", "ExactProduct", q * b, b))

    b = random_n_limb(42, rng)
    a = (b << (30 * LIMB_BITS)) + random_n_limb(20, rng)
    cases.append(emit_div_case("DivisionDc", "TopWindowEqualsDivisorPrefix", a, b))

    cases.append(emit_div_case("DivisionDc", "PowerOfTwoDivisor", random_n_limb(100, rng), 1 << (40 * LIMB_BITS - 17)))

    # ---- 4. Unbalanced shapes (long block marches) ----
    cases.append(emit_div_case("DivisionDc", "LongBlockMarch1200By50", random_n_limb(1200, rng), random_n_limb(50, rng)))
    cases.append(emit_div_case("DivisionDc", "LongBlockMarch600By48", random_n_limb(600, rng), random_n_limb(48, rng)))
    cases.append(emit_div_case("DivisionDc", "NearlyBalanced500By479", random_n_limb(500, rng), random_n_limb(479, rng)))

    # ---- 5. Signs resolve above the dc layer; one quadrant sweep at dc size ----
    a = random_n_limb(84, rng)
    b = random_n_limb(42, rng)
    cases.append(emit_div_case("DivisionDcSigned", "NegDividend", -a, b))
    cases.append(emit_div_case("DivisionDcSigned", "NegDivisor", a, -b))
    cases.append(emit_div_case("DivisionDcSigned", "BothNeg", -a, -b))

    # ---- 6. Invariant-only stress at randomized dc sizes ----
    for i in range(8):
        s = rng.randint(38, 100)
        extra = rng.randint(18, 120)
        a = random_n_limb(s + extra, rng)
        b = random_n_limb(s, rng)
        cases.append(emit_invariant_case("DivisionDcInvariant", f"Random{i}_{s + extra}by{s}", a, b))

    # ---- 7. Constant evaluation above the gates ----
    cases.append(emit_consteval_dc_case())

    print_file(cases, with_parse_helper=True)


def main():
    rng = random.Random(73)
    cases = []

    # ---- 1. Multi-limb / multi-limb with exact quotient and remainder ----
    # 10-limb / 5-limb
    a = random_n_limb(10, rng)
    b = random_n_limb(5, rng)
    cases.append(emit_div_case("DivisionHuge", "TenByFiveLimbs", a, b))

    # 20-limb / 10-limb
    a = random_n_limb(20, rng)
    b = random_n_limb(10, rng)
    cases.append(emit_div_case("DivisionHuge", "TwentyByTenLimbs", a, b))

    # 8-limb / 3-limb
    a = random_n_limb(8, rng)
    b = random_n_limb(3, rng)
    cases.append(emit_div_case("DivisionHuge", "EightByThreeLimbs", a, b))

    # ---- 2. Exact division (product / factor == factor, remainder 0) ----
    f1 = random_n_limb(5, rng)
    f2 = random_n_limb(5, rng)
    product = f1 * f2
    cases.append(emit_div_case("DivisionHuge", "ExactTenLimbProduct", product, f1))

    f1 = random_n_limb(8, rng)
    f2 = random_n_limb(4, rng)
    product = f1 * f2
    cases.append(emit_div_case("DivisionHuge", "ExactTwelveLimbProduct", product, f1))

    # ---- 3. Large dividend / single limb (short division path) ----
    a = random_n_limb(15, rng)
    b = rng.randint(2, LIMB_MAX)
    cases.append(emit_div_case("DivisionHuge", "FifteenLimbByOneLimb", a, b))

    a = random_n_limb(30, rng)
    b = rng.randint(2, LIMB_MAX)
    cases.append(emit_div_case("DivisionHuge", "ThirtyLimbByOneLimb", a, b))

    # ---- 4. Nearly equal huge numbers ----
    base = random_n_limb(10, rng)
    a = base + rng.randint(1, 1000)
    b = base
    cases.append(emit_div_case("DivisionHuge", "NearlyEqualTenLimb", a, b))

    base = random_n_limb(6, rng)
    a = base * 3 + rng.randint(1, base - 1)
    cases.append(emit_div_case("DivisionHuge", "SmallQuotientSixLimb", a, base))

    # ---- 5. Power-of-two edge cases ----
    # (2^512) / (2^256) == 2^256
    a = 1 << 512
    b = 1 << 256
    cases.append(emit_div_case("DivisionHuge", "PowerOfTwo512By256", a, b))

    # (2^1024 - 1) / (2^512 - 1)
    a = (1 << 1024) - 1
    b = (1 << 512) - 1
    cases.append(emit_div_case("DivisionHuge", "AllOnes1024By512", a, b))

    # (2^1024 - 1) / (2^512 + 1) - the +1 makes remainder interesting
    a = (1 << 1024) - 1
    b = (1 << 512) + 1
    cases.append(emit_div_case("DivisionHuge", "AllOnes1024ByPow512Plus1", a, b))

    # ---- 6. Signed huge values ----
    a = random_n_limb(10, rng)
    b = random_n_limb(4, rng)
    cases.append(emit_div_case("DivisionHugeSigned", "NegDividend", -a, b))
    cases.append(emit_div_case("DivisionHugeSigned", "NegDivisor", a, -b))
    cases.append(emit_div_case("DivisionHugeSigned", "BothNeg", -a, -b))

    # ---- 7. Divisor just barely smaller than dividend ----
    # Quotient = 2 exactly
    half = random_n_limb(8, rng)
    a = half * 2
    cases.append(emit_div_case("DivisionHuge", "QuotientExactlyTwo", a, half))

    # Quotient = max_limb (all bits set in one limb)
    b = random_n_limb(4, rng)
    a = b * LIMB_MAX + rng.randint(0, b - 1)
    cases.append(emit_div_case("DivisionHuge", "QuotientMaxLimb", a, b))

    # ---- 8. Very large: 50-limb / 25-limb ----
    a = random_n_limb(50, rng)
    b = random_n_limb(25, rng)
    cases.append(emit_div_case("DivisionHuge", "FiftyByTwentyFiveLimbs", a, b))

    # ---- 9. Division invariant stress tests with random sizes ----
    for i in range(5):
        na = rng.randint(10, 40)
        nb = rng.randint(2, na)
        a = random_n_limb(na, rng)
        b = random_n_limb(nb, rng)
        cases.append(emit_invariant_case("DivisionHugeInvariant", f"Random{i}_{na}by{nb}", a, b))

    # ---- 10. Modulus-specific: remainder should be small ----
    # Large number mod small prime
    a = random_n_limb(20, rng)
    p = 1000000007
    r = a % p
    lines = []
    lines.append("TEST(ModulusHuge, LargeModSmallPrime) {")
    lines.append(f"    const big_int a = {a}_n;")
    lines.append(f"    EXPECT_EQ(a % big_int{{{p}}}, {r});")
    lines.append("}")
    cases.append("\n".join(lines))

    # Large number mod (2^64 - 1) -- Mersenne-like
    a = random_n_limb(20, rng)
    mersenne = (1 << 64) - 1
    r = a % mersenne
    lines = []
    lines.append("TEST(ModulusHuge, LargeModMersenne64) {")
    lines.append(f"    const big_int a = {a}_n;")
    lines.append(f"    const big_int m = {mersenne}_n;")
    lines.append(f"    EXPECT_EQ(a % m, {r}_n);")
    lines.append("}")
    cases.append("\n".join(lines))

    print_file(cases)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "dc":
        main_dc()
    else:
        main()
