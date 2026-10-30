#include "pch.h"
#include "skCrypter.h"

static DWORD WINAPI SkeetThread(LPVOID parameter) {
    const HMODULE base = static_cast<HMODULE>(parameter);
    InitializeCrashReporting(base);
#ifdef DLOG
    system("cls");
#endif
    skeet_t* skeet = skeet_t::getInstance(base);
    ReportCrashStage(skeet_crash::ImageConstructed);
    if (!skeet->map()) {
        ReportCrashStage(skeet_crash::MapFailed);
        std::cout << skCrypt("[INFO] failed to map the Skeet image\n");
        return 1;
    }
    ReportCrashStage(skeet_crash::MapSucceeded);

    while (GetModuleHandleA("serverbrowser.dll") == 0);
    if (!skeet->fix_imports()) {
        ReportCrashStage(skeet_crash::ImportsFailed);
        std::cout << skCrypt("[INFO] failed to resolve Skeet imports\n");
        return 1;
    }
    ReportCrashStage(skeet_crash::ImportsSucceeded);
    std::cout << skCrypt("[INFO] fixed!\n");

    if (!skeet->extra()) {
        // extra() publishes the precise failed preflight; do not overwrite it.
        std::cout << skCrypt("[INFO] Skeet patch preflight failed\n");
        return 1;
    }
    ReportCrashStage(skeet_crash::PatchesSucceeded);
    if (!skeet->entry()) {
        ReportCrashStage(skeet_crash::EntryFailed);
        std::cout << skCrypt("[INFO] failed to start the Skeet entry thread\n");
        return 1;
    }
    ReportCrashStage(skeet_crash::Ready);
    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
#ifdef DLOG
        AllocConsole();
        SetConsoleTitleA("SkeetCrack");
        freopen("CONOUT$", "w", stdout);
#endif
        DisableThreadLibraryCalls(hModule);
        CloseHandle(CreateThread(0, 0, SkeetThread, hModule, 0, 0));
        break;
    }
    return TRUE;
}
