#pragma once
#include <cstdint>
#include <cstddef>
#include <windows.h>
// Compat shim: old project memory helpers used by the ported menu code.
// Plain Win32 - identical behavior, no extra allocator state.
namespace MEM {
inline void* GetModuleBase(const char* module) {
    return reinterpret_cast<void*>(GetModuleHandleA(module));
}
// Byte pattern with "?" wildcards (e.g. "8D 44 24 28 B9 ? ? ? ? 50").
inline void* FindPattern(const char* module, const char* pattern) {
    const auto base = reinterpret_cast<uint8_t*>(GetModuleHandleA(module));
    if (!base) return nullptr;
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto* section = reinterpret_cast<IMAGE_SECTION_HEADER*>(
        reinterpret_cast<uint8_t*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader);
    size_t need = 0;
    for (size_t j = 0; pattern[j]; ++j)
        if (pattern[j] != ' ') { ++j; /* skip second hex digit or '?' */ ++need; }
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* begin = base + section[i].VirtualAddress;
        const size_t length = section[i].Misc.VirtualSize;
        if (need > length) continue;
        for (size_t offset = 0; offset + need <= length; ++offset) {
            size_t p = 0;
            bool hit = true;
            size_t byte_index = 0;
            while (hit && pattern[p]) {
                while (pattern[p] == ' ') ++p;
                if (!pattern[p]) break;
                if (pattern[p] != '?') {
                    const int upper = (pattern[p] <= '9') ? pattern[p] - '0' : pattern[p] - 'A' + 10;
                    const int lower = (pattern[p + 1] <= '9') ? pattern[p + 1] - '0' : pattern[p + 1] - 'A' + 10;
                    if (begin[offset + byte_index] != static_cast<uint8_t>(upper * 16 + lower))
                        hit = false;
                    p += 2;
                } else {
                    p += 2;
                }
                ++byte_index;
            }
            if (hit) return begin + offset;
        }
    }
    return nullptr;
}
inline uintptr_t GetPattern(const char* module, const char* pattern) {
    return reinterpret_cast<uintptr_t>(FindPattern(module, pattern));
}
template <class T>
inline T GetVFunc(void* object, std::size_t slot) {
    return reinterpret_cast<T>(*reinterpret_cast<void***>(object)[slot]);
}
}
