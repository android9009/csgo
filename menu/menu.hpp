#pragma once
#include <windows.h>
#include <d3d9.h>
namespace FatalityMenu {
bool Initialize(IDirect3DDevice9*, HWND);
void Render(IDirect3DDevice9*, HWND, bool);
bool Input(UINT, WPARAM, LPARAM, bool);
void BeforeReset(IDirect3DDevice9*);
void AfterReset(IDirect3DDevice9*, HRESULT);
// Which step of the last/ current Render() is live (see the phase table in
// menu.cpp). Read by the render-fault handler so csgo_base.log says where the
// menu render died instead of just "exception in EndScene".
int render_phase();
}
