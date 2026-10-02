#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
#
# Generates test vectors for bitwise AND, OR, and XOR operations on big_int.
# Each test case uses Python's arbitrary-precision integers as the reference
# implementation, which already models two's complement bitwise semantics.
#
# Usage: python3 generate_bitwise_test_data.py

import random

LIMB_BITS = 64
LIMB_MAX = (1 << LIMB_BITS) - 1


def int_to_limbs(n: int) -> list[int]:
    """Convert a non-negative Python int to a list of 64-bit limbs (little-endian)."""
    if n == 0:
        return [0]
    limbs = []
    while n > 0:
        limbs.append(n & LIMB_MAX)
        n >>= LIMB_BITS
    return limbs


def format_limb_array(limbs: list[int], name: str) -> str:
    entries = ", ".join(f"0x{limb:016X}ULL" for limb in limbs)
    return f"    constexpr std::array<uint_multiprecision_t, {len(limbs)}> {name} = {{{{{entries}}}}};"


def make_big_int(varname: str, value: int) -> list[str]:
    """Emit C++ lines that construct a signed big_int from a Python int."""
    mag = abs(value)
    neg = value < 0
    limbs = int_to_limbs(mag)
    lines = [format_limb_array(limbs, f"{varname}_limbs")]
    lines.append(f"    const big_int {varname} = {'neg_from_limbs' if neg else 'pos_from_limbs'}({varname}_limbs);")
    return lines


def generate_case(suite: str, name: str, a: int, b: int) -> str:
    """Generate a single GTest TEST() covering AND, OR, and XOR for (a, b)."""
    and_result = a & b
    or_result  = a | b
    xor_result = a ^ b

    lines = [f"TEST({suite}, {name}) {{"]
    lines += make_big_int("a", a)
    lines += make_big_int("b", b)
    lines += make_big_int("expected_and", and_result)
    lines += make_big_int("expected_or",  or_result)
    lines += make_big_int("expected_xor", xor_result)
    lines += [
        "",
        "    EXPECT_EQ(a & b, expected_and);",
        "    EXPECT_EQ(b & a, expected_and);  // commutativity",
        "    EXPECT_EQ(a | b, expected_or);",
        "    EXPECT_EQ(b | a, expected_or);   // commutativity",
        "    EXPECT_EQ(a ^ b, expected_xor);",
        "    EXPECT_EQ(b ^ a, expected_xor);  // commutativity",
        "}",
    ]
    return "\n".join(lines)

def generate_compound_case(suite: str, name: str, a: int, b: int) -> str:
    """Generate a TEST() verifying that op= matches the free operator."""
    lines = [f"TEST({suite}, {name}) {{"]
    lines += make_big_int("a", a)
    lines += make_big_int("b", b)
    lines += [
        "",
        "    { big_int x = a; x &= b; EXPECT_EQ(x, a & b); }",
        "    { big_int x = b; x &= a; EXPECT_EQ(x, b & a);  // commutativity",
        "    }",
        "    { big_int x = a; x |= b; EXPECT_EQ(x, a | b); }",
        "    { big_int x = b; x |= a; EXPECT_EQ(x, b | a);  // commutativity",
        "    }",
        "    { big_int x = a; x ^= b; EXPECT_EQ(x, a ^ b); }",
        "    { big_int x = b; x ^= a; EXPECT_EQ(x, b ^ a);  // commutativity",
        "    }",
        "}",
    ]
    return "\n".join(lines)

def generate_self_assign_case(suite: str, name: str, a: int) -> str:
    """Generate a TEST() for self-compound-assignment (x op= x)."""
    lines = [f"TEST({suite}, {name}) {{"]
    lines += make_big_int("a", a)
    lines += [
        "",
        "    { big_int x = a; x &= x; EXPECT_EQ(x, a & a); }",
        "    { big_int x = a; x |= x; EXPECT_EQ(x, a | a); }",
        "    { big_int x = a; x ^= x; EXPECT_EQ(x, a ^ a); }  // must be zero",
        "}",
    ]
    return "\n".join(lines)

def random_n_limb(n: int, rng: random.Random) -> int:
    """Generate a random positive integer with exactly n limbs (top limb nonzero)."""
    value = 0
    for i in range(n - 1):
        value |= rng.getrandbits(LIMB_BITS) << (i * LIMB_BITS)
    top = rng.randint(1, LIMB_MAX)
    value |= top << ((n - 1) * LIMB_BITS)
    return value


def main():
    rng = random.Random(42)
    cases = []

    cases.append(generate_case("BitwiseSingleLimb", "PosPos",  5,  3))
    cases.append(generate_case("BitwiseSingleLimb", "PosNeg",  5, -3))
    cases.append(generate_case("BitwiseSingleLimb", "NegPos", -5,  3))
    cases.append(generate_case("BitwiseSingleLimb", "NegNeg", -5, -3))

    cases.append(generate_case("BitwiseSingleLimb", "PosAndMinusOne",   42, -1))
    cases.append(generate_case("BitwiseSingleLimb", "NegAndMinusOne",  -42, -1))
    cases.append(generate_case("BitwiseSingleLimb", "PosOrMinusOne",    42, -1))
    cases.append(generate_case("BitwiseSingleLimb", "PosXorMinusOne",   42, -1))

    cases.append(generate_case("BitwiseSingleLimb", "PosAndZero",  99,  0))
    cases.append(generate_case("BitwiseSingleLimb", "NegAndZero", -99,  0))
    cases.append(generate_case("BitwiseSingleLimb", "PosOrZero",   99,  0))
    cases.append(generate_case("BitwiseSingleLimb", "PosXorZero",  99,  0))

    for x in [7, -7]:
        sign = "Pos" if x > 0 else "Neg"
        cases.append(generate_case("BitwiseSingleLimb", f"{sign}SelfAnd",  x,  x))
        cases.append(generate_case("BitwiseSingleLimb", f"{sign}SelfOr",   x,  x))
        cases.append(generate_case("BitwiseSingleLimb", f"{sign}SelfXor",  x,  x))
        cases.append(generate_case("BitwiseSingleLimb", f"{sign}ComplementAnd", x, ~x))
        cases.append(generate_case("BitwiseSingleLimb", f"{sign}ComplementOr",  x, ~x))
        cases.append(generate_case("BitwiseSingleLimb", f"{sign}ComplementXor", x, ~x))

    cases.append(generate_case("BitwiseSingleLimb", "MaxUint64PosPos", LIMB_MAX,  LIMB_MAX))
    cases.append(generate_case("BitwiseSingleLimb", "MaxUint64PosNeg", LIMB_MAX, -LIMB_MAX))
    cases.append(generate_case("BitwiseSingleLimb", "MaxUint64NegNeg", -LIMB_MAX, -LIMB_MAX))

    # Carry in two's complement conversion crosses the limb boundary when the
    # magnitude is a power of two times the limb size, e.g. 2^64.
    # For -2^64: magnitude = [0, 1], two's complement = ~[0,1] + 1 = [0,0xFFFF...FFF] + 1
    # which carries into the second limb.
    base = 1 << LIMB_BITS  # exactly 2 limbs: [0, 1]

    cases.append(generate_case("BitwiseTwoLimb", "PowerOfLimbNegNeg", -base,       -base))
    cases.append(generate_case("BitwiseTwoLimb", "PowerOfLimbNegPos", -base,        base))
    cases.append(generate_case("BitwiseTwoLimb", "PowerOfLimbPosNeg",  base,       -base))
    cases.append(generate_case("BitwiseTwoLimb", "PowerOfLimbMinus1Neg", -(base-1), -(base-1)))

    # Random two-limb values, all sign combos
    for i in range(3):
        a_mag = random_n_limb(2, rng)
        b_mag = random_n_limb(2, rng)
        for signs, label in [((1,1),"PP"), ((1,-1),"PN"), ((-1,1),"NP"), ((-1,-1),"NN")]:
            a = signs[0] * a_mag
            b = signs[1] * b_mag
            cases.append(generate_case("BitwiseTwoLimb", f"Random{i}_{label}", a, b))
    # One operand is longer than the other. Exercises the extension paths where
    # i >= lhs.size() or i >= rhs.size() in make_bitwise_of_limbs.
    a_long  = random_n_limb(4, rng)
    b_short = random_n_limb(2, rng)
    for signs, label in [((1,1),"PP"), ((1,-1),"PN"), ((-1,1),"NP"), ((-1,-1),"NN")]:
        a = signs[0] * a_long
        b = signs[1] * b_short
        cases.append(generate_case("BitwiseUnequalLength", f"4x2_{label}", a, b))

    a_long  = random_n_limb(5, rng)
    b_short = random_n_limb(1, rng)
    for signs, label in [((1,1),"PP"), ((1,-1),"PN"), ((-1,1),"NP"), ((-1,-1),"NN")]:
        a = signs[0] * a_long
        b = signs[1] * b_short
        cases.append(generate_case("BitwiseUnequalLength", f"5x1_{label}", a, b))

    # Multi-limb large random cases
    for i in range(3):
        n = rng.randint(3, 6)
        a_mag = random_n_limb(n, rng)
        b_mag = random_n_limb(n, rng)
        for signs, label in [((1,1),"PP"), ((1,-1),"PN"), ((-1,1),"NP"), ((-1,-1),"NN")]:
            a = signs[0] * a_mag
            b = signs[1] * b_mag
            cases.append(generate_case("BitwiseMultiLimb", f"Random{i}_{label}", a, b))

    def de_morgan_case(suite: str, name: str, a: int, b: int) -> str:
        nand = ~(a & b)
        nor  = ~(a | b)
        lines = [f"TEST({suite}, {name}) {{"]
        lines += make_big_int("a", a)
        lines += make_big_int("b", b)
        lines += make_big_int("expected_nand", nand)
        lines += make_big_int("expected_nor",  nor)
        lines += [
            "",
            "    // De Morgan: ~(a & b) == ~a | ~b",
            "    EXPECT_EQ(~(a & b), expected_nand);",
            "    EXPECT_EQ(~a | ~b,  expected_nand);",
            "    // De Morgan: ~(a | b) == ~a & ~b",
            "    EXPECT_EQ(~(a | b), expected_nor);",
            "    EXPECT_EQ(~a & ~b,  expected_nor);",
            "}",
        ]
        return "\n".join(lines)

    for i, (a, b) in enumerate([
        (42, 13),
        (-42, 13),
        (42, -13),
        (-42, -13),
        (random_n_limb(3, rng), random_n_limb(3, rng)),
        (-random_n_limb(3, rng), random_n_limb(2, rng)),
    ]):
        cases.append(de_morgan_case("BitwiseDeMorgan", f"Case{i}", a, b))

    compound_inputs = [
        (5, 3), (5, -3), (-5, 3), (-5, -3),
        (42, -1), (-42, -1), (0, -1),
        (LIMB_MAX, -LIMB_MAX), (-LIMB_MAX, -LIMB_MAX),
        (1 << LIMB_BITS, -(1 << LIMB_BITS)),
        (random_n_limb(2, rng), random_n_limb(2, rng)),
        (-random_n_limb(2, rng), random_n_limb(2, rng)),
        (random_n_limb(4, rng), random_n_limb(2, rng)),
        (-random_n_limb(4, rng), -random_n_limb(3, rng)),
    ]
    for i, (a, b) in enumerate(compound_inputs):
        cases.append(generate_compound_case("BitwiseCompoundAssign", f"Case{i}", a, b))

    self_assign_inputs = [
        0, 1, -1, 7, -7, 42, -42, LIMB_MAX, -LIMB_MAX,
        1 << LIMB_BITS, -(1 << LIMB_BITS),
        random_n_limb(2, rng),
        -random_n_limb(2, rng),
        random_n_limb(4, rng),
        -random_n_limb(4, rng),
    ]
    for i, a in enumerate(self_assign_inputs):
        cases.append(generate_self_assign_case("BitwiseSelfAssign", f"Case{i}", a))
    # Edge cases.
    cases.append(generate_case("BitwiseEdgeCases", "ZeroAndMinusOne",        0, -1))
    cases.append(generate_case("BitwiseEdgeCases", "MinusOneAndMinusOne",   -1, -1))
    cases.append(generate_case("BitwiseEdgeCases", "MaxUint64XorMinusOne",   LIMB_MAX, -1))
    cases.append(generate_case("BitwiseEdgeCases", "ZeroOrMinusOne",         0, -1))
    cases.append(generate_case("BitwiseEdgeCases", "ZeroXorMinusOne",        0, -1))
    cases.append(generate_case("BitwiseEdgeCases", "MinusOneXorMinusOne",   -1, -1))
    cases.append(generate_case("BitwiseEdgeCases", "MinusOneOrMinusOne",    -1, -1))

    print("// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception")
    print("// SPDX-License-Identifier: BSL-1.0")
    print("//")
    print("// Auto-generated by generate_bitwise_test_data.py - do not edit manually.")
    print()
    print("#include <array>")
    print("#include <cstddef>")
    print("#include <cstdint>")
    print()
    print("#include <gtest/gtest.h>")
    print()
    print("#include <beman/big_int.hpp>")
    print()
    print("using BEMAN_BIG_INT_NAMESPACE::big_int;")
    print("using BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;")
    print()
    print("namespace {")
    print()
    print("template <std::size_t N>")
    print("big_int pos_from_limbs(const std::array<uint_multiprecision_t, N>& limbs) {")
    print("    big_int result{0};")
    print("    for (std::size_t i = N; i > 0; --i) {")
    print("        result <<= 64;")
    print("        result = result + big_int{limbs[i - 1]};")
    print("    }")
    print("    return result;")
    print("}")
    print()
    print("template <std::size_t N>")
    print("big_int neg_from_limbs(const std::array<uint_multiprecision_t, N>& limbs) {")
    print("    return -pos_from_limbs(limbs);")
    print("}")
    print()
    print("} // namespace")
    print()
    for case in cases:
        print(case)
        print()


if __name__ == "__main__":
    main()
