// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// "Do we need asm for linear kernels?" headroom probe: library span kernels versus loop variants and GMP's mpn.
// Standalone program, not part of the CMake build; build with build_mac.sh / build_x64.sh in this directory.
// Needs only `-I include` (the headers fall back to their own feature detection when no generated config header is on
// the include path).
//
//   probe --list                       list kernel:variant names
//   probe --variant K:V --n N --reps R run one variant R times (for perf stat)
//   probe --all [--rounds 7] [--ms 20] check everything, then CSV kernel,variant,n,ns_per_limb
//   probe --check                      correctness checks only

#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// ---- GMP (hand-declared; mp_limb_t = unsigned long, mp_size_t = long) -------------------------
using gl = unsigned long;
extern "C" {
gl __gmpn_add_n(gl*, const gl*, const gl*, long);
gl __gmpn_sub_n(gl*, const gl*, const gl*, long);
gl __gmpn_lshift(gl*, const gl*, long, unsigned);
gl __gmpn_submul_1(gl*, const gl*, long, gl);
gl __gmpn_mul_1(gl*, const gl*, long, gl);
gl __gmpn_divrem_1(gl*, long, const gl*, long, gl);
#ifdef PROBE_HAVE_BY3C
gl __gmpn_divexact_by3c(gl*, const gl*, long, gl);
#endif
#ifdef PROBE_HAVE_BDIV_Q_1
gl __gmpn_bdiv_q_1(gl*, const gl*, long, gl);
#endif
#ifdef PROBE_HAVE_DIVEXACT_1
void __gmpn_divexact_1(gl*, const gl*, long, gl);
#endif
}

namespace dt = BEMAN_BIG_INT_NAMESPACE::detail;
using limb   = unsigned long long;
static_assert(sizeof(limb) == sizeof(gl) && std::is_same_v<limb, BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t>);

inline static gl*       G(limb* p) { return reinterpret_cast<gl*>(p); }
inline static const gl* G(const limb* p) { return reinterpret_cast<const gl*>(p); }

#define NOINL __attribute__((noinline))

// ---- test buffers ------------------------------------------------------------------------------
constexpr std::size_t kMaxN = 512;
constexpr int         kSets = 3;
constexpr std::size_t kPad  = kMaxN + 8;

struct Buf {
    alignas(64) limb a[kPad];
    alignas(64) limb b[kPad];
    alignas(64) limb r[kPad];
};
static Buf g_bufs[kSets];

struct Args {
    Buf&        B;
    std::size_t n;
    limb        extra; // multiplier / divisor / shift count
};
using Fn = limb (*)(const Args&);

static limb splitmix(std::uint64_t& s) {
    std::uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
    z               = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z               = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline static void escape(limb v) { asm volatile("" : : "r"(v) : "memory"); }

// ---- variants ----------------------------------------------------------------------------------
using cspan = std::span<const limb>;
using mspan = std::span<limb>;

// add_n -----------------------------------------------------------------------------------------
NOINL static limb add_lib(const Args& x) {
    return dt::add_unsigned_spans(mspan{x.B.r, x.n}, cspan{x.B.a, x.n}, cspan{x.B.b, x.n});
}
NOINL static limb add_loop(const Args& x) {
    bool carry = false;
    for (std::size_t i = 0; i < x.n; ++i) {
        const auto [v, c] = dt::carrying_add(x.B.a[i], x.B.b[i], carry);
        x.B.r[i]          = v;
        carry             = c;
    }
    return carry;
}
#if __has_builtin(__builtin_addcll)
NOINL static limb add_addcll4(const Args& x) {
    const limb *a = x.B.a, *b = x.B.b;
    limb*       r = x.B.r;
    limb        c = 0;
    std::size_t i = 0;
    for (; i + 4 <= x.n; i += 4) {
        r[i]     = __builtin_addcll(a[i], b[i], c, &c);
        r[i + 1] = __builtin_addcll(a[i + 1], b[i + 1], c, &c);
        r[i + 2] = __builtin_addcll(a[i + 2], b[i + 2], c, &c);
        r[i + 3] = __builtin_addcll(a[i + 3], b[i + 3], c, &c);
    }
    for (; i < x.n; ++i) {
        r[i] = __builtin_addcll(a[i], b[i], c, &c);
    }
    return c;
}
    #define PROBE_HAVE_ADDCLL4 1
#endif
#if defined(__x86_64__)
// Tail (n % 4 limbs) in C, then a 4x unrolled adc loop; dec and lea leave CF intact.
NOINL static limb add_asm4(const Args& x) {
    const limb *a = x.B.a, *b = x.B.b;
    limb*       r = x.B.r;
    limb        c = 0;
    std::size_t i = 0;
    for (std::size_t t = x.n & 3; i < t; ++i) {
        const auto [v, cc] = dt::carrying_add(a[i], b[i], static_cast<bool>(c));
        r[i]               = v;
        c                  = cc;
    }
    std::size_t cnt = (x.n - i) >> 2;
    if (cnt != 0) {
        asm volatile("neg %[c]\n\t"
                     ".p2align 4\n"
                     "1:\n\t"
                     "mov (%[a],%[i],8), %%r8\n\t"
                     "mov 8(%[a],%[i],8), %%r9\n\t"
                     "mov 16(%[a],%[i],8), %%r10\n\t"
                     "mov 24(%[a],%[i],8), %%r11\n\t"
                     "adc (%[b],%[i],8), %%r8\n\t"
                     "adc 8(%[b],%[i],8), %%r9\n\t"
                     "adc 16(%[b],%[i],8), %%r10\n\t"
                     "adc 24(%[b],%[i],8), %%r11\n\t"
                     "mov %%r8, (%[r],%[i],8)\n\t"
                     "mov %%r9, 8(%[r],%[i],8)\n\t"
                     "mov %%r10, 16(%[r],%[i],8)\n\t"
                     "mov %%r11, 24(%[r],%[i],8)\n\t"
                     "lea 4(%[i]), %[i]\n\t"
                     "dec %[cnt]\n\t"
                     "jnz 1b\n\t"
                     "sbb %[c], %[c]\n\t"
                     "neg %[c]\n"
                     : [c] "+r"(c), [i] "+r"(i), [cnt] "+r"(cnt)
                     : [a] "r"(a), [b] "r"(b), [r] "r"(r)
                     : "r8", "r9", "r10", "r11", "cc", "memory");
    }
    return c;
}
    #define PROBE_HAVE_ASM4 1
#endif
NOINL static limb add_gmp(const Args& x) { return __gmpn_add_n(G(x.B.r), G(x.B.a), G(x.B.b), static_cast<long>(x.n)); }

// sub_n -----------------------------------------------------------------------------------------
// Library kernel asserts a >= b and returns a trimmed size, so the wrapper reports borrow 0.
NOINL static limb sub_lib(const Args& x) {
    escape(dt::subtract_unsigned_spans(mspan{x.B.r, x.n}, cspan{x.B.a, x.n}, cspan{x.B.b, x.n}));
    return 0;
}
NOINL static limb sub_loop(const Args& x) {
    bool borrow = false;
    for (std::size_t i = 0; i < x.n; ++i) {
        const auto [v, c] = dt::borrowing_sub(x.B.a[i], x.B.b[i], borrow);
        x.B.r[i]          = v;
        borrow            = c;
    }
    return borrow;
}
NOINL static limb sub_gmp(const Args& x) { return __gmpn_sub_n(G(x.B.r), G(x.B.a), G(x.B.b), static_cast<long>(x.n)); }

// lshift: x.extra = bit count (1..63). r[0..n) = a << cnt; returns bits shifted out.
NOINL static limb lsh_lib(const Args& x) {
    std::memcpy(x.B.r, x.B.a, x.n * sizeof(limb));
    x.B.r[x.n] = 0;
    escape(dt::shift_left_n(mspan{x.B.r, x.n + 1}, x.n, static_cast<unsigned>(x.extra)));
    return x.B.r[x.n];
}
NOINL static limb lsh_gmp(const Args& x) {
    return __gmpn_lshift(G(x.B.r), G(x.B.a), static_cast<long>(x.n), static_cast<unsigned>(x.extra));
}
NOINL static limb lsh_gmp_copy(const Args& x) {
    std::memcpy(x.B.r, x.B.a, x.n * sizeof(limb));
    return __gmpn_lshift(G(x.B.r), G(x.B.r), static_cast<long>(x.n), static_cast<unsigned>(x.extra));
}

// submul_1: r[0..n) -= a[0..n) * extra; returns borrow limb.
NOINL static limb smul_lib(const Args& x) {
    return dt::submul_single_limb(mspan{x.B.r, x.n}, cspan{x.B.a, x.n}, x.extra);
}
NOINL static limb smul_gmp(const Args& x) {
    return __gmpn_submul_1(G(x.B.r), G(x.B.a), static_cast<long>(x.n), x.extra);
}

// mul_1: r[0..n) = a * extra; returns carry limb (library: r[n]).
NOINL static limb mul1_lib(const Args& x) {
    x.B.r[x.n] = 0;
    escape(dt::multiply_single_limb(mspan{x.B.r, x.n + 1}, cspan{x.B.a, x.n}, x.extra));
    return x.B.r[x.n];
}
NOINL static limb mul1_gmp(const Args& x) { return __gmpn_mul_1(G(x.B.r), G(x.B.a), static_cast<long>(x.n), x.extra); }

// divrem_1 / divexact: q = a / extra, returns remainder.
NOINL static limb div_lib(const Args& x) {
    return dt::divide_unsigned_short(mspan{x.B.r, x.n}, cspan{x.B.a, x.n}, x.extra);
}
NOINL static limb div_gmp(const Args& x) {
    return __gmpn_divrem_1(G(x.B.r), 0, G(x.B.a), static_cast<long>(x.n), x.extra);
}
#ifdef PROBE_HAVE_BY3C
NOINL static limb by3c_gmp(const Args& x) {
    return __gmpn_divexact_by3c(G(x.B.r), G(x.B.a), static_cast<long>(x.n), 0);
}
#endif
#ifdef PROBE_HAVE_BDIV_Q_1
NOINL static limb bdivq1_gmp(const Args& x) {
    __gmpn_bdiv_q_1(G(x.B.r), G(x.B.a), static_cast<long>(x.n), x.extra);
    return 0;
}
#endif
#ifdef PROBE_HAVE_DIVEXACT_1
NOINL static limb dexact1_gmp(const Args& x) {
    __gmpn_divexact_1(G(x.B.r), G(x.B.a), static_cast<long>(x.n), x.extra);
    return 0;
}
#endif

// ---- registry ----------------------------------------------------------------------------------
enum class Prep { plain, sub_ge, divisible3, nonzero_top };
struct Variant {
    const char* name;
    Fn          fn;
    bool        ref; // reference (GMP) for the kernel's correctness check
};
struct Kernel {
    const char*          name;
    limb                 extra;
    Prep                 prep;
    bool                 r_in; // r holds an input (submul): r is reset to r0 before each check run
    std::vector<Variant> v;
};

static std::vector<Kernel> make_kernels() {
    std::vector<Kernel> k;
    {
        Kernel a{"add_n", 0, Prep::plain, false, {}};
        a.v.push_back({"lib_add_unsigned_spans", add_lib, false});
        a.v.push_back({"loop_carrying_add", add_loop, false});
#ifdef PROBE_HAVE_ADDCLL4
        a.v.push_back({"addcll_x4", add_addcll4, false});
#endif
#ifdef PROBE_HAVE_ASM4
        a.v.push_back({"asm_adc_x4", add_asm4, false});
#endif
        a.v.push_back({"gmp", add_gmp, true});
        k.push_back(a);
    }
    {
        Kernel s{"sub_n", 0, Prep::sub_ge, false, {}};
        s.v.push_back({"lib_subtract_unsigned_spans", sub_lib, false});
        s.v.push_back({"loop_borrowing_sub", sub_loop, false});
        s.v.push_back({"gmp", sub_gmp, true});
        k.push_back(s);
    }
    for (const limb cnt : {limb{13}, limb{1}, limb{63}}) {
        const char* nm = cnt == 13 ? "lshift13" : cnt == 1 ? "lshift1" : "lshift63";
        Kernel      l{nm, cnt, Prep::plain, false, {}};
        l.v.push_back({"lib_copy_shift_left_n", lsh_lib, false});
        l.v.push_back({"gmp_copy_inplace", lsh_gmp_copy, false});
        l.v.push_back({"gmp", lsh_gmp, true});
        k.push_back(l);
    }
    {
        Kernel s{"submul_1", 0x9e3779b97f4a7c15ULL, Prep::plain, true, {}};
        s.v.push_back({"lib_submul_single_limb", smul_lib, false});
        s.v.push_back({"gmp", smul_gmp, true});
        k.push_back(s);
        Kernel m{"mul_1", 0x9e3779b97f4a7c15ULL, Prep::plain, false, {}};
        m.v.push_back({"lib_multiply_single_limb", mul1_lib, false});
        m.v.push_back({"gmp", mul1_gmp, true});
        k.push_back(m);
    }
    {
        Kernel d{"divrem_1_norm", 0xF123456789ABCDEFULL, Prep::plain, false, {}};
        d.v.push_back({"lib_divide_unsigned_short", div_lib, false});
        d.v.push_back({"gmp", div_gmp, true});
        k.push_back(d);
        Kernel u{"divrem_1_unnorm", 1000000007ULL, Prep::plain, false, {}};
        u.v.push_back({"lib_divide_unsigned_short", div_lib, false});
        u.v.push_back({"gmp", div_gmp, true});
        k.push_back(u);
    }
    {
        Kernel e{"divexact_3", 3, Prep::divisible3, false, {}};
        e.v.push_back({"lib_divide_unsigned_short", div_lib, false});
        e.v.push_back({"gmp_divrem_1", div_gmp, true});
#ifdef PROBE_HAVE_BY3C
        e.v.push_back({"gmp_divexact_by3c", by3c_gmp, false});
#endif
#ifdef PROBE_HAVE_BDIV_Q_1
        e.v.push_back({"gmp_bdiv_q_1", bdivq1_gmp, false});
#endif
#ifdef PROBE_HAVE_DIVEXACT_1
        e.v.push_back({"gmp_divexact_1", dexact1_gmp, false});
#endif
        k.push_back(e);
    }
    return k;
}

static void fill(Buf& B, std::uint64_t seed, Prep prep) {
    std::uint64_t s = seed;
    for (std::size_t i = 0; i < kPad; ++i) {
        B.a[i] = splitmix(s);
        B.b[i] = splitmix(s);
        B.r[i] = splitmix(s);
    }
    // Give sub_n a >= b at every n: top limb of a above b's. For n >= 1 any prefix n
    // needs its own top limb ordering, so make a[i] > b[i] limbwise (no borrows at all
    // is too easy), so instead force b[i] = a[i] >> 1 on odd i and keep random on even i.
    if (prep == Prep::sub_ge) {
        for (std::size_t i = 0; i < kPad; ++i) {
            if (i % 2 == 1) {
                B.b[i] = B.a[i] >> 1; // forces a[i] > b[i] on odd limbs
            }
            if (i % 5 == 0) {
                B.b[i] = B.a[i]; // runs of equal limbs exercise borrow chains
            }
        }
        for (std::size_t i = 0; i < kPad; ++i) {
            if (i % 7 == 3) {
                B.b[i] = B.a[i] + 1; // introduces borrows
            }
        }
        // Make every prefix satisfy a >= b by making the final limb of any prefix larger:
        // enforced below per-n by the checker through a_top adjustments (see check_one).
    }
    if (prep == Prep::divisible3) {
        for (std::size_t i = 0; i < kPad; ++i) {
            B.a[i] = splitmix(s);
        }
    }
}

// Prepare the n-limb prefix so the kernel's preconditions hold.
static void shape_for_n(Buf& B, std::size_t n, Prep prep) {
    if (prep == Prep::sub_ge) {
        B.b[n - 1] = B.a[n - 1] >> 1; // top limb strictly smaller when a[n-1] != 0
        B.a[n - 1] |= 1ULL << 63;
        B.b[n - 1] &= ~(1ULL << 63);
    } else if (prep == Prep::divisible3) {
        B.a[n - 1] >>= 2;
        const gl cy = __gmpn_mul_1(G(B.a), G(B.a), static_cast<long>(n), 3);
        if (cy != 0) {
            std::fprintf(stderr, "divexact prep overflow\n");
            std::abort();
        }
    }
}

[[noreturn]] static void die(const char* k, const char* v, std::size_t n, limb extra, const char* what) {
    std::fprintf(stderr, "MISMATCH %s:%s n=%zu extra=%llu: %s\n", k, v, n, extra, what);
    std::abort();
}

static void check_kernel(const Kernel& k, const std::vector<std::size_t>& ns, const std::vector<limb>& extras) {
    const Variant* ref = nullptr;
    for (const auto& v : k.v) {
        if (v.ref) {
            ref = &v;
        }
    }
    static Buf Bref, Bv, Binit;
    for (const std::size_t n : ns) {
        for (const limb ex : extras) {
            for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                fill(Binit, seed * 7919 + n, k.prep);
                shape_for_n(Binit, n, k.prep);
                if (seed == 3) { // extreme limbs: all ones
                    for (std::size_t i = 0; i < n && k.prep == Prep::plain; ++i) {
                        Binit.a[i] = ~0ULL;
                        Binit.b[i] = (i & 1) ? ~0ULL : 0;
                        Binit.r[i] = ~0ULL;
                    }
                }
                Bref            = Binit;
                const limb want = ref->fn({Bref, n, ex});
                for (const auto& v : k.v) {
                    Bv           = Binit;
                    const limb g = v.fn({Bv, n, ex});
                    if (g != want) {
                        die(k.name, v.name, n, ex, "scalar result");
                    }
                    if (std::memcmp(Bv.r, Bref.r, n * sizeof(limb)) != 0) {
                        die(k.name, v.name, n, ex, "limbs");
                    }
                    if (std::memcmp(Bv.a, Binit.a, n * sizeof(limb)) != 0) {
                        die(k.name, v.name, n, ex, "input a clobbered");
                    }
                }
            }
        }
    }
}

static void check_all(const std::vector<Kernel>& ks) {
    const std::vector<std::size_t> ns = {1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 64, 65, 255, 511, 512};
    for (const auto& k : ks) {
        std::vector<limb>        extras = {k.extra};
        std::vector<std::size_t> kn     = ns;
        if (std::strcmp(k.name, "lshift13") == 0) {
            extras.clear();
            for (limb c = 1; c < 64; ++c) {
                extras.push_back(c);
            }
        }
        if (std::strncmp(k.name, "divrem_1_", 9) == 0) {
            extras = {k.extra, 1ULL << 63, 3, 0xFFFFFFFFFFFFFFFFULL, 12345, (1ULL << 32) + 1};
        }
        if (std::strcmp(k.name, "submul_1") == 0 || std::strcmp(k.name, "mul_1") == 0) {
            extras = {k.extra, 0, 1, ~0ULL, 1ULL << 63};
        }
        check_kernel(k, kn, extras);
    }
    std::fprintf(stderr, "correctness: all kernels match GMP\n");
}

// ---- timing ------------------------------------------------------------------------------------
static void init_bufs(const Kernel& k, std::size_t n) {
    for (int s = 0; s < kSets; ++s) {
        fill(g_bufs[s], 100 + s, k.prep);
        shape_for_n(g_bufs[s], n, k.prep);
    }
}

static limb run_reps(const Variant& v, const Kernel& k, std::size_t n, std::uint64_t reps) {
    limb acc = 0;
    int  s   = 0;
    for (std::uint64_t i = 0; i < reps; ++i) {
        acc += v.fn({g_bufs[s], n, k.extra});
        escape(acc);
        if (++s == kSets) {
            s = 0;
        }
    }
    return acc;
}

static double ns_now() {
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static double time_variant(const Variant& v, const Kernel& k, std::size_t n, int rounds, double ms) {
    init_bufs(k, n);
    std::uint64_t reps = 16;
    for (;;) { // calibrate to ~ms per round
        const double t0 = ns_now();
        run_reps(v, k, n, reps);
        const double dt_ns = ns_now() - t0;
        if (dt_ns >= ms * 1e6 * 0.5 || reps > (1ULL << 40)) {
            reps = static_cast<std::uint64_t>(static_cast<double>(reps) * (ms * 1e6) / (dt_ns > 1 ? dt_ns : 1));
            if (reps < 1) {
                reps = 1;
            }
            break;
        }
        reps *= 4;
    }
    double best = 1e300;
    for (int r = 0; r < rounds; ++r) {
        const double t0 = ns_now();
        run_reps(v, k, n, reps);
        const double t = ns_now() - t0;
        if (t < best) {
            best = t;
        }
    }
    return best / (static_cast<double>(reps) * static_cast<double>(n));
}

int main(int argc, char** argv) {
    std::string   var;
    std::size_t   n      = 0;
    std::uint64_t reps   = 0;
    int           rounds = 7;
    double        ms     = 20;
    bool          all = false, list = false, check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--variant" && i + 1 < argc) {
            var = argv[++i];
        } else if (a == "--n" && i + 1 < argc) {
            n = std::strtoull(argv[++i], nullptr, 10);
        } else if (a == "--reps" && i + 1 < argc) {
            reps = std::strtoull(argv[++i], nullptr, 10);
        } else if (a == "--rounds" && i + 1 < argc) {
            rounds = std::atoi(argv[++i]);
        } else if (a == "--ms" && i + 1 < argc) {
            ms = std::atof(argv[++i]);
        } else if (a == "--all") {
            all = true;
        } else if (a == "--list") {
            list = true;
        } else if (a == "--check") {
            check = true;
        } else {
            std::fprintf(stderr, "bad arg %s\n", a.c_str());
            return 2;
        }
    }
    const auto ks = make_kernels();
    if (list) {
        for (const auto& k : ks) {
            for (const auto& v : k.v) {
                std::printf("%s:%s\n", k.name, v.name);
            }
        }
        return 0;
    }
    if (var.empty()) {
        check_all(ks); // verify before timing (--variant runs skip it: perf must see only the kernel)
    }
    if (check) {
        return 0;
    }
    if (all) {
        std::printf("kernel,variant,n,ns_per_limb\n");
        for (const auto& k : ks) {
            for (const std::size_t nn : {std::size_t{8}, std::size_t{64}, std::size_t{512}}) {
                for (const auto& v : k.v) {
                    std::printf("%s,%s,%zu,%.4f\n", k.name, v.name, nn, time_variant(v, k, nn, rounds, ms));
                    std::fflush(stdout);
                }
            }
        }
        return 0;
    }
    if (!var.empty() && n != 0 && reps != 0) {
        for (const auto& k : ks) {
            for (const auto& v : k.v) {
                if (var == std::string(k.name) + ":" + v.name) {
                    init_bufs(k, n);
                    const limb acc = run_reps(v, k, n, reps);
                    std::printf(
                        "%s n=%zu reps=%llu acc=%llu\n", var.c_str(), n, static_cast<unsigned long long>(reps), acc);
                    return 0;
                }
            }
        }
        std::fprintf(stderr, "unknown variant %s\n", var.c_str());
        return 2;
    }
    std::fprintf(stderr, "usage: probe --list | --check | --all | --variant K:V --n N --reps R\n");
    return 2;
}
