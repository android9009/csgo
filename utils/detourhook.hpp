#pragma once
// VMT hook for interface slots + MinHook-based inline detour for arbitrary functions.
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <MinHook.h>

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

// Inline detour on a raw function address (engine internals without a vtable slot).
class Detour_Hook {
public:
    static bool ensure_init() {
        static const bool ok = [] {
            const MH_STATUS status = MH_Initialize();
            return status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED;
        }();
        return ok;
    }
    bool attach(void* target, void* detour, void** original) {
        if (!target || !detour || hooked || !ensure_init()) return false;
        if (MH_CreateHook(target, detour, original) != MH_OK) return false;
        if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
        this->target = target;
        hooked = true;
        return true;
    }
    void detach() {
        if (!hooked || !target) return;
        MH_DisableHook(target);
        MH_RemoveHook(target);
        target = nullptr; hooked = false;
    }
    template <typename Fn> Fn get_original(void** trampoline) const {
        return trampoline ? reinterpret_cast<Fn>(*trampoline) : nullptr;
    }
    bool is_hooked() const { return hooked; }
private:
    void* target = nullptr;
    bool hooked = false;
};
