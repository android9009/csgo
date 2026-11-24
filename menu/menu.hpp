#pragma once
#include <windows.h>
#include <d3d9.h>
namespace FatalityMenu {
bool Initialize(IDirect3DDevice9*, HWND);
void Render(IDirect3DDevice9*, HWND, bool);
bool Input(UINT, WPARAM, LPARAM, bool);
void BeforeReset(IDirect3DDevice9*);
void AfterReset(IDirect3DDevice9*, HRESULT);
}
