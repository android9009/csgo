#pragma once
// PE memory helpers + IDA-pattern scanner (Havoc utils/memory.hpp approach).
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string_view>
namespace MEM {
    std::uintptr_t FindPattern(std::string_view module, std::string_view pattern);
    std::uintptr_t FindPattern(const std::uint8_t* start, std::uintptr_t size, std::string_view pattern);
    void* GetModuleBase(std::string_view module);
    void* GetExport(void* module_base, std::string_view name);
    std::uintptr_t GetAbsolute(std::uintptr_t rel_addr);
    std::uintptr_t ResolveRelative(std::uintptr_t addr, std::uintptr_t offset = 0);
    bool GetSection(std::uintptr_t base, std::string_view section,
                    std::uintptr_t* out_start, std::uintptr_t* out_size);
    bool ReadPointer(std::uintptr_t address, std::uintptr_t& value);
    template <typename Fn> Fn GetVFunc(void* instance, size_t index) {
        if (!instance) return nullptr;
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(instance))[index]);
    }
    template <typename Fn> Fn GetVFunc(std::uintptr_t instance, size_t index) {
        return GetVFunc<Fn>(reinterpret_cast<void*>(instance), index);
    }
    class Protect {
    public:
        Protect(void* address, size_t length, DWORD protection) : base(address), len(length), old(0) {
            if (base && len) VirtualProtect(base, len, protection, &old);
        }
        ~Protect() { if (base && len) { DWORD ignored; VirtualProtect(base, len, old, &ignored); } }
        Protect(const Protect&) = delete; Protect& operator=(const Protect&) = delete;
    private: void* base; size_t len; DWORD old;
    };
}
