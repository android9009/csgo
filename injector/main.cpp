#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstdio>
#include "status.hpp"

namespace {
constexpr UINT kMessage = WM_APP + 1;
HWND window{}, status_text{}, log_view{};
HANDLE stop_event{}, worker{};
std::wstring dll_path, log_path;

struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring Error(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring result = L"error " + std::to_wstring(code);
    if (n && text) {
        std::wstring detail(text, n);
        while (!detail.empty() && (detail.back() == L'\r' || detail.back() == L'\n' || detail.back() == L' ')) detail.pop_back();
        result += L": " + detail;
    }
    if (text) LocalFree(text);
    return result;
}
std::wstring FromUtf8(const char* text) {
    if (!text || !*text) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (!n) return L"Invalid error text";
    std::vector<wchar_t> buffer(static_cast<size_t>(n));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer.data(), n);
    return buffer.data();
}
bool WriteLog(const std::wstring& message) {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t prefix[48]{};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    if (!n) return false;
    std::string bytes(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), bytes.data(), n, nullptr, nullptr);
    Handle file(CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.value || file.value == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    return WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
}
void Emit(const std::wstring& message) {
    const bool ok = WriteLog(message);
    auto text = std::make_unique<std::wstring>(message);
    if (!ok) *text += L"\r\nFailed to write pastavar_debug.log.";
    if (PostMessageW(window, kMessage, 0, reinterpret_cast<LPARAM>(text.get()))) text.release();
}
bool Wait(DWORD ms) { return WaitForSingleObject(stop_event, ms) == WAIT_OBJECT_0; }
DWORD FindGame() {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap.value == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap.value, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"csgo.exe") == 0) return entry.th32ProcessID;
    } while (Process32NextW(snap.value, &entry));
    return 0;
}
struct Module { std::wstring name, path; uintptr_t base; };
bool Modules(DWORD pid, std::vector<Module>& result, DWORD& error) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snap.value == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snap.value, &entry)) { error = GetLastError(); return false; }
    do { result.push_back({entry.szModule, entry.szExePath, reinterpret_cast<uintptr_t>(entry.modBaseAddr)}); }
    while (Module32NextW(snap.value, &entry));
    return true;
}
const Module* FindModule(const std::vector<Module>& modules, const wchar_t* name) {
    for (const auto& m : modules) if (_wcsicmp(m.name.c_str(), name) == 0) return &m;
    return nullptr;
}
bool Alive(HANDLE process) { return WaitForSingleObject(process, 0) == WAIT_TIMEOUT; }
bool CheckArchitecture(HANDLE process, std::wstring& error) {
    BOOL game{}, self{};
    if (!IsWow64Process(process, &game) || !IsWow64Process(GetCurrentProcess(), &self)) { error = Error(GetLastError()); return false; }
    if (game != self) { error = L"CS:GO must be a 32-bit process"; return false; }
    return true;
}
void Monitor(DWORD pid, HANDLE process, PastavarStatus::Shared* shared, bool already_injected = false) {
    LONG previous = -1;
    DWORD since = GetTickCount();
    while (!Wait(200)) {
        if (!Alive(process)) {
            DWORD code{};
            if (GetExitCodeProcess(process, &code) && code != 0) {
                wchar_t message[128]{};
                swprintf_s(message, L"Status: Crashed - CS:GO exited with code 0x%08lX (cause unknown)", code);
                Emit(message);
            } else Emit(L"CS:GO exited");
            return;
        }
        const LONG state = InterlockedCompareExchange(&shared->state, 0, 0);
        if (state != previous && shared->protocol == PastavarStatus::version) {
            previous = state;
            const wchar_t* name = state == PastavarStatus::Initializing ? L"Initializing" :
                state == PastavarStatus::Ready ? L"Ready" : state == PastavarStatus::Failed ? L"Failed" :
                state == PastavarStatus::Crashed ? L"Crashed" : state == PastavarStatus::Unloaded ? L"Unloaded" : nullptr;
            if (name) {
                std::wstring message = std::wstring(L"Status: ") + name;
                if (already_injected) message += L" (already injected)";
                if ((state == PastavarStatus::Failed || state == PastavarStatus::Crashed) && shared->detail[0])
                    message += L" - " + FromUtf8(shared->detail);
                Emit(message);
            }
            if (state == PastavarStatus::Failed || state == PastavarStatus::Crashed || state == PastavarStatus::Unloaded) return;
        }
        if ((state == PastavarStatus::Unknown || state == PastavarStatus::Initializing) && GetTickCount() - since > 60000) {
            Emit(L"Status: Failed - DLL did not report ready within 60 seconds"); return;
        }
        if (state == PastavarStatus::Ready) {
            std::vector<Module> modules; DWORD error{};
            if (Modules(pid, modules, error) && !FindModule(modules, L"pastavar.dll")) { Emit(L"Status: Unloaded"); return; }
        }
    }
}
DWORD WINAPI Run(LPVOID) {
    Emit(L"Waiting for CS:GO...");
    DWORD pid{};
    while (!(pid = FindGame())) if (Wait(500)) return 0;
    Emit(L"CS:GO found (PID: " + std::to_wstring(pid) + L")");
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process.value) { Emit(L"Status: Failed - OpenProcess: " + Error(GetLastError())); return 0; }
    std::wstring architecture_error;
    if (!CheckArchitecture(process.value, architecture_error)) { Emit(L"Status: Failed - " + architecture_error); return 0; }
    if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES) { Emit(L"Status: Failed - pastavar.dll not found next to the injector"); return 0; }

    wchar_t mutex_name[64]{};
    swprintf_s(mutex_name, L"Local\\PastavarInjector_%lu", static_cast<unsigned long>(pid));
    Handle injection_guard(CreateMutexW(nullptr, FALSE, mutex_name));
    if (!injection_guard.value) { Emit(L"Status: Failed - injector lock: " + Error(GetLastError())); return 0; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) { Emit(L"Status: Failed - another injector is already monitoring CS:GO"); return 0; }

    wchar_t mapping_name[64]{}; PastavarStatus::MappingName(pid, mapping_name);
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(PastavarStatus::Shared), mapping_name));
    if (!mapping.value) { Emit(L"Status: Failed - status mapping: " + Error(GetLastError())); return 0; }
    const bool new_mapping = GetLastError() != ERROR_ALREADY_EXISTS;
    auto shared = static_cast<PastavarStatus::Shared*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PastavarStatus::Shared)));
    if (!shared) { Emit(L"Status: Failed - status view: " + Error(GetLastError())); return 0; }
    struct Unmap { void* p; ~Unmap() { UnmapViewOfFile(p); } } unmap{shared};
    if (new_mapping) { ZeroMemory(shared, sizeof(*shared)); shared->protocol = PastavarStatus::version; }

    std::vector<Module> modules;
    while (Alive(process.value) && !Wait(300)) {
        modules.clear(); DWORD error{};
        if (!Modules(pid, modules, error)) {
            if (error == ERROR_BAD_LENGTH || error == ERROR_PARTIAL_COPY) continue;
            Emit(L"Status: Failed - module enumeration: " + Error(error)); return 0;
        }
        if (FindModule(modules, L"pastavar.dll")) {
            Emit(L"pastavar.dll is already injected");
            if (new_mapping || shared->state == PastavarStatus::Unknown)
                Emit(L"Status: Unknown - loaded DLL did not provide status");
            else Monitor(pid, process.value, shared, true);
            return 0;
        }
        if (FindModule(modules, L"client.dll") && FindModule(modules, L"engine.dll") &&
            FindModule(modules, L"serverbrowser.dll") && FindModule(modules, L"gameoverlayrenderer.dll")) break;
    }
    if (Wait(0)) return 0;
    if (!Alive(process.value)) { Emit(L"Status: Failed - CS:GO exited before injection"); return 0; }

    auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner{};
    wchar_t owner_path[MAX_PATH]{};
    if (!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(load), &owner) || !GetModuleFileNameW(owner, owner_path, MAX_PATH)) {
        Emit(L"Status: Failed - cannot resolve LoadLibraryW: " + Error(GetLastError())); return 0;
    }
    const wchar_t* owner_name = wcsrchr(owner_path, L'\\'); owner_name = owner_name ? owner_name + 1 : owner_path;
    const Module* remote_owner = FindModule(modules, owner_name);
    if (!remote_owner) { Emit(L"Status: Failed - LoadLibraryW module not found in CS:GO"); return 0; }
    auto remote_load = reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_owner->base +
        (reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner)));
    const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) { Emit(L"Status: Failed - VirtualAllocEx: " + Error(GetLastError())); return 0; }
    SIZE_T written{};
    if (!WriteProcessMemory(process.value, remote_path, dll_path.c_str(), bytes, &written) || written != bytes) {
        const DWORD error = GetLastError(); VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE);
        Emit(L"Status: Failed - WriteProcessMemory: " + Error(error)); return 0;
    }
    Emit(L"Injecting pastavar.dll...");
    Handle thread(CreateRemoteThread(process.value, nullptr, 0, remote_load, remote_path, 0, nullptr));
    if (!thread.value) { const DWORD error = GetLastError(); VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE); Emit(L"Status: Failed - CreateRemoteThread: " + Error(error)); return 0; }
    DWORD start = GetTickCount();
    while (WaitForSingleObject(thread.value, 200) == WAIT_TIMEOUT) {
        if (Wait(0)) return 0; // remote thread may still need the path
        if (!Alive(process.value)) { Emit(L"Status: Crashed - CS:GO exited during LoadLibraryW"); return 0; }
        if (GetTickCount() - start > 30000) { Emit(L"Status: Failed - LoadLibraryW timed out; remote thread may still be running"); return 0; }
    }
    DWORD result{};
    if (!GetExitCodeThread(thread.value, &result)) { Emit(L"Status: Failed - GetExitCodeThread: " + Error(GetLastError())); return 0; }
    VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE);
    if (!result) { Emit(L"Status: Failed - LoadLibraryW returned null; Windows did not provide a remote error code"); return 0; }
    Emit(L"Injected successfully");
    Monitor(pid, process.value, shared);
    return 0;
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case kMessage: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
        SetWindowTextW(status_text, *text == L"Waiting for CS:GO..." ? L"Please open CS:GO" : text->c_str());
#ifdef _DEBUG
        const int length = GetWindowTextLengthW(log_view);
        SendMessageW(log_view, EM_SETSEL, length, length);
        const std::wstring line = *text + L"\r\n";
        SendMessageW(log_view, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
#endif
        return 0;
    }
    case WM_COMMAND: if (LOWORD(wp) == 1) { SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0; } break;
    case WM_CLOSE:
        SetEvent(stop_event);
        if (worker) WaitForSingleObject(worker, 2000);
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) { MessageBoxW(nullptr, L"Cannot determine injector directory", L"Pastavar", MB_OK | MB_ICONERROR); return 1; }
    std::wstring directory(path, length);
    const auto slash = directory.find_last_of(L"\\/");
    directory = slash == std::wstring::npos ? L".\\" : directory.substr(0, slash + 1);
    dll_path = directory + L"pastavar.dll"; log_path = directory + L"pastavar_debug.log";
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) { MessageBoxW(nullptr, L"Cannot create stop event", L"Pastavar", MB_OK | MB_ICONERROR); return 1; }
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = L"PastavarInjector"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) { CloseHandle(stop_event); return 1; }
    window = CreateWindowW(wc.lpszClassName, L"Pastavar Injector", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 280, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(stop_event); return 1; }
    status_text = CreateWindowW(L"STATIC", L"Please open CS:GO", WS_CHILD | WS_VISIBLE, 20, 18, 430, 42, window, nullptr, instance, nullptr);
#ifdef _DEBUG
    log_view = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
        20, 64, 424, 115, window, nullptr, instance, nullptr);
#endif
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 354, 192, 90, 30, window, reinterpret_cast<HMENU>(1), instance, nullptr);
    ShowWindow(window, show); UpdateWindow(window);
    worker = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (!worker) { SetWindowTextW(status_text, L"Status: Failed - cannot create worker thread"); WriteLog(L"Status: Failed - cannot create worker thread"); }
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (worker) CloseHandle(worker);
    CloseHandle(stop_event);
    return 0;
}
