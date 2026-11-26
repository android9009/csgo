/* Standalone GUI injector (Win32, no console).
 *
 * Picks a process from a list, injects a DLL (default: csgo-release.dll next
 * to the exe) via VirtualAllocEx + remote LoadLibraryW. Works from x64 into
 * both x86 and x64 targets: the target's kernel32 base is walked out of its
 * PEB and LoadLibraryW is resolved from the target's own PE export table, so
 * no cross-bitness GetModuleHandle guessing is involved.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <string.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <wchar.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

enum {
	IDC_PROCESS = 1001,
	IDC_REFRESH = 1002,
	IDC_DLL     = 1003,
	IDC_BROWSE  = 1004,
	IDC_INJECT  = 1005,
	IDC_STATUS  = 1006,
};

static HWND  g_hProcess, g_hRefresh, g_hDll, g_hBrowse, g_hInject, g_hStatus;
static bool  g_statusOk = true;
static WCHAR g_exeDir[MAX_PATH];

/* ---------------------------------------------------------------- helpers */

static void SetStatus(HWND h, const WCHAR* text, bool ok)
{
	g_statusOk = ok;
	SetWindowTextW(h, text);
	InvalidateRect(h, NULL, TRUE);
}

static void GetExeDir(void)
{
	WCHAR buf[MAX_PATH];
	GetModuleFileNameW(NULL, buf, MAX_PATH);
	WCHAR* slash = wcsrchr(buf, L'\\');
	if (slash)
		lstrcpynW(g_exeDir, buf, (int)(slash - buf + 2));
}

static int CmpProc(const PROCESSENTRY32W* a, const PROCESSENTRY32W* b)
{
	return lstrcmpiW(a->szExeFile, b->szExeFile);
}

static void RefreshProcesses(HWND hCombo, const WCHAR* lastPick)
{
	SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32W* list = (PROCESSENTRY32W*)LocalAlloc(LPTR, sizeof(PROCESSENTRY32W) * 4096);
	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);
	int count = 0;
	if (Process32FirstW(snap, &pe)) {
		do {
			if (count < 4096)
				list[count++] = pe;
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);

	/* simple insertion sort - process lists are small */
	for (int i = 1; i < count; i++) {
		PROCESSENTRY32W key = list[i];
		int j = i - 1;
		while (j >= 0 && CmpProc(&list[j], &key) > 0) {
			list[j + 1] = list[j];
			j--;
		}
		list[j + 1] = key;
	}

	int best = -1;
	for (int i = 0; i < count; i++) {
		int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)list[i].szExeFile);
		if (lastPick && *lastPick && lstrcmpiW(list[i].szExeFile, lastPick) == 0)
			SendMessageW(hCombo, CB_SETCURSEL, idx, 0);
		if (best < 0 &&
			(lstrcmpiW(list[i].szExeFile, L"cs2.exe") == 0 ||
			 lstrcmpiW(list[i].szExeFile, L"csgo.exe") == 0))
			best = i;
	}
	if (best >= 0)
		SendMessageW(hCombo, CB_SETCURSEL, best, 0);

	LocalFree(list);
}

static DWORD GetSelectedPid(HWND hCombo)
{
	int idx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
	if (idx < 0)
		return 0;

	WCHAR name[1024] = {0};
	SendMessageW(hCombo, CB_GETLBTEXT, idx, (LPARAM)name);

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	DWORD pid = 0;
	if (snap != INVALID_HANDLE_VALUE) {
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
	}
	return pid;
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

		if (!ReadRemote(hProcess, listHead, &entry, ptrSize) || !entry || entry == ldrAddr)
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
		listHead = entry; /* follow InLoadOrder Flink */
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

static bool DoInject(DWORD pid, const WCHAR* dllPath, DWORD* errOut, WCHAR* outBuf, int outBufLen)
{
	*errOut = 0;

	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (!hProcess) {
		*errOut = GetLastError();
		return false;
	}

	bool is64 = false;
	DWORD64 kernel32Base = 0;
	if (!GetTargetInfo(hProcess, &is64, &kernel32Base)) {
		CloseHandle(hProcess);
		*errOut = ERROR_READ_FAULT;
		return false;
	}

	DWORD64 loadLibraryW = 0;
	if (!GetRemoteExport(hProcess, kernel32Base, L"LoadLibraryW", &loadLibraryW)) {
		CloseHandle(hProcess);
		*errOut = 7; /* ERROR_BAD_MODULE */
		return false;
	}

	size_t bytes = (wcslen(dllPath) + 1) * sizeof(WCHAR);
	LPVOID remote = VirtualAllocEx(hProcess, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) {
		CloseHandle(hProcess);
		*errOut = GetLastError();
		return false;
	}

	if (!WriteProcessMemory(hProcess, remote, dllPath, bytes, NULL)) {
		VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		*errOut = GetLastError();
		return false;
	}

	DWORD threadId = 0;
	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
		(LPTHREAD_START_ROUTINE)loadLibraryW, remote, 0, &threadId);
	if (!hThread) {
		*errOut = GetLastError();
		VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	bool ok = false;
	if (WaitForSingleObject(hThread, 30000) == WAIT_OBJECT_0) {
		DWORD exitCode = 0;
		GetExitCodeThread(hThread, &exitCode);
		if (exitCode) {
			int n = swprintf(outBuf, outBufLen, L"0x%llX", (unsigned long long)exitCode);
			if (n < 0) n = 0;
			ok = true;
		} else {
			*errOut = GetLastError();
		}
	} else {
		*errOut = ERROR_TIMEOUT;
		TerminateThread(hThread, 0);
	}

	CloseHandle(hThread);
	VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
	CloseHandle(hProcess);
	return ok;
}

/* ----------------------------------------------------------------- gui */

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		g_hProcess = CreateWindowExW(0, L"COMBOBOX", L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
			16, 66, 430, 260, hWnd, (HMENU)IDC_PROCESS, NULL, NULL);
		g_hRefresh = CreateWindowExW(0, L"BUTTON", L"Refresh",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP, 452, 63, 76, 26, hWnd, (HMENU)IDC_REFRESH, NULL, NULL);

		CreateWindowExW(0, L"STATIC", L"DLL:", WS_CHILD | WS_VISIBLE, 16, 106, 40, 18, hWnd, NULL, NULL, NULL);
		g_hDll = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			56, 104, 400, 24, hWnd, (HMENU)IDC_DLL, NULL, NULL);
		g_hBrowse = CreateWindowExW(0, L"BUTTON", L"...",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP, 462, 103, 30, 26, hWnd, (HMENU)IDC_BROWSE, NULL, NULL);

		g_hInject = CreateWindowExW(0, L"BUTTON", L"Inject",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16, 140, 120, 32, hWnd, (HMENU)IDC_INJECT, NULL, NULL);
		g_hStatus = CreateWindowExW(0, L"STATIC", L"Ready.",
			WS_CHILD | WS_VISIBLE, 150, 148, 378, 20, hWnd, (HMENU)IDC_STATUS, NULL, NULL);

		HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		SendMessageW(g_hProcess, WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(g_hRefresh, WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(g_hDll,     WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(g_hBrowse,  WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(g_hInject,  WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(g_hStatus,  WM_SETFONT, (WPARAM)font, TRUE);

		RefreshProcesses(g_hProcess, NULL);
		return 0;
	}

	case WM_COMMAND: {
		WORD id = LOWORD(wParam);

		if (id == IDC_REFRESH && HIWORD(wParam) == BN_CLICKED) {
			WCHAR pick[1024] = {0};
			int cur = (int)SendMessageW(g_hProcess, CB_GETCURSEL, 0, 0);
			if (cur >= 0)
				SendMessageW(g_hProcess, CB_GETLBTEXT, cur, (LPARAM)pick);
			RefreshProcesses(g_hProcess, pick);
			SetStatus(g_hStatus, L"Process list refreshed.", true);
		}
		else if (id == IDC_BROWSE && HIWORD(wParam) == BN_CLICKED) {
			WCHAR file[MAX_PATH] = {0};
			OPENFILENAMEW ofn;
			ZeroMemory(&ofn, sizeof(ofn));
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = hWnd;
			ofn.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files (*.*)\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
			if (GetOpenFileNameW(&ofn))
				SetWindowTextW(g_hDll, file);
		}
		else if (id == IDC_INJECT && HIWORD(wParam) == BN_CLICKED) {
			DWORD pid = GetSelectedPid(g_hProcess);
			if (!pid) {
				SetStatus(g_hStatus, L"Select a process first.", false);
				break;
			}

			WCHAR dll[MAX_PATH] = {0};
			GetWindowTextW(g_hDll, dll, MAX_PATH);
			if (!*dll) {
				int n = (int)wcslen(g_exeDir);
				if (n + 24 < MAX_PATH) {
					memcpy(dll, g_exeDir, (size_t)n * sizeof(WCHAR));
					wcscpy(dll + n, L"csgo-release.dll");
				}
			}
			if (!*dll || !PathFileExistsW(dll)) {
				SetStatus(g_hStatus, L"DLL not found.", false);
				break;
			}

			SetWindowTextW(g_hInject, L"Injecting...");
			EnableWindow(g_hInject, FALSE);
			SetStatus(g_hStatus, L"Injecting...", true);

			DWORD err = 0;
			WCHAR mod[32] = {0};
			WCHAR status[160];
			if (DoInject(pid, dll, &err, mod, 32)) {
				swprintf(status, 160, L"Loaded in target at %s - OK", mod);
				SetStatus(g_hStatus, status, true);
			} else {
				swprintf(status, 160, L"Failed (error %lu)", (unsigned long)err);
				SetStatus(g_hStatus, status, false);
			}

			EnableWindow(g_hInject, TRUE);
			SetWindowTextW(g_hInject, L"Inject");
		}
		return 0;
	}

	case WM_CTLCOLORSTATIC: {
		HDC dc = (HDC)wParam;
		if ((HWND)lParam == g_hStatus) {
			SetTextColor(dc, g_statusOk ? RGB(20, 120, 20) : RGB(170, 30, 30));
			SetBkMode(dc, TRANSPARENT);
			return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
		}
		break;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdShow, int)
{
	(void)hPrev; (void)cmdShow;

	INITCOMMONCONTROLSEX icc;
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_STANDARD_CLASSES;
	InitCommonControlsEx(&icc);

	GetExeDir();

	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"OpaiInjectorWnd";
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	RegisterClassExW(&wc);

	HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"Injector",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 550, 200, NULL, NULL, hInst, NULL);
	if (!hWnd)
		return 1;

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return (int)msg.wParam;
}
