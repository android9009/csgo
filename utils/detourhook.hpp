#pragma once
// Dependency-free VMT hook plus optional MinHook adapter.
#include <windows.h>
#include <cstdint>
#include <cstddef>
class VMT_Hook {
public:
    VMT_Hook() = default;
    ~VMT_Hook() { detach(); }
    bool attach(void* instance, std::size_t index, void* replacement) {
        if (!instance || !replacement || hooked) return false;
        target = instance; slot = index;
        table = *reinterpret_cast<void***>(instance);
        original = table[index];
        DWORD old = 0;
        if (!VirtualProtect(&table[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
        table[index] = replacement;
        DWORD ignored = 0; VirtualProtect(&table[index], sizeof(void*), old, &ignored);
        hooked = true;
        return true;
    }
    void detach() {
        if (!hooked || !table) return;
        DWORD old = 0;
        if (VirtualProtect(&table[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            table[slot] = original;
            DWORD ignored = 0; VirtualProtect(&table[slot], sizeof(void*), old, &ignored);
        }
        target = nullptr; table = nullptr; original = nullptr; hooked = false;
    }
    template <typename Fn> Fn get_original() const { return reinterpret_cast<Fn>(original); }
    bool is_hooked() const { return hooked; }
private:
    void* target = nullptr;
    void** table = nullptr;
    void* original = nullptr;
    std::size_t slot = 0;
    bool hooked = false;
};
class Detour_Hook {
public:
    bool attach(void*, void*, void**) { return false; } // Optional MinHook integration can be added here.
    void detach(void*) {}
};
