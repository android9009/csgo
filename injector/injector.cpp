/* Standalone console auto-injector (Win32, pure C).
 *
 * Zero-argument flow:
 *   1. finds the built DLL   (csgo_release.dll next to the injector, in Bins\
 *      next to it, or in the current folder / Bins there)
 *   2. waits for the game    (csgo.exe, then cs2.exe) until it starts
 *   3. waits until the game is fully loaded (client.dll + engine.dll present)
 *   4. injects via VirtualAllocEx + remote LoadLibraryW
 *
 * LoadLibraryW is resolved the same way the proven csgo_base injector does:
 * the offset of the export inside the local kernel32.dll is applied to the
 * target's kernel32 base, which comes straight from a Toolhelp32 module
 * snapshot of the game (TH32CS_SNAPMODULE32, so it also works from x64 into
 * a 32-bit process). No PEB walking involved.
 *
 * Optional args:
 *   injector.exe [path\to\dll] [process name]
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdbool.h>

#define MAX_MODULES 512

static void GetExeDir(WCHAR* out, int cap)
{
	WCHAR buf[MAX_PATH];
	GetModuleFileNameW(NULL, buf, MAX_PATH);
	WCHAR* slash = wcsrchr(buf, L'\\');
	if (slash) {
		int n = (int)(slash - buf + 2);
		if (n >= cap) n = cap - 1;
		memcpy(out, buf, (size_t)n * sizeof(WCHAR));
		out[n] = 0;
	} else
		out[0] = 0;
}

static void PrintW(const WCHAR* w)
{
	char buf[1024];
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)sizeof(buf) - 1, NULL, NULL);
	if (n > 0)
		fwrite(buf, 1, (size_t)(n - 1), stdout);
}

/* returns pid or 0 */
static DWORD FindProcess(const WCHAR* name)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;

	DWORD pid = 0;
	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);
	if (Process32FirstW(snap, &pe)) {
		do {
			if (lstrcmpiW(pe.szExeFile, name) == 0) {
				pid = pe.th32ProcessID;
				break;
			}
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return pid;
}

/* tries candidate locations, writes the first existing one to out */
static bool ResolveDll(const WCHAR* argDll, WCHAR* out, int cap)
{
	if (argDll && *argDll) {
		if (GetFileAttributesW(argDll) != INVALID_FILE_ATTRIBUTES) {
			lstrcpynW(out, argDll, cap);
			return true;
		}
		WCHAR line[1100];
		swprintf(line, 1100, L"DLL not found: %s", argDll);
		PrintW(line);
		printf("\n");
		return false;
	}

	WCHAR exeDir[MAX_PATH];
	GetExeDir(exeDir, MAX_PATH);

	/* candidates, in order */
	WCHAR paths[4][MAX_PATH] = {0};
	wcsncat(paths[0], exeDir, MAX_PATH - 1);
	wcsncat(paths[0], L"csgo_release.dll", MAX_PATH - 1 - (int)wcslen(paths[0]));
	wcsncat(paths[1], exeDir, MAX_PATH - 1);
	wcsncat(paths[1], L"Bins\\csgo_release.dll", MAX_PATH - 1 - (int)wcslen(paths[1]));
	wcsncpy(paths[2], L"csgo_release.dll", MAX_PATH - 1);
	wcsncpy(paths[3], L"Bins\\csgo_release.dll", MAX_PATH - 1);

	for (int i = 0; i < 4; i++) {
		if (GetFileAttributesW(paths[i]) != INVALID_FILE_ATTRIBUTES) {
			lstrcpynW(out, paths[i], cap);
			return true;
		}
	}
	return false;
}

/* ---------------------------------------------- target module snapshot */

typedef struct {
	WCHAR name[64];
	DWORD64 base;
} Mod;

/* fills mods with the target's module list; returns count or 0 on error
 * (TH32CS_SNAPMODULE32 keeps 32-bit modules visible when called from x64) */
static int SnapshotModules(DWORD pid, Mod* mods, int maxMods)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;

	int count = 0;
	MODULEENTRY32W me;
	me.dwSize = sizeof(me);
	if (Module32FirstW(snap, &me)) {
		do {
			if (count < maxMods) {
				lstrcpynW(mods[count].name, me.szModule, 64);
				mods[count].base = (DWORD64)(uintptr_t)me.modBaseAddr;
				count++;
			}
		} while (Module32NextW(snap, &me));
	}
	CloseHandle(snap);
	return count;
}

static const Mod* FindModule(const Mod* mods, int count, const WCHAR* name)
{
	for (int i = 0; i < count; i++)
		if (lstrcmpiW(mods[i].name, name) == 0)
			return &mods[i];
	return NULL;
}

static bool ProcessAlive(HANDLE h)
{
	return WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
}

/* ---------------------------------------------------------------- inject */

int main(int argc, char** argv)
{
	SetConsoleOutputCP(CP_UTF8);

	WCHAR argDll[1024] = {0};
	if (argc > 1)
		MultiByteToWideChar(CP_ACP, 0, argv[1], -1, argDll, 1024);

	WCHAR argProc[64] = {0};
	if (argc > 2)
		MultiByteToWideChar(CP_ACP, 0, argv[2], -1, argProc, 64);

	WCHAR dll[MAX_PATH] = {0};
	if (!ResolveDll(argDll, dll, MAX_PATH)) {
		printf("csgo_release.dll not found.\n");
		printf("Put it next to injector.exe or in Bins\\ next to injector.exe.\n");
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}
	PrintW(L"  DLL: ");
	PrintW(dll);
	printf("\n");

	/* ------------------------------------------------- find the game */
	const WCHAR* targets[2] = { L"cs2.exe", L"csgo.exe" };
	const WCHAR* foundName = NULL;
	DWORD pid = 0;

	if (argProc[0]) {
		pid = FindProcess(argProc);
		foundName = argProc;
	} else {
		for (int i = 0; i < 2; i++) {
			pid = FindProcess(targets[i]);
			if (pid) {
				foundName = targets[i];
				break;
			}
		}
	}

	if (!pid) {
		printf("CS is not running, waiting up to 2 minutes for it to start...\n");
		for (int i = 0; i < 240 && !pid; i++) {
			Sleep(500);
			if (!argProc[0]) {
				for (int t = 0; t < 2; t++) {
					pid = FindProcess(targets[t]);
					if (pid) {
						foundName = targets[t];
						break;
					}
				}
			} else
				pid = FindProcess(argProc);
		}
	}

	if (!pid) {
		printf("Timed out waiting for the game process.\n");
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	{
		WCHAR line[128];
		swprintf(line, 128, L"Found %s (pid %lu)", foundName, (unsigned long)pid);
		PrintW(line);
		printf("\n");
	}

	HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
		PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
	if (!hProcess) {
		printf("OpenProcess failed (error %lu). Try running as administrator.\n", (unsigned long)GetLastError());
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	/* same-bitness check: the export-offset trick requires both processes
	 * to use the same kernel32 image (x86 injector -> x86 game) */
	BOOL selfWow = FALSE, gameWow = FALSE;
	IsWow64Process(GetCurrentProcess(), &selfWow);
	IsWow64Process(hProcess, &gameWow);
	if (selfWow != gameWow) {
		printf("Bitness mismatch: run the %s injector (this one is %s).\n",
			gameWow ? "x86" : "x64", selfWow ? "x86" : "x64");
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	/* ------------------------------------- wait until the game is ready */
	Mod mods[MAX_MODULES];
	int count = 0;
	bool already = false;
	bool ready = false;

	DWORD readyStart = GetTickCount();
	while (ProcessAlive(hProcess)) {
		count = SnapshotModules(pid, mods, MAX_MODULES);
		if (!count) {
			/* snapshot can fail transiently while the loader is active */
			Sleep(300);
			if (GetTickCount() - readyStart > 90000)
				break;
			continue;
		}

		if (FindModule(mods, count, L"csgo_release.dll") || FindModule(mods, count, L"csgo-release.dll")) {
			already = true;
			break;
		}
		if (FindModule(mods, count, L"client.dll") && FindModule(mods, count, L"engine.dll")) {
			ready = true;
			break;
		}
		Sleep(300);
		if (GetTickCount() - readyStart > 90000)
			break; /* proceed anyway, the game modules may differ */
	}

	if (already) {
		printf("The DLL is already injected into this process.\n");
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 0;
	}
	if (!ProcessAlive(hProcess)) {
		printf("The game process exited before injection.\n");
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}
	if (!ready)
		printf("Game core modules not detected, injecting anyway...\n");

	/* --------------------------- resolve remote LoadLibraryW by offset */
	FARPROC localLoad = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
	HMODULE localK32 = NULL;
	if (!localLoad ||
		!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)(uintptr_t)localLoad, &localK32)) {
		printf("Cannot resolve local LoadLibraryW.\n");
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	const Mod* remoteK32 = FindModule(mods, count, L"kernel32.dll");
	if (!remoteK32) {
		/* one more fresh snapshot, kernel32 must be there */
		count = SnapshotModules(pid, mods, MAX_MODULES);
		remoteK32 = FindModule(mods, count, L"kernel32.dll");
	}
	if (!remoteK32) {
		printf("kernel32.dll not found in the target module list.\n");
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	DWORD64 remoteLoad = remoteK32->base +
		((DWORD64)(uintptr_t)localLoad - (DWORD64)(uintptr_t)localK32);

	/* ---------------------------------------------------- do the inject */
	printf("Injecting csgo_release.dll...\n");

	SIZE_T pathBytes = (wcslen(dll) + 1) * sizeof(WCHAR);
	void* remote = VirtualAllocEx(hProcess, NULL, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) {
		printf("VirtualAllocEx failed (error %lu).\n", (unsigned long)GetLastError());
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	SIZE_T written = 0;
	if (!WriteProcessMemory(hProcess, remote, dll, pathBytes, &written) || written != pathBytes) {
		printf("WriteProcessMemory failed (error %lu).\n", (unsigned long)GetLastError());
		VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	HANDLE thread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)remoteLoad, remote, 0, NULL);
	if (!thread) {
		printf("CreateRemoteThread failed (error %lu).\n", (unsigned long)GetLastError());
		VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		printf("\nPress any key to exit...");
		getchar();
		return 1;
	}

	DWORD start = GetTickCount();
	while (WaitForSingleObject(thread, 200) == WAIT_TIMEOUT) {
		if (!ProcessAlive(hProcess))
			break;
		if (GetTickCount() - start > 30000)
			break;
	}

	DWORD exitCode = 0;
	GetExitCodeThread(thread, &exitCode);
	CloseHandle(thread);
	VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
	CloseHandle(hProcess);

	if (exitCode) {
		printf("OK, injected. csgo_release.dll loaded at 0x%llX\n", (unsigned long long)exitCode);
		printf("\nEnjoy.");
	} else {
		printf("FAILED: LoadLibraryW returned 0 (the DLL did not load).\n");
		printf("Possible reasons: the game is still starting, a mismatch between the DLL build and the game version,\n");
		printf("or an error inside the DLL's DllMain. Look at the game window / crash log for details.\n");
	}

	printf("\nPress any key to exit...");
	getchar();
	return exitCode ? 0 : 1;
}
