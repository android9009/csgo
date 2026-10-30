#include <Windows.h>

#include "runtime/runtime.hpp"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        return app_runtime::start() ? TRUE : FALSE;
    }
    return TRUE;
}
