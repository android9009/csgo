/* Standalone console auto-injector (Win32, pure C).
 *
 * What it does, by itself, with zero arguments:
 *   1. finds the freshly built DLL  (csgo-release.dll next to the injector,
 *      in Bins/ next to the injector, or in the current folder / Bins there)
 *   2. waits for the game process to appear (cs2.exe, then csgo.exe)
 *   3. injects the DLL via VirtualAllocEx + remote LoadLibraryW
 *
 * Works from x64 into both x86 and x64 targets: the target's kernel32 base
 * is walked out of its PEB and LoadLibraryW is resolved from the target's
 * own PE export table, so no cross-bitness GetModuleHandle guessing.
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

#define INJECTOR_OK    0
#define INJECTOR_ERR   1

static void PrintW(const WCHAR* w);

/* ---------------------------------------------------------------- helpers */

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

/* tries candidate locations, writes the first existing one to out; true on success */
static bool ResolveDll(const WCHAR* argDll, WCHAR* out, int cap)
{
	if (argDll && *argDll) {
		if (GetFileAttributesW(argDll) != INVALID_FILE_ATTRIBUTES) {
			lstrcpynW(out, argDll, cap);
			return true;
		}
		{
			WCHAR line[1100];
			swprintf(line, 1100, L"DLL not found: %s", argDll);
			PrintW(line);
			printf("\n");
		}
		return false;
	}

	WCHAR exeDir[MAX_PATH];
	GetExeDir(exeDir, MAX_PATH);

	/* candidates, in order:
	 *   0: next to the injector
	 *   1: in Bins\ next to the injector (local build layout)
	 *   2: current folder
	 *   3: Bins\ in current folder (CI artifact layout)
	 */
	WCHAR paths[4][MAX_PATH] = {0};
	wcsncat(paths[0], exeDir, MAX_PATH - 1);
	wcsncat(paths[0], L"csgo-release.dll", MAX_PATH - 1 - (int)wcslen(paths[0]));
	wcsncat(paths[1], exeDir, MAX_PATH - 1);
	wcsncat(paths[1], L"Bins\\csgo-release.dll", MAX_PATH - 1 - (int)wcslen(paths[1]));
	wcsncpy(paths[2], L"csgo-release.dll", MAX_PATH - 1);
	wcsncpy(paths[3], L"Bins\\csgo-release.dll", MAX_PATH - 1);

	for (int i = 0; i < 4; i++) {
		if (GetFileAttributesW(paths[i]) != INVALID_FILE_ATTRIBUTES) {
			lstrcpynW(out, paths[i], cap);
			return true;
		}
	}
	return false;
}

/* ------------------------------------------- remote PE / PEB reading (C) */

static bool ReadRemote(HANDLE h, DWORD64 addr, void* out, SIZE_T size)
{
	SIZE_T got = 0;
	return ReadProcessMemory(h, (LPCVOID)addr, out, size, &got) && got == size;
}

/* reads the target PE header + walks the PEB to find kernel32.dll base */
static bool GetTargetInfo(HANDLE hProcess, bool* is64Out, DWORD64* kernel32BaseOut)
{
	IMAGE_DOS_HEADER dos;
	if (!ReadRemote(hProcess, 0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	IMAGE_NT_HEADERS32 nt;
	if (!ReadRemote(hProcess, dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE)
		return false;

	bool is64 = (nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 || nt.FileHeader.Machine == IMAGE_FILE_MACHINE_IA64);
	*is64Out = is64;

	/* PEB / LDR_DATA_TABLE_ENTRY layouts differ between x64 and x86 targets */
	DWORD64 pebAddr      = is64 ? 0x001FF7000ull : 0x7FFDF000ull;
	DWORD   pebLdrOff    = is64 ? 0x18 : 0x0C;
	DWORD   ldrListOff   = is64 ? 0x10 : 0x0C;
	DWORD   entDllBase   = is64 ? 0x30 : 0x18;
	DWORD   entNameLen   = is64 ? 0x58 : 0x2C;
	DWORD   entNamePtr   = is64 ? 0x60 : 0x30;
	DWORD   ptrSize      = is64 ? 8 : 4;

	DWORD64 ldrAddr;
	if (!ReadRemote(hProcess, pebAddr + pebLdrOff, &ldrAddr, ptrSize) || !ldrAddr)
		return false;

	DWORD64 listHead;
	if (!ReadRemote(hProcess, ldrAddr + ldrListOff, &listHead, ptrSize) || !listHead)
		return false;

	for (int i = 0; i < 1024; i++) {
		DWORD64 entry, base, namePtr;
		WORD    nameLen;

		if (!ReadRemote(hProcess, listHead, &entry, ptrSize) || !entry)
			break;

		if (!ReadRemote(hProcess, entry + entDllBase, &base, ptrSize))
			break;
		if (!ReadRemote(hProcess, entry + entNameLen, &nameLen, 2))
			break;
		if (!ReadRemote(hProcess, entry + entNamePtr, &namePtr, ptrSize))
			break;
		if (!nameLen || nameLen > 512 || (nameLen & 1))
			break;

		WCHAR name[260] = {0};
		if (ReadRemote(hProcess, namePtr, name, nameLen) && _wcsicmp(name, L"kernel32.dll") == 0) {
			*kernel32BaseOut = base;
			return true;
		}
		listHead = entry; /* next entry */
	}
	return false;
}

/* resolves an export by name from a remote module (cross-bitness safe) */
static bool GetRemoteExport(HANDLE hProcess, DWORD64 moduleBase, const WCHAR* name, DWORD64* outAddr)
{
	IMAGE_DOS_HEADER dos;
	if (!ReadRemote(hProcess, moduleBase, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	IMAGE_NT_HEADERS32 nt;
	if (!ReadRemote(hProcess, moduleBase + dos.e_lfanew, &nt, sizeof(nt)))
		return false;

	bool is64 = (nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
	int  dirOff = is64 ? 112 : 96; /* export data directory from e_lfanew */

	IMAGE_DATA_DIRECTORY expDir;
	if (!ReadRemote(hProcess, moduleBase + dos.e_lfanew + dirOff, &expDir, sizeof(expDir)) ||
		!expDir.VirtualAddress)
		return false;

	IMAGE_EXPORT_DIRECTORY exp;
	if (!ReadRemote(hProcess, moduleBase + expDir.VirtualAddress, &exp, sizeof(exp)))
		return false;
	if (exp.NumberOfNames == 0 || exp.NumberOfNames > 100000 || exp.NumberOfFunctions == 0)
		return false;

	DWORD* nameRvas   = (DWORD*)LocalAlloc(LPTR, exp.NumberOfNames * sizeof(DWORD));
	WORD*  ordinals   = (WORD*)LocalAlloc(LPTR, exp.NumberOfNames * sizeof(WORD));
	DWORD* funcRvas   = (DWORD*)LocalAlloc(LPTR, exp.NumberOfFunctions * sizeof(DWORD));
	if (!nameRvas || !ordinals || !funcRvas) {
		LocalFree(nameRvas); LocalFree(ordinals); LocalFree(funcRvas);
		return false;
	}

	bool found = false;
	if (ReadRemote(hProcess, moduleBase + exp.AddressOfNames, nameRvas, exp.NumberOfNames * sizeof(DWORD)) &&
		ReadRemote(hProcess, moduleBase + exp.AddressOfNameOrdinals, ordinals, exp.NumberOfNames * sizeof(WORD)) &&
		ReadRemote(hProcess, moduleBase + exp.AddressOfFunctions, funcRvas, exp.NumberOfFunctions * sizeof(DWORD))) {
		for (DWORD i = 0; i < exp.NumberOfNames; i++) {
			WCHAR nm[64] = {0};
			if (!ReadRemote(hProcess, moduleBase + nameRvas[i], nm, sizeof(nm)))
				continue;
			if (_wcsicmp(nm, name) == 0) {
				if (ordinals[i] < exp.NumberOfFunctions) {
					*outAddr = moduleBase + funcRvas[ordinals[i]];
					found = true;
				}
				break;
			}
		}
	}

	LocalFree(nameRvas); LocalFree(ordinals); LocalFree(funcRvas);
	return found;
}

/* ---------------------------------------------------------------- inject */

static bool DoInject(DWORD pid, const WCHAR* dllPath, DWORD* errOut, DWORD64* loadedAtOut)
{
	*errOut = 0;

	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (!hProcess) {
		*errOut = GetLastError();
		return false;
	}

	bool is64 = false;
	DWORD64 kernel32 = 0;
	if (!GetTargetInfo(hProcess, &is64, &kernel32)) {
		CloseHandle(hProcess);
		*errOut = 2;
		return false;
	}

	DWORD64 loadLib = 0;
	if (!GetRemoteExport(hProcess, kernel32, L"LoadLibraryW", &loadLib)) {
		CloseHandle(hProcess);
		*errOut = 3;
		return false;
	}

	SIZE_T pathBytes = (wcslen(dllPath) + 1) * sizeof(WCHAR);
	void* remote = VirtualAllocEx(hProcess, NULL, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) {
		CloseHandle(hProcess);
		*errOut = 4;
		return false;
	}

	if (!WriteProcessMemory(hProcess, remote, dllPath, pathBytes, NULL)) {
		VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		*errOut = 5;
		return false;
	}

	HANDLE thread = CreateRemoteThread(hProcess, NULL, 0,
		(LPTHREAD_START_ROUTINE)loadLib, remote, 0, NULL);
	if (!thread) {
		DWORD err = GetLastError();
		VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		*errOut = err;
		return false;
	}

	WaitForSingleObject(thread, 15000);
	DWORD exitCode = 0;
	GetExitCodeThread(thread, &exitCode);
	*loadedAtOut = (DWORD64)exitCode; /* LoadLibraryW returns the module base */
	CloseHandle(thread);

	VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
	CloseHandle(hProcess);

	return exitCode != 0;
}

/* ------------------------------------------------------------------ main */

static void PrintW(const WCHAR* w)
{
	char buf[1024];
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)sizeof(buf) - 1, NULL, NULL);
	if (n > 0)
		fwrite(buf, 1, (size_t)(n - 1), stdout);
}

int main(int argc, char** argv)
{
	SetConsoleOutputCP(CP_UTF8);

	WCHAR argDll[1024] = {0};
	if (argc > 1)
		MultiByteToWideChar(CP_ACP, 0, argv[1], -1, argDll, 1024);

	WCHAR dll[MAX_PATH] = {0};
	if (!ResolveDll(argDll, dll, MAX_PATH)) {
		printf("csgo-release.dll not found.\n");
		printf("Put it next to injector.exe or in Bins\\ next to injector.exe.\n");
		printf("\nPress any key to exit...");
		getchar();
		return INJECTOR_ERR;
	}
	PrintW(L"  DLL: ");
	PrintW(dll);
	printf("\n");

	const WCHAR* targets[2] = { L"cs2.exe", L"csgo.exe" };
	const WCHAR* foundName = NULL;
	DWORD pid = 0;

	for (int i = 0; i < 2; i++) {
		pid = FindProcess(targets[i]);
		if (pid) {
			foundName = targets[i];
			break;
		}
	}

	if (!pid) {
		printf("CS not running, waiting for it to start...\n");
		for (int i = 0; i < 240 && !pid; i++) { /* up to 2 minutes */
			Sleep(500);
			for (int t = 0; t < 2; t++) {
				pid = FindProcess(targets[t]);
				if (pid) {
					foundName = targets[t];
					break;
				}
			}
		}
	}

	if (!pid) {
		printf("Timed out waiting for the game process.\n");
		printf("\nPress any key to exit...");
		getchar();
		return INJECTOR_ERR;
	}
	{
		WCHAR line[128];
		swprintf(line, 128, L"Found %s (pid %lu)", foundName, (unsigned long)pid);
		PrintW(line);
		printf("\n");
	}

	printf("Injecting...\n");

	DWORD err = 0;
	DWORD64 base = 0;
	if (DoInject(pid, dll, &err, &base)) {
		printf("OK, injected. Loaded at 0x%llX\n", (unsigned long long)base);
		printf("\nEnjoy.");
	} else {
		printf("FAILED (error %lu).", (unsigned long)err);
		printf("\nTry running as administrator or check that the game is not under debugger.\n");
	}

	printf("\nPress any key to exit...");
	getchar();
	return 0;
}
