// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP
#define BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP

#include <beman/big_int/detail/config.hpp>

#ifndef BEMAN_BIG_INT_BUILD_MODULE
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <type_traits>
#endif

BEMAN_BIG_INT_BEGIN_NAMESPACE
namespace detail {

// Scratchpad bump allocator used by the multiplication and division
// algorithms. Allocates LIFO from a pre-sized buffer; "deallocation" simply
// rewinds the bump pointer so sibling recursive branches reuse the same memory.
//
// The base class holds the bump-pointer state and the type-erased
// allocate/deallocate interface that the algorithms call per recursive level.
// The templated derived class owns the user-provided Allocator and performs
// the initial heap allocation / final deallocation. Splitting in this way lets
// the algorithm implementations live in non-template .cpp files: they only
// touch the base, so a single compiled definition serves every allocator type.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

// Type-erased heap allocation for the runtime workspaces that cannot live in
// the limb bump buffer: the FFT tiers' std::uint64_t and double arrays and
// the per-product limb buffers of the non-template kernels in src/. The
// element types are exactly these three, so the derived scratch_allocator
// installs one rebind-backed hook pair per type -- every user allocator
// works through its own rebinds, with no per-allocator template
// instantiation in the kernels.
struct scratch_heap_source {
    uint_multiprecision_t* (*allocate_limbs)(void* ctx, std::size_t n)                    = nullptr;
    void (*deallocate_limbs)(void* ctx, uint_multiprecision_t* p, std::size_t n) noexcept = nullptr;
    std::uint64_t* (*allocate_u64)(void* ctx, std::size_t n)                              = nullptr;
    void (*deallocate_u64)(void* ctx, std::uint64_t* p, std::size_t n) noexcept           = nullptr;
    double* (*allocate_f64)(void* ctx, std::size_t n)                                     = nullptr;
    void (*deallocate_f64)(void* ctx, double* p, std::size_t n) noexcept                  = nullptr;
    void* ctx                                                                             = nullptr;
};

struct scratch_allocator_base {
    uint_multiprecision_t* m_base     = nullptr;
    std::size_t            m_capacity = 0;
    std::size_t            m_offset   = 0;

    // Running high-water mark of `m_offset` across the lifetime of this
    // allocator, used by tests/benchmarks to validate the _storage_size
    // models. Unconditional: the update is one compare-and-store per
    // allocate(), amortized over a whole recursive multiplication or
    // division (measured well below noise), and a macro-gated member would
    // change the class layout between the src/ kernels and any test TU
    // defining the macro.
    std::size_t m_peak = 0;

    scratch_heap_source m_heap{};

    constexpr scratch_allocator_base() noexcept = default;
    constexpr scratch_allocator_base(uint_multiprecision_t* base, const std::size_t capacity) noexcept
        : m_base(base), m_capacity(capacity) {}

    scratch_allocator_base(const scratch_allocator_base&)            = delete;
    scratch_allocator_base& operator=(const scratch_allocator_base&) = delete;

    // Bump-allocate `n` limbs from the workspace, returned as a mutable span.
    constexpr std::span<uint_multiprecision_t> allocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(m_offset + n <= m_capacity);
        auto* p = m_base + m_offset;
        m_offset += n;

        if (m_offset > m_peak) {
            m_peak = m_offset;
        }

        return {p, n};
    }

    // LIFO release of the last `n` limbs so sibling branches can reuse them.
    constexpr void deallocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(n <= m_offset);
        m_offset -= n;
    }

    // Highest value `m_offset` has reached since construction. Used to tune
    // the _storage_size heuristics.
    [[nodiscard]] constexpr std::size_t peak() const noexcept { return m_peak; }

    // The type-erased heap hooks (empty when constructed without an owning
    // allocator, e.g. over a plain stack buffer with no allocator attached).
    [[nodiscard]] constexpr const scratch_heap_source& heap() const noexcept { return m_heap; }
};

// RAII heap workspace of one of the three hook element types; the span is
// default-initialized (every kernel writes its workspace fully before
// reading). Runtime only.
template <class T>
class scratch_heap_array {
    static_assert(std::is_same_v<T, uint_multiprecision_t> || std::is_same_v<T, std::uint64_t> ||
                  std::is_same_v<T, double>);

  public:
    scratch_heap_array(const scratch_heap_source& src, const std::size_t count) : m_src(src), m_count(count) {
        if constexpr (std::is_same_v<T, uint_multiprecision_t>) {
            m_data = m_src.allocate_limbs(m_src.ctx, count);
        } else if constexpr (std::is_same_v<T, double>) {
            m_data = m_src.allocate_f64(m_src.ctx, count);
        } else {
            m_data = m_src.allocate_u64(m_src.ctx, count);
        }
    }

    ~scratch_heap_array() {
        if constexpr (std::is_same_v<T, uint_multiprecision_t>) {
            m_src.deallocate_limbs(m_src.ctx, m_data, m_count);
        } else if constexpr (std::is_same_v<T, double>) {
            m_src.deallocate_f64(m_src.ctx, m_data, m_count);
        } else {
            m_src.deallocate_u64(m_src.ctx, m_data, m_count);
        }
    }

    scratch_heap_array(const scratch_heap_array&)            = delete;
    scratch_heap_array& operator=(const scratch_heap_array&) = delete;

    [[nodiscard]] std::span<T> span() const noexcept { return {m_data, m_count}; }
    [[nodiscard]] T*           data() const noexcept { return m_data; }

  private:
    const scratch_heap_source& m_src;
    T*                         m_data;
    std::size_t                m_count;
};

template <class Allocator>
struct scratch_allocator : scratch_allocator_base {
    using alloc_traits = std::allocator_traits<Allocator>;
    using pointer      = typename alloc_traits::pointer;

    // NOTE: deliberately plain [[no_unique_address]], NOT BEMAN_BIG_INT_NO_UNIQUE_ADDRESS.
    // BEMAN_BIG_INT_NO_UNIQUE_ADDRESS expands to [[msvc::no_unique_address]] which miscompiles leading to segfault
    // MSVC ignores plain [[no_unique_address]] which works fine (and correctly) with GCC and Clang
    [[no_unique_address]] Allocator m_alloc;
    pointer                         m_owned_pointer = pointer{};
    bool                            m_owns;

    // Wrap an existing stack buffer — no ownership.
    constexpr scratch_allocator(pointer buf, const std::size_t cap, const Allocator& alloc) noexcept
        : scratch_allocator_base(std::to_address(buf), cap), m_alloc(alloc), m_owns(false) {
        install_heap_hooks();
    }

    // No limb buffer at all: a pure carrier for the type-erased heap hooks
    // (the runtime dispatchers in src/ size and own their own workspaces).
    explicit constexpr scratch_allocator(const Allocator& alloc) noexcept : m_alloc(alloc), m_owns(false) {
        install_heap_hooks();
    }

    // Heap-allocate at least `cap` limbs using the provided allocator.
    constexpr scratch_allocator(std::size_t cap, const Allocator& alloc) : m_alloc(alloc), m_owns(true) {
        install_heap_hooks();
#ifdef BEMAN_BIG_INT_HAS_CPP_LIB_ALLOCATE_AT_LEAST
        if constexpr (traits_has_allocate_at_least<alloc_traits, Allocator>) {
            auto result     = alloc_traits::allocate_at_least(m_alloc, cap);
            m_owned_pointer = result.ptr;
            m_capacity      = result.count;
        } else {
            m_owned_pointer = alloc_traits::allocate(m_alloc, cap);
            m_capacity      = cap;
        }
#else
        m_owned_pointer = alloc_traits::allocate(m_alloc, cap);
        m_capacity      = cap;
#endif
        m_base = std::to_address(m_owned_pointer);

        // At constant evaluation the allocated limbs' lifetimes have not
        // begun, so plain assignment into them (std::ranges::copy/fill in the
        // algorithms) is ill-formed; start them here. Free at runtime.
        if BEMAN_BIG_INT_IS_CONSTEVAL {
            for (std::size_t i = 0; i < m_capacity; ++i) {
                std::construct_at(m_base + i, uint_multiprecision_t{0});
            }
        }
    }

    constexpr ~scratch_allocator() {
        if (m_owns) {
            alloc_traits::deallocate(m_alloc, m_owned_pointer, m_capacity);
        }
    }

    scratch_allocator(const scratch_allocator&)            = delete;
    scratch_allocator& operator=(const scratch_allocator&) = delete;

  private:
    // The heap hooks rebind the owning allocator to the requested element
    // type, so every allocator's own allocation behavior (pmr resources,
    // counting wrappers, rings, ...) carries through the type-erased
    // interface. Elements are default-initialized (a no-op for these
    // trivial types) to begin their lifetimes; all kernels write their
    // workspaces fully before reading.
    template <class U>
    static U* heap_allocate(void* ctx, const std::size_t n) {
        auto& self = *static_cast<scratch_allocator*>(ctx);
        using rb   = typename alloc_traits::template rebind_alloc<U>;
        rb alloc   = rb(self.m_alloc);
        U* p       = std::allocator_traits<rb>::allocate(alloc, n);
        std::uninitialized_default_construct_n(p, n);
        return p;
    }

    template <class U>
    static void heap_deallocate(void* ctx, U* p, const std::size_t n) noexcept {
        auto& self = *static_cast<scratch_allocator*>(ctx);
        using rb   = typename alloc_traits::template rebind_alloc<U>;
        rb alloc   = rb(self.m_alloc);
        std::allocator_traits<rb>::deallocate(alloc, p, n);
    }

    constexpr void install_heap_hooks() noexcept {
        m_heap = scratch_heap_source{&heap_allocate<uint_multiprecision_t>,
                                     &heap_deallocate<uint_multiprecision_t>,
                                     &heap_allocate<std::uint64_t>,
                                     &heap_deallocate<std::uint64_t>,
                                     &heap_allocate<double>,
                                     &heap_deallocate<double>,
                                     this};
    }
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

} // namespace detail
BEMAN_BIG_INT_END_NAMESPACE

#endif // BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP
