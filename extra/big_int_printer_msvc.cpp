// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Visual Studio debugger expression-evaluator add-in for
// beman::big_int::basic_big_int<min_inplace_bits, Limb, Allocator>.
//
// Natvis expressions cannot call functions and the debugger has no wide-integer
// arithmetic, so a pure .natvis visualizer cannot render a big_int in decimal
// beyond 64 bits. This DLL closes that gap: big_int_printer_msvc.natvis routes
// the summary through the exports below, which read the limbs out of the
// debuggee, rebuild the value locally, and run it through `to_chars`. The result
// is exact decimal (or hexadecimal) at any width.
//
// The add-in runs inside the debugger, so every entry point is noexcept in
// practice: an escaping exception or a fault would take down Visual Studio.
// Everything below returns E_FAIL on any problem, which makes the debugger fall
// back to the raw field view.
//
// Layout read from the debuggee (see include/beman/big_int/big_int.hpp):
//     offset 0: std::uint32_t m_capacity;      // 0 = in-place, >0 = heap capacity
//     offset 4: std::uint32_t m_size_and_sign; // bit 31 = sign, bits 0-30 = limb count
//     offset 8: union { pointer data; limb_type limbs[inplace_capacity]; }
// The magnitude is little-endian across the limbs, and the sign bit is never set
// when the magnitude is zero. Only the live limbs are ever read, never the whole
// in-place array, so this works for every `min_inplace_bits`.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/string.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using limb_type = BEMAN_BIG_INT_NAMESPACE::uint_multiprecision_t;
using big_int   = BEMAN_BIG_INT_NAMESPACE::big_int;

// The debuggee's representation header. basic_big_int is standard-layout with
// these two words first, and the union that follows is limb-aligned, so the
// limbs (or the heap pointer) start at offset 8 on every supported target.
struct representation_header {
    std::uint32_t capacity;
    std::uint32_t size_and_sign;
};

inline constexpr DWORD storage_offset = sizeof(representation_header);
static_assert(storage_offset == 8);
static_assert(sizeof(big_int) >= storage_offset + sizeof(limb_type));

// Conversion cost grows with the limb count, and this runs synchronously inside
// the debugger's UI. Values wider than this are summarized instead of converted,
// so a runaway or uninitialized object cannot hang Visual Studio.
inline constexpr std::uint32_t max_convertible_limbs = 65536;

// Largest limb count the representation can even express; anything above this is
// garbage rather than a value.
inline constexpr std::size_t max_representable_limbs = big_int{}.max_representation_size();

// The add-in DLL is loaded by the debugger, but it is built for the debuggee's
// architecture (it links the same static library), so its pointer width is the
// debuggee's pointer width.
inline constexpr DWORD debuggee_pointer_size = sizeof(void*);

struct DEBUGHELPER {
    DWORD m_version;
    HRESULT(WINAPI* m_pfn_ReadDebuggeeMemory)(DEBUGHELPER* self, DWORD addr, DWORD want, void* where, DWORD* got);
    DWORDLONG(WINAPI* m_pfn_GetRealAddress)(DEBUGHELPER* self);
    HRESULT(WINAPI* m_pfn_ReadDebuggeeMemoryEx)(
        DEBUGHELPER* self, DWORDLONG addr, DWORD want, void* where, DWORD* got);
    int(WINAPI* m_pfn_GetProcessorType)(DEBUGHELPER* self);
};

// Reads `want` bytes at `addr` out of the debugged process. Uses the 64-bit
// capable entry point whenever the debugger offers it, which is every debugger
// since Visual Studio .NET; the narrow one is the Visual C++ 6.0 fallback and
// only reaches 32-bit addresses.
bool read_debuggee(DEBUGHELPER* const helper, const DWORDLONG addr, const DWORD want, void* const where) {
    if (helper == nullptr || want == 0) {
        return false;
    }

    DWORD   got = 0;
    HRESULT hr  = E_FAIL;

    if (helper->m_version >= 0x00020000 && helper->m_pfn_ReadDebuggeeMemoryEx != nullptr) {
        hr = helper->m_pfn_ReadDebuggeeMemoryEx(helper, addr, want, where, &got);
    } else if (helper->m_pfn_ReadDebuggeeMemory != nullptr && addr <= 0xFFFFFFFFull) {
        hr = helper->m_pfn_ReadDebuggeeMemory(helper, static_cast<DWORD>(addr), want, where, &got);
    }

    return hr == S_OK && got == want;
}

// The address of the object being visualized. `address` carries only the low 32
// bits, so the 64-bit accessor is preferred wherever it exists.
DWORDLONG object_address(DEBUGHELPER* const helper, const DWORD address) {
    if (helper->m_version >= 0x00020000 && helper->m_pfn_GetRealAddress != nullptr) {
        return helper->m_pfn_GetRealAddress(helper);
    }
    return address;
}

// Pulls the magnitude limbs of the visualized object into `limbs`, and reports
// its sign. Returns false if the object does not look like a live big_int, which
// is the expected outcome for uninitialized or optimized-away storage.
bool read_representation(DEBUGHELPER* const      helper,
                         const DWORD             address,
                         std::vector<limb_type>& limbs,
                         bool&                   negative,
                         std::uint32_t&          limb_count) {
    const DWORDLONG base = object_address(helper, address);

    representation_header header{};
    if (!read_debuggee(helper, base, sizeof(header), &header)) {
        return false;
    }

    limb_count = header.size_and_sign & 0x7FFF'FFFFU;
    negative   = (header.size_and_sign & 0x8000'0000U) != 0;

    // A limb count of zero is a class-invariant violation; anything past the
    // representable maximum is garbage rather than a value.
    if (limb_count == 0 || limb_count > max_representable_limbs) {
        return false;
    }

    DWORDLONG limbs_address = base + storage_offset;
    if (header.capacity != 0) {
        // Heap storage: the union holds the pointer, and the capacity must cover
        // the live limbs.
        if (header.capacity < limb_count) {
            return false;
        }
        DWORDLONG pointer = 0;
        if (!read_debuggee(helper, limbs_address, debuggee_pointer_size, &pointer)) {
            return false;
        }
        if (pointer == 0) {
            return false;
        }
        limbs_address = pointer;
    }

    if (limb_count > max_convertible_limbs) {
        // Caller renders a summary instead; the limbs are not needed. `limbs`
        // stays empty, which is how the caller tells the two paths apart.
        return true;
    }

    limbs.resize(limb_count);
    return read_debuggee(helper, limbs_address, static_cast<DWORD>(limb_count * sizeof(limb_type)), limbs.data());
}

// Renders the magnitude in `limbs` in the requested base, with a sign and, for
// base 16, an `0x` prefix.
std::string render(const std::vector<limb_type>& limbs, const bool negative, const int base) {
    big_int value(limbs.begin(), limbs.end());
    if (negative) {
        value = -value;
    }

    std::string text = BEMAN_BIG_INT_NAMESPACE::to_string(value, base);
    if (base == 16) {
        text.insert(text.front() == '-' ? 1 : 0, "0x");
    }
    return text;
}

// Stand-in for values too wide to convert synchronously: the extreme limbs and
// the limb count, which is enough to tell two such values apart.
std::string summarize(const std::uint32_t limb_count) {
    return "big_int of " + std::to_string(limb_count) + " limbs (too wide to render; expand [limbs])";
}

// Copies `text` into the debugger's buffer, which holds `maximum` characters
// including the terminator, widening it first when the debugger asked for
// UTF-16. Overlong values keep their most significant digits and end in "...".
void emit(const std::string& text, const BOOL unicode, char* const result, const std::size_t maximum) {
    if (result == nullptr || maximum == 0) {
        return;
    }

    const std::size_t capacity  = maximum - 1;
    const bool        truncated = text.size() > capacity;
    std::size_t       count     = truncated ? capacity : text.size();
    if (truncated && capacity >= 3) {
        count = capacity - 3;
    }

    std::size_t i   = 0;
    const auto  put = [&](const char c) {
        if (unicode) {
            reinterpret_cast<wchar_t*>(result)[i] = static_cast<wchar_t>(c);
        } else {
            result[i] = c;
        }
        ++i;
    };

    for (std::size_t j = 0; j != count; ++j) {
        put(text[j]);
    }
    if (truncated && capacity >= 3) {
        put('.');
        put('.');
        put('.');
    }
    put('\0');
}

template <int base>
HRESULT format_big_int(const DWORD        address,
                       DEBUGHELPER* const helper,
                       const BOOL         unicode,
                       char* const        result,
                       const std::size_t  maximum) {
    if (helper == nullptr || (result == nullptr && maximum != 0)) {
        return E_INVALIDARG;
    }

    try {
        std::vector<limb_type> limbs;
        bool                   negative   = false;
        std::uint32_t          limb_count = 0;

        if (!read_representation(helper, address, limbs, negative, limb_count)) {
            return E_FAIL;
        }

        const std::string text =
            limb_count > max_convertible_limbs ? summarize(limb_count) : render(limbs, negative, base);
        emit(text, unicode, result, maximum);
        return S_OK;
    } catch (...) {
        // Never let anything escape into the debugger.
        return E_FAIL;
    }
}

} // namespace

// The `Export` names in big_int_printer_msvc.natvis must match these exactly.
// x64 has a single calling convention, so __stdcall leaves them undecorated.
extern "C" {

__declspec(dllexport) HRESULT __stdcall formatter_big_int_dec(
    DWORD address, DEBUGHELPER* helper, int base, BOOL unicode, char* result, std::size_t maximum, DWORD reserved);

__declspec(dllexport) HRESULT __stdcall formatter_big_int_hex(
    DWORD address, DEBUGHELPER* helper, int base, BOOL unicode, char* result, std::size_t maximum, DWORD reserved);
}

// `base` and `reserved` are part of the fixed add-in signature: the debugger
// passes its own decimal/hex display setting in `base`, which the dedicated
// `,dec` and `,hex` views override, and `reserved` is always zero.
HRESULT __stdcall formatter_big_int_dec(const DWORD            address,
                                        DEBUGHELPER* const     helper,
                                        [[maybe_unused]] int   base,
                                        const BOOL             unicode,
                                        char* const            result,
                                        const std::size_t      maximum,
                                        [[maybe_unused]] DWORD reserved) {
    return format_big_int<10>(address, helper, unicode, result, maximum);
}

HRESULT __stdcall formatter_big_int_hex(const DWORD            address,
                                        DEBUGHELPER* const     helper,
                                        [[maybe_unused]] int   base,
                                        const BOOL             unicode,
                                        char* const            result,
                                        const std::size_t      maximum,
                                        [[maybe_unused]] DWORD reserved) {
    return format_big_int<16>(address, helper, unicode, result, maximum);
}

BOOL APIENTRY DllMain([[maybe_unused]] HMODULE hmodule,
                      [[maybe_unused]] DWORD   reason,
                      [[maybe_unused]] LPVOID  reserved) {
    return TRUE;
}
