#pragma once
#include <windows.h>
#include <d3d9.h>
#include "../studiohdr.hpp"
#include "../../utils/math/matrix.hpp"
class CUserCmd;
namespace Hooks {
    bool Setup();
    void Restore();
    using PaintTraverseFn = void (__thiscall*)(void*, unsigned int, bool, bool);
    PaintTraverseFn GetPaintTraverseOriginal();
    using EndSceneFn = long (__stdcall*)(IDirect3DDevice9*);
    EndSceneFn GetEndSceneOriginal();
    EndSceneFn GetEndSceneOriginalFor(void* device);
    using ResetFn = long (__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    ResetFn GetResetOriginalFor(void* device);
    using CreateMoveFn = bool (__fastcall*)(void*, void*, float, void*);
    using ViewModelFovFn = float (__thiscall*)(void*);
    // IEngineClient slot 101: `float GetScreenAspectRatio(int width, int height)`.
    using ScreenAspectRatioFn = float (__thiscall*)(void*, int, int);
    using FrameStageNotifyFn = void (__thiscall*)(void*, int);
    using SceneEndFn = void (__thiscall*)(void*);
    using DrawModelExecuteFn = void (__thiscall*)(void*, void*, const DrawModelState_t&, const ModelRenderInfo_t&, matrix3x4_t*);
    CreateMoveFn GetCreateMoveOriginal();
    FrameStageNotifyFn GetFrameStageNotifyOriginal();
    ViewModelFovFn GetViewModelFovOriginal();
    ScreenAspectRatioFn GetScreenAspectRatioOriginal();
    SceneEndFn GetSceneEndOriginal();
    DrawModelExecuteFn GetDrawModelExecuteOriginal();
    WNDPROC GetOriginalWndProc();
}
namespace HooksInternal {
    void __fastcall PaintTraverse(void* ecx, void* edx, unsigned int panel, bool force_repaint, bool allow_force);
    long __stdcall EndScene(void* device);
    long __stdcall Reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    bool __fastcall CreateMove(void* ecx, void* edx, float input_sample_time, void* cmd);
    float __fastcall GetViewModelFov(void* ecx, void* edx);
    // The original is __thiscall: `this` in ecx, `width` and `height` on the stack,
    // callee pops 8 bytes (`ret 8`). A __fastcall thunk therefore has to declare BOTH stack
    // arguments so that it pops exactly those 8 bytes - declaring only the first one (what
    // this hook used to do) pops 4 and the caller returns into a shifted stack, which crashed
    // the game on the first rendered frame. ecx carries `this`; edx is unused by a thiscall
    // callee and is ignored here.
    float __fastcall GetScreenAspectRatio(void* ecx, void* edx, int width, int height);
    void __fastcall FrameStageNotify(void* ecx, void* edx, int stage);
    void __fastcall SceneEnd(void* ecx, void* edx);
    void __fastcall DrawModelExecute(void* ecx, void* edx, void* context, const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone_to_world);
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    // `void __thiscall(C_CSPlayer* this)` - the epilogue in the supplied client.dll is a plain
    // `ret` (no stack arguments), so the __fastcall thunk pops nothing and the stack stays
    // balanced.
    void __fastcall UpdatePostScreenEffects(void* ecx, void* edx);
    // `void __thiscall(C_BaseViewModel* this, C_BasePlayer* owner, Vector& eye_pos,
    // QAngle& eye_angles)` - three stack arguments, i.e. the callee pops 12 bytes and the
    // thunk therefore declares all three.
    void __fastcall CalcViewModelView(void* ecx, void* edx, void* owner, float* eye_pos,
                                     float* eye_angles);
    // MinHook detours on the two client.dll functions the visuals-misc block needs:
    // CalcViewModelView (the viewmodel changer's rotation) and UpdatePostScreenEffects
    // ("Remove scope" dropping the engine's own scope mask). Both signatures come from the
    // runtime catalogue (Patterns::Get); a signature that does not resolve leaves the feature
    // off with a log line instead of a guessed address. Installed from Hooks::Initialize.
    void InstallVisualMiscDetours();
    void RemoveVisualMiscDetours();
    // Visuals -> World: the IMaterial::GetColorModulation detour (Nightmode / Sky tints) and the
    // weapon_debug_spread_show GetInt detour ("Force crosshair"). Both are resolved at runtime.
    void InstallWorldDetours();
    void RemoveWorldDetours();
}
