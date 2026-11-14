#include "hooks.hpp"
#include "../utils/detourhook.hpp"
#include <d3d9.h>
namespace HooksInternal {
long __stdcall EndScene(void* device) {
    auto original = Hooks::GetEndSceneOriginal();
    return original ? original(static_cast<IDirect3DDevice9*>(device)) : D3D_OK;
}
}
