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
    // `width` arrives in edx (fastcall), `height` on the stack - the original is __thiscall.
    float __fastcall GetScreenAspectRatio(void* ecx, void* edx, int height);
    void __fastcall FrameStageNotify(void* ecx, void* edx, int stage);
    void __fastcall SceneEnd(void* ecx, void* edx);
    void __fastcall DrawModelExecute(void* ecx, void* edx, void* context, const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone_to_world);
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
}
