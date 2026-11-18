#pragma once
// Panorama (the CS:GO UI framework) access: the interface the game itself uses to
// run JavaScript inside the HUD, plus the two objects needed to reach it.
//
// Only the vfuncs this base uses are bound. The vtable indices are the part of this
// file that cannot be verified offline:
//
//   IPanoramaUIEngine::AccessUIEngine          11
//   UIEngine::IsValidPanelPointer              36
//   UIEngine::GetLastTargetPanel               56
//   UIEngine::RunScript                       113
//   IUIPanel::GetID                             9
//   IUIPanel::GetParent                        25
//
// They come from public CS:GO sources that call them against the 32-bit client
// (`panorama.dll`, interface "PanoramaUIEngine001"): the Aviarita lua-scripts FFI
// binding and the unknowncheats "GetLastDispatchedEventTargetPanel" thread. Hand
// written SDK headers (Fuzion/Skeletux) disagree by one or two slots, because Valve
// inserts vfuncs between builds, so every entry here is treated as a runtime risk:
// the owning feature validates what it can (module range, panel ids), guards each
// call with SEH and switches itself off instead of retrying.
#include <cstdint>
#include <windows.h>

namespace panorama {
class IUIPanel {
public:
    const char* GetID() {
        using Fn = const char* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[9])(this);
    }
    IUIPanel* GetParent() {
        using Fn = IUIPanel* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[25])(this);
    }
};

class UIEngine {
public:
    bool IsValidPanelPointer(IUIPanel* panel) {
        using Fn = bool (__thiscall*)(void*, IUIPanel*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[36])(this, panel);
    }
    IUIPanel* GetLastTargetPanel() {
        using Fn = IUIPanel* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[56])(this);
    }
};

class IPanoramaUIEngine {
public:
    UIEngine* AccessUIEngine() {
        using Fn = UIEngine* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[11])(this);
    }
};

// Resolves and validates RunScript once per session. A zero result means the slot is
// outside panorama.dll, or the interface was never captured.
void* ResolveRunScript(UIEngine* engine);

// Runs `js` in the context of `panel`. `path` only names the origin for Panorama's
// error messages.
//
// The argument count of RunScript differs between client builds (public sources show
// both six and seven parameters), so the call goes through an ESP-restoring thunk:
// the callee may pop fewer arguments than were pushed, and the thunk puts the stack
// back regardless. Exceptions raised by the game propagate to the caller's SEH.
void RunScript(UIEngine* engine, IUIPanel* panel, const char* js, const char* path);
} // namespace panorama
