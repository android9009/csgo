#pragma once
#include <cstddef>

// Detours for game functions (MinHook). One helper for every feature, the
// counterpart of main's hooker::add_detour.
namespace hooks {
// Detours `target`; `*original` receives the trampoline.
bool detour(void* target, void* replacement, void** original);
// Detours the function behind a vtable slot. The vtable itself is not modified.
bool detour_virtual(void* object, std::size_t index, void* replacement, void** original);
}
