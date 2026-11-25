#pragma once
#include <windows.h>
#include <d3d9.h>
namespace FatalityMenu {
bool Initialize(IDirect3DDevice9*, HWND);
void Render(IDirect3DDevice9*, HWND, bool);
bool Input(UINT, WPARAM, LPARAM, bool);
void BeforeReset(IDirect3DDevice9*);
void AfterReset(IDirect3DDevice9*, HRESULT);

// Exception-handling boundary. The game hooks are compiled with EH disabled, so
// any C++ exception that would unwind into them is undefined behaviour. The
// wrappers below (compiled with /EHsc) swallow and log it instead, so a menu
// failure can never take the game frame down.
void RenderSafe(IDirect3DDevice9*, HWND, bool);
bool InputSafe(UINT, WPARAM, LPARAM, bool);
void BeforeResetSafe(IDirect3DDevice9*);
void AfterResetSafe(IDirect3DDevice9*, HRESULT);
}
