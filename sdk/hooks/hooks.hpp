#pragma once
#include <windows.h>
#include <d3d9.h>
#include "../studiohdr.hpp"
#include "../../utils/math/matrix.hpp"
struct CUserCmd;
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
    using OverrideViewFn = void (__thiscall*)(void*, void*);
    using ViewModelFovFn = float (__thiscall*)(void*);
    using FrameStageNotifyFn = void (__thiscall*)(void*, int);
    using SceneEndFn = void (__thiscall*)(void*);
    using DrawModelExecuteFn = void (__thiscall*)(void*, void*, const DrawModelState_t&, const ModelRenderInfo_t&, matrix3x4_t*);
    FrameStageNotifyFn GetFrameStageNotifyOriginal();
    OverrideViewFn GetOverrideViewOriginal();
    ViewModelFovFn GetViewModelFovOriginal();
    SceneEndFn GetSceneEndOriginal();
    DrawModelExecuteFn GetDrawModelExecuteOriginal();
    WNDPROC GetOriginalWndProc();
}
namespace HooksInternal {
    void __fastcall PaintTraverse(void* ecx, void* edx, unsigned int panel, bool force_repaint, bool allow_force);
    long __stdcall EndScene(void* device);
    long __stdcall Reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    bool __fastcall CreateMove(void* ecx, void* edx, float input_sample_time, void* cmd);
    void __fastcall OverrideView(void* ecx, void* edx, void* setup);
    float __fastcall GetViewModelFov(void* ecx, void* edx);
    void __fastcall FrameStageNotify(void* ecx, void* edx, int stage);
    void __fastcall SceneEnd(void* ecx, void* edx);
    void __fastcall DrawModelExecute(void* ecx, void* edx, void* context, const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone_to_world);
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
}
