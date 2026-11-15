#pragma once
#include <windows.h>
#include <d3d9.h>
#include "../sdk/studiohdr.hpp"
#include "../math/matrix.hpp"
struct CUserCmd;
namespace Hooks {
    bool Setup();
    void Restore();
    using CreateMoveFn = bool (__thiscall*)(void*, float, CUserCmd*);
    using PaintTraverseFn = void (__thiscall*)(void*, unsigned int, bool, bool);
    CreateMoveFn GetCreateMoveOriginal();
    PaintTraverseFn GetPaintTraverseOriginal();
    using EndSceneFn = long (__stdcall*)(IDirect3DDevice9*);
    EndSceneFn GetEndSceneOriginal();
    using FrameStageNotifyFn = void (__thiscall*)(void*, int);
    using SceneEndFn = void (__thiscall*)(void*);
    using DrawModelExecuteFn = void (__thiscall*)(void*, void*, const DrawModelState_t&, const ModelRenderInfo_t&, matrix3x4_t*);
    FrameStageNotifyFn GetFrameStageNotifyOriginal();
    SceneEndFn GetSceneEndOriginal();
    DrawModelExecuteFn GetDrawModelExecuteOriginal();
    WNDPROC GetOriginalWndProc();
}
namespace HooksInternal {
    bool __fastcall CreateMove(void* ecx, void* edx, float sample_time, CUserCmd* cmd);
    void __fastcall PaintTraverse(void* ecx, void* edx, unsigned int panel, bool force_repaint, bool allow_force);
    long __stdcall EndScene(void* device);
    void __fastcall FrameStageNotify(void* ecx, void* edx, int stage);
    void __fastcall SceneEnd(void* ecx, void* edx);
    void __fastcall DrawModelExecute(void* ecx, void* edx, void* context, const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone_to_world);
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
}
