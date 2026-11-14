#include "hooks.hpp"

#include "MinHook.h"

namespace hooks {
bool detour(void* target, void* replacement, void** original) {
    if (!target || !replacement || !original)
        return false;
    const MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    if (MH_CreateHook(target, replacement, original) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        *original = nullptr;
        return false;
    }
    return true;
}

bool detour_virtual(void* object, std::size_t index, void* replacement, void** original) {
    if (!object)
        return false;
    void** table = *reinterpret_cast<void***>(object);
    return table && detour(table[index], replacement, original);
}
}
