#pragma once
#include <cstddef>
#include <cstdint>
#include <windows.h>
// Compat shim: the ported preview renderer owns one VMT hook (the studio
// skeleton observer). Minimal x86 slot patch with original restoration -
// same semantics as the old project's VMT_Hook, no backup vtable needed
// because only ever one slot per object is hooked here.
class VMT_Hook {
public:
    bool attach(void* object, std::size_t slot, void* fn) {
        if (!object || !fn) return false;
        auto** vt = *static_cast<void***>(object);
        if (hooked_ && vt_ == vt) return false;
        if (hooked_) detach();
        auto* entry = static_cast<void**>(vt + slot);
        auto* page = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(entry) & ~0xFFFu);
        DWORD old_prot = 0;
        if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READWRITE, &old_prot)) return false;
        vt_ = vt;
        slot_ = slot;
        original_ = *entry;
        *entry = fn;
        DWORD ignored = 0;
        VirtualProtect(page, 0x1000, old_prot, &ignored);
        hooked_ = true;
        return true;
    }
    template <class Fn> Fn get_original() const {
        return hooked_ ? static_cast<Fn>(original_) : nullptr;
    }
    bool is_hooked() const { return hooked_; }
    void detach() {
        if (!hooked_ || !vt_) return;
        auto* entry = static_cast<void**>(vt_ + slot_);
        auto* page = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(entry) & ~0xFFFu);
        DWORD old_prot = 0;
        if (VirtualProtect(page, 0x1000, PAGE_EXECUTE_READWRITE, &old_prot)) {
            *entry = original_;
            DWORD ignored = 0;
            VirtualProtect(page, 0x1000, old_prot, &ignored);
        }
        hooked_ = false;
        vt_ = nullptr;
        original_ = nullptr;
        slot_ = 0;
    }
    ~VMT_Hook() { detach(); }
    VMT_Hook() = default;
    VMT_Hook(const VMT_Hook&) = delete;
    VMT_Hook& operator=(const VMT_Hook&) = delete;

private:
    void** vt_ = nullptr;
    std::size_t slot_ = 0;
    void* original_ = nullptr;
    bool hooked_ = false;
};
