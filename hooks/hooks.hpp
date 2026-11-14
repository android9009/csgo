#pragma once
#include <windows.h>
#include <d3d9.h>
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
    WNDPROC GetOriginalWndProc();
}
namespace HooksInternal {
    bool __fastcall CreateMove(void* ecx, void* edx, float sample_time, CUserCmd* cmd);
    void __fastcall PaintTraverse(void* ecx, void* edx, unsigned int panel, bool force_repaint, bool allow_force);
    long __stdcall EndScene(void* device);
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
}
