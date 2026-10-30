#include <Windows.h>

// Minimal DLL entry point. LoadLibrary-based injection only needs this
// function to succeed; no game code or worker thread is started here.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(module);

	return TRUE;
}
