#pragma once
// Panorama (the CS:GO UI framework) access: the interface the game itself uses to run
// JavaScript inside the HUD.
//
// Indices are kept to the absolute minimum, and every call that could be wrong is
// guarded by the caller with SEH plus a log line written *before* the call, so a crash
// always leaves the offending step in the log.
//
//   IPanoramaUIEngine::AccessUIEngine   11
//   UIEngine::GetLastTargetPanel        56
//   UIEngine::RunScript                113
//
// Deliberately NOT bound: IUIPanel::GetID / GetParent / SetID and
// UIEngine::IsValidPanelPointer. Public CS:GO sources disagree with each other by one
// or two slots exactly there, and the neighbours in that region are mutating calls
// (`SetID(uint64, const char*)`, `PanelDestroyed`); calling `SetID` through a
// "GetID" index writes a garbage string pointer into the panel and crashes the game
// later. The HUD panel is therefore identified from JavaScript (`panel.id`,
// `GetParent()`), which is engine-handled and cannot desync from the build.
//
// See docs/panorama.md for provenance and the remaining risk.
#include <cstdint>
#include <windows.h>

namespace panorama {
class UIEngine;

class IPanoramaUIEngine {
public:
    UIEngine* AccessUIEngine() {
        using Fn = UIEngine* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[11])(this);
    }
};

class UIEngine {
public:
    // The panel that received the last dispatched UI event. Used as a script context
    // only; it is never dereferenced on our side.
    void* GetLastTargetPanel() {
        using Fn = void* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[56])(this);
    }
};

// Resolves and validates RunScript once per session. Zero means the slot is outside
// panorama.dll (wrong index or wrong module) and the feature must stay off.
void* ResolveRunScript(UIEngine* engine);

// Runs `js` in the JS context that owns `panel`. `panel` may be any panel inside the
// HUD; the script finds what it needs by walking the tree.
//
// Public sources disagree on whether the CS:GO callee takes six or seven parameters, so
// the call goes through an ESP-restoring thunk: one extra argument is harmless and the
// stack is put back whatever the callee popped. Exceptions raised by the game propagate
// to the caller's SEH frame.
void RunScript(UIEngine* engine, void* panel, const char* js, const char* path);
} // namespace panorama
